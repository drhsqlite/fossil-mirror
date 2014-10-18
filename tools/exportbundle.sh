#!/bin/sh
# This is a very, very prototype proof-of-concept tool to generate 'push
# requests'. It diffs two repositories (currently only local ones) and
# generates a bundle which contains all the artifacts needed to
# reproduce a particular artifact.
# 
# The intended workflow is: user says 'I want to make a bundle to update
# OLD.fossil to checkin X of NEW.fossil'; the tool walks the checkin tree
# of NEW.fossil to figure out what checkins are necessary to reproduce X;
# then it removes all the checkins which are present in OLD.fossil; then
# it emits the bundle.
#
# To import, simple clone oldrepo (or not, if you're feeling brave);
# then fossil import --incremental the bundle.

set -e

oldrepo=$1
newrepo=$2
artifact=$3

ridlist=ridlist

fossil sqlite3 >$ridlist <<-EOF

ATTACH DATABASE "$newrepo" AS new;
ATTACH DATABASE "$oldrepo" AS old;

-- Map of parent -> child checkin artifacts. This contains our checkin graph.

CREATE TEMPORARY VIEW newcheckinmap AS
	SELECT
		child.uuid AS child,
		child.rid AS rid,
		parent.uuid AS parent,
		parent.rid AS parentrid,
		plink.mtime AS mtime
	FROM
		new.plink,
		new.blob AS parent,
		new.blob AS child
	WHERE
		(child.rid = plink.cid)
		AND (parent.rid = plink.pid);
	
CREATE TEMPORARY VIEW oldcheckinmap AS
	SELECT
		child.uuid AS child,
		child.rid AS rid,
		parent.uuid AS parent,
		parent.rid AS parentrid,
		plink.mtime AS mtime
	FROM
		old.plink,
		old.blob AS parent,
		old.blob AS child
	WHERE
		(child.rid = plink.cid)
		AND (parent.rid = plink.pid);
	
-- Create sets of all checkins (unordered). We construct these from the graph
-- so we get only checkin artifacts.

CREATE TEMPORARY VIEW newcheckins AS
	SELECT parent AS id, parentrid AS rid FROM newcheckinmap
	UNION
	SELECT child AS id, rid AS rid FROM newcheckinmap;

CREATE TEMPORARY VIEW oldcheckins AS
	SELECT parent AS id, parentrid AS rid FROM oldcheckinmap
	UNION
	SELECT child AS id, rid AS rid FROM oldcheckinmap;

-- Now create maps of checkin->file artifacts.

CREATE TEMPORARY VIEW newfiles AS
	SELECT
		checkin.uuid AS checkin,
		file.uuid AS file,
		file.rid AS rid
	FROM
		new.mlink,
		new.blob AS checkin,
		new.blob AS file
	WHERE
		(checkin.rid = mlink.mid)
		AND (file.rid = mlink.fid);

-- Walk the tree and figure out what checkins need to go into the bundle.

CREATE TEMPORARY VIEW desiredcheckins AS
	WITH RECURSIVE
	  ancestors(id, mtime) AS (
			SELECT child AS id, mtime
			FROM newcheckinmap
			WHERE child LIKE "$artifact%"
		UNION 
			SELECT
				newcheckinmap.parent AS id,
				newcheckinmap.mtime
			FROM
				newcheckinmap, ancestors
			ON
				newcheckinmap.child = ancestors.id
			WHERE
				-- Filter to include checkins which *aren't* in oldrepo.
				NOT EXISTS(SELECT * FROM oldcheckinmap WHERE
					oldcheckinmap.child = newcheckinmap.parent)
			ORDER BY
				newcheckinmap.mtime DESC
	  )
	SELECT * FROM ancestors;

-- Now we know what checkins are going in the bundle, figure out which
-- files get included.

CREATE TEMPORARY VIEW desiredfiles AS
	SELECT
		newfiles.file AS id
	FROM
		newfiles,
		desiredcheckins
	WHERE
		newfiles.checkin = desiredcheckins.id;

-- Because this prototype is using the git exporter to create bundles, and the
-- exporter's ability to select artifacts is based on having a list of rids to
-- ignore, we have to emit a list of all rids in newrepo which don't correspond
-- to the list above.

CREATE TEMPORARY VIEW skipcheckinrids AS
	SELECT
		"c" || oldcheckins.rid AS msg,
		oldcheckins.rid AS rid,
		oldcheckins.id AS id
	FROM
		oldcheckins LEFT JOIN desiredcheckins
	ON
		desiredcheckins.id = oldcheckins.id
	WHERE
		desiredcheckins.id IS NULL
	ORDER BY
		rid ASC;

CREATE TEMPORARY VIEW skipfilerids AS
	SELECT
		"b" || newfiles.rid AS msg,
		newfiles.rid AS rid,
		newfiles.file AS id
	FROM
		newfiles, skipcheckinrids
	WHERE
		newfiles.checkin = skipcheckinrids.id
	ORDER BY
		rid ASC;

SELECT msg FROM skipfilerids
UNION
SELECT msg FROM skipcheckinrids;

EOF

fossil export --git --import-marks $ridlist $newrepo

