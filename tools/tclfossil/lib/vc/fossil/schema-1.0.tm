## -*- tcl -*-
# # ## ### ##### ######## ############# #####################
## Copyright (c) 2008 Mark Janssen.
#
# This software is licensed as described in the file LICENSE, which
# you should have received as part of this distribution.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://fossil-scm.hwaci.com/fossil
# # ## ### ##### ######## ############# #####################

## Repository schema's

# # ## ### ##### ######## ############# #####################
## Requirements

package require Tcl 8.5                             ; # Required runtime.
package require snit                                ; # OO system.

package provide vc::fossil::schema 1.0

# # ## ### ##### ######## ############# #####################
##



namespace eval ::vc::fossil {

    snit::type schema {
        typemethod repo1 {} {
	    return {
		-- The BLOB and DELTA tables contain all records held in the repository.
		--
		-- The BLOB.CONTENT column is always compressed using libz.  This
		-- column might hold the full text of the record or it might hold
		-- a delta that is able to reconstruct the record from some other
		-- record.  If BLOB.CONTENT holds a delta, then a DELTA table entry
		-- will exist for the record and that entry will point to another
		-- entry that holds the source of the delta.  Deltas can be chained.
		--
		CREATE TABLE blob(
				  rid INTEGER PRIMARY KEY,        -- Record ID
				  rcvid INTEGER,                  -- Origin of this record
				  size INTEGER,                   -- Size of content. -1 for a phantom.
				  uuid TEXT UNIQUE,               -- SHA1 hash of the content
				  content BLOB                    -- Compressed content of this record
				  );
		CREATE TABLE delta(
				   rid INTEGER PRIMARY KEY,                 -- Record ID
				   srcid INTEGER NOT NULL REFERENCES blob   -- Record holding source document
				   );
		CREATE INDEX delta_i1 ON delta(srcid);

		-- Whenever new blobs are received into the repository, an entry
		-- in this table records the source of the blob.
		--
		CREATE TABLE rcvfrom(
				     rcvid INTEGER PRIMARY KEY,      -- Received-From ID
				     uid INTEGER REFERENCES user,    -- User login
				     mtime DATETIME,                 -- Time or receipt
				     nonce TEXT UNIQUE,              -- Nonce used for login
				     ipaddr TEXT                     -- Remote IP address.  NULL for direct.
				     );

		-- Information about users
		--
		CREATE TABLE user(
				  uid INTEGER PRIMARY KEY,        -- User ID
				  login TEXT,                     -- login name of the user
				  pw TEXT,                        -- password
				  cap TEXT,                       -- Capabilities of this user
				  cookie TEXT,                    -- WWW login cookie
				  ipaddr TEXT,                    -- IP address for which cookie is valid
				  cexpire DATETIME,               -- Time when cookie expires
				  info TEXT,                      -- contact information
				  photo BLOB                      -- JPEG image of this user
				  );

		-- The VAR table holds miscellanous information about the repository.
		-- in the form of name-value pairs.
		--
		CREATE TABLE config(
				    name TEXT PRIMARY KEY NOT NULL,  -- Primary name of the entry
				    value CLOB,                      -- Content of the named parameter
				    CHECK( typeof(name)='text' AND length(name)>=1 )
				    );

		-- Artifacts that should not be processed are identified in the
		-- "shun" table.  Artifacts that are control-file forgeries or
		-- spam can be shunned in order to prevent them from contaminating
		-- the repository.
		--
		CREATE TABLE shun(uuid UNIQUE);

		-- An entry in this table describes a database query that generates a
		-- table of tickets.
		--
		CREATE TABLE reportfmt(
				       rn integer primary key,  -- Report number
				       owner text,              -- Owner of this report format (not used)
				       title text,              -- Title of this report
				       cols text,               -- A color-key specification
				       sqlcode text             -- An SQL SELECT statement for this report
				       );
	    }
	}
	typemethod repo2 {} {
	    return {
		-- Filenames
		--
		CREATE TABLE filename(
				      fnid INTEGER PRIMARY KEY,    -- Filename ID
				      name TEXT UNIQUE             -- Name of file page
				      );

		-- Linkages between manifests, files created by that manifest, and
		-- the names of those files.
		--
		-- pid==0 if the file is added by check-in mid.
		-- fid==0 if the file is removed by check-in mid.
		--
		CREATE TABLE mlink(
				   mid INTEGER REFERENCES blob,        -- Manifest ID where change occurs
				   pid INTEGER REFERENCES blob,        -- File ID in parent manifest
				   fid INTEGER REFERENCES blob,        -- Changed file ID in this manifest
				   fnid INTEGER REFERENCES filename    -- Name of the file
				   );
		CREATE INDEX mlink_i1 ON mlink(mid);
		CREATE INDEX mlink_i2 ON mlink(fnid);
		CREATE INDEX mlink_i3 ON mlink(fid);
		CREATE INDEX mlink_i4 ON mlink(pid);

		-- Parent/child linkages
		--
		CREATE TABLE plink(
				   pid INTEGER REFERENCES blob,    -- Parent manifest
				   cid INTEGER REFERENCES blob,    -- Child manifest
				   isprim BOOLEAN,                 -- pid is the primary parent of cid
				   mtime DATETIME,                 -- the date/time stamp on cid
				   UNIQUE(pid, cid)
				   );
		CREATE INDEX plink_i2 ON plink(cid);

		-- Events used to generate a timeline
		--
		CREATE TABLE event(
				   type TEXT,                      -- Type of event
				   mtime DATETIME,                 -- Date and time when the event occurs
				   objid INTEGER PRIMARY KEY,      -- Associated record ID
				   uid INTEGER REFERENCES user,    -- User who caused the event
				   bgcolor TEXT,                   -- Color set by 'bgcolor' property
				   brbgcolor TEXT,                 -- Color set by 'br-bgcolor' property
				   euser TEXT,                     -- User set by 'user' property
				   user TEXT,                      -- Name of the user
				   ecomment TEXT,                  -- Comment set by 'comment' property
				   comment TEXT                    -- Comment describing the event
				   );
		CREATE INDEX event_i1 ON event(mtime);

		-- A record of phantoms.  A phantom is a record for which we know the
		-- UUID but we do not (yet) know the file content.
		--
		CREATE TABLE phantom(
				     rid INTEGER PRIMARY KEY         -- Record ID of the phantom
				     );

		-- Unclustered records.  An unclustered record is a record (including
									    -- a cluster records themselves) that is not mentioned by some other
		-- cluster.
		--
		-- Phantoms are usually included in the unclustered table.  A new cluster
		-- will never be created that contains a phantom.  But another repository
		-- might send us a cluster that contains entries that are phantoms to
		-- us.
		--
		CREATE TABLE unclustered(
					 rid INTEGER PRIMARY KEY         -- Record ID of the unclustered file
					 );

		-- Records which have never been pushed to another server.  This is
		-- used to reduce push operations to a single HTTP request in the
		-- common case when one repository only talks to a single server.
		--
		CREATE TABLE unsent(
				    rid INTEGER PRIMARY KEY         -- Record ID of the phantom
				    );

		-- Each baseline or manifest can have one or more tags.  A tag
		-- is defined by a row in the next table.
		-- 
		-- Wiki pages are tagged with "wiki-NAME" where NAME is the name of
		-- the wiki page.  Tickets changes are tagged with "ticket-UUID" where 
		-- UUID is the indentifier of the ticket.
		--
		CREATE TABLE tag(
				 tagid INTEGER PRIMARY KEY,       -- Numeric tag ID
				 tagname TEXT UNIQUE              -- Tag name.
				 );
		INSERT INTO tag VALUES(1, 'bgcolor');         -- TAG_BGCOLOR
		INSERT INTO tag VALUES(2, 'comment');         -- TAG_COMMENT
		INSERT INTO tag VALUES(3, 'user');            -- TAG_USER
		INSERT INTO tag VALUES(4, 'hidden');          -- TAG_HIDDEN

		-- Assignments of tags to baselines.  Note that we allow tags to
		-- have values assigned to them.  So we are not really dealing with
		-- tags here.  These are really properties.  But we are going to
		-- keep calling them tags because in many cases the value is ignored.
		--
		CREATE TABLE tagxref(
				     tagid INTEGER REFERENCES tag,   -- The tag that added or removed
				     tagtype INTEGER,                -- 0:cancel  1:single  2:branch
				     srcid INTEGER REFERENCES blob,  -- Origin of the tag. 0 for propagated tags
				     value TEXT,                     -- Value of the tag.  Might be NULL.
				     mtime TIMESTAMP,                -- Time of addition or removal
				     rid INTEGER REFERENCE blob,     -- Baseline that tag added/removed from
				     UNIQUE(rid, tagid)
				     );
		CREATE INDEX tagxref_i1 ON tagxref(tagid, mtime);
	    }
	}  
    }
}