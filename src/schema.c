/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains string constants that implement the database schema.
*/
#include "config.h"
#include "schema.h"

/*
** The database schema for the ~/.fossil configuration database.
*/
const char zConfigSchema[] =
@ -- This file contains the schema for the database that is kept in the
@ -- ~/.fossil file and that stores information about the users setup.
@ --
@ CREATE TABLE global_config(
@   name TEXT PRIMARY KEY,
@   value TEXT
@ ) WITHOUT ROWID;
@
@ -- Identifier for this file type.
@ -- The integer is the same as 'FSLG'.
@ PRAGMA application_id=252006675;
;

#if INTERFACE
/*
** The content tables have a content version number which rarely
** changes.  The aux tables have an arbitrary version number (typically
** a date) which can change frequently.  When the content schema changes,
** we have to execute special procedures to update the schema.  When
** the aux schema changes, all we need to do is rebuild the database.
*/
#define CONTENT_SCHEMA  "2"
#define AUX_SCHEMA_MIN  "2011-04-25 19:50"
#define AUX_SCHEMA_MAX  "2015-01-24"
/* NB:  Some features require the latest schema.  Warning or error messages
** will appear if an older schema is used.  However, the older schemas are
** adequate for many common functions. */

#endif /* INTERFACE */


/*
** The schema for a repository database.
**
** Schema1[] contains parts of the schema that are fixed and unchanging
** across versions.  Schema2[] contains parts of the schema that can
** change from one version to the next.  The information in Schema2[]
** is reconstructed from the information in Schema1[] by the "rebuild"
** operation.
*/
const char zRepositorySchema1[] =
@ -- The BLOB and DELTA tables contain all records held in the repository.
@ --
@ -- The BLOB.CONTENT column is always compressed using zlib.  This
@ -- column might hold the full text of the record or it might hold
@ -- a delta that is able to reconstruct the record from some other
@ -- record.  If BLOB.CONTENT holds a delta, then a DELTA table entry
@ -- will exist for the record and that entry will point to another
@ -- entry that holds the source of the delta.  Deltas can be chained.
@ --
@ -- The blob and delta tables collectively hold the "global state" of
@ -- a Fossil repository.
@ --
@ CREATE TABLE blob(
@   rid INTEGER PRIMARY KEY,        -- Record ID
@   rcvid INTEGER,                  -- Origin of this record
@   size INTEGER,                   -- Size of content. -1 for a phantom.
@   uuid TEXT UNIQUE NOT NULL,      -- hash of the content
@   content BLOB,                   -- Compressed content of this record
@   CHECK( length(uuid)>=40 AND rid>0 )
@ );
@ CREATE TABLE delta(
@   rid INTEGER PRIMARY KEY,                 -- BLOB that is delta-compressed
@   srcid INTEGER NOT NULL REFERENCES blob   -- Baseline for delta-compression
@ );
@ CREATE INDEX delta_i1 ON delta(srcid);
@
@ -------------------------------------------------------------------------
@ -- The BLOB and DELTA tables above hold the "global state" of a Fossil
@ -- project; the stuff that is normally exchanged during "sync".  The
@ -- "local state" of a repository is contained in the remaining tables of
@ -- the zRepositorySchema1 string.
@ -------------------------------------------------------------------------
@
@ -- Whenever new blobs are received into the repository, an entry
@ -- in this table records the source of the blob.
@ --
@ CREATE TABLE rcvfrom(
@   rcvid INTEGER PRIMARY KEY,      -- Received-From ID
@   uid INTEGER REFERENCES user,    -- User login
@   mtime DATETIME,                 -- Time of receipt.  Julian day.
@   nonce TEXT UNIQUE,              -- Nonce used for login
@   ipaddr TEXT                     -- Remote IP address.  NULL for direct.
@ );
@
@ -- Information about users
@ --
@ -- The user.pw field can be either cleartext of the password, or
@ -- a SHA1 hash of the password.  If the user.pw field is exactly 40
@ -- characters long we assume it is a SHA1 hash.  Otherwise, it is
@ -- cleartext.  The sha1_shared_secret() routine computes the password
@ -- hash based on the project-code, the user login, and the cleartext
@ -- password.
@ --
@ CREATE TABLE user(
@   uid INTEGER PRIMARY KEY,        -- User ID
@   login TEXT UNIQUE,              -- login name of the user
@   pw TEXT,                        -- password
@   cap TEXT,                       -- Capabilities of this user
@   cookie TEXT,                    -- WWW login cookie
@   ipaddr TEXT,                    -- IP address for which cookie is valid
@   cexpire DATETIME,               -- Time when cookie expires
@   info TEXT,                      -- contact information
@   mtime DATE,                     -- last change.  seconds since 1970
@   photo BLOB,                     -- JPEG image of this user
@   jx TEXT DEFAULT '{}'            -- Extra fields in JSON
@ );
@
@ -- The config table holds miscellanous information about the repository.
@ -- in the form of name-value pairs.
@ --
@ CREATE TABLE config(
@   name TEXT PRIMARY KEY NOT NULL,  -- Primary name of the entry
@   value CLOB,                      -- Content of the named parameter
@   mtime DATE,                      -- last modified.  seconds since 1970
@   CHECK( typeof(name)='text' AND length(name)>=1 )
@ ) WITHOUT ROWID;
@
@ -- Artifacts that should not be processed are identified in the
@ -- "shun" table.  Artifacts that are control-file forgeries or
@ -- spam or artifacts whose contents violate administrative policy
@ -- can be shunned in order to prevent them from contaminating
@ -- the repository.
@ --
@ -- Shunned artifacts do not exist in the blob table.  Hence they
@ -- have not artifact ID (rid) and we thus must store their full
@ -- UUID.
@ --
@ CREATE TABLE shun(
@   uuid TEXT PRIMARY KEY,-- UUID of artifact to be shunned. Canonical form
@   mtime DATE,           -- When added.  seconds since 1970
@   scom TEXT             -- Optional text explaining why the shun occurred
@ ) WITHOUT ROWID;
@
@ -- Artifacts that should not be pushed are stored in the "private"
@ -- table.  Private artifacts are omitted from the "unclustered" and
@ -- "unsent" tables.
@ --
@ -- A phantom artifact (that is, an artifact with BLOB.SIZE<0 - an artifact
@ -- for which we do not know the content) might also be marked as private.
@ -- This comes about when an artifact is named in a manifest or tag but
@ -- the content of that artifact is held privately by some other peer
@ -- repository.
@ --
@ CREATE TABLE private(rid INTEGER PRIMARY KEY);
@
@ -- An entry in this table describes a database query that generates a
@ -- table of tickets.
@ --
@ CREATE TABLE reportfmt(
@    rn INTEGER PRIMARY KEY,  -- Report number
@    owner TEXT,              -- Owner of this report format (not used)
@    title TEXT UNIQUE,       -- Title of this report
@    mtime DATE,              -- Last modified.  seconds since 1970
@    cols TEXT,               -- A color-key specification
@    sqlcode TEXT,            -- An SQL SELECT statement for this report
@    jx TEXT DEFAULT '{}'     -- Additional fields encoded as JSON
@ );
@
@ -- Some ticket content (such as the originators email address or contact
@ -- information) needs to be obscured to protect privacy.  This is achieved
@ -- by storing an SHA1 hash of the content.  For display, the hash is
@ -- mapped back into the original text using this table.
@ --
@ -- This table contains sensitive information and should not be shared
@ -- with unauthorized users.
@ --
@ CREATE TABLE concealed(
@   hash TEXT PRIMARY KEY,    -- The SHA1 hash of content
@   mtime DATE,               -- Time created.  Seconds since 1970
@   content TEXT              -- Content intended to be concealed
@ ) WITHOUT ROWID;
@
@ -- The application ID helps the unix "file" command to identify the
@ -- database as a fossil repository.
@ PRAGMA application_id=252006673;
;

/*
** The default reportfmt entry for the schema. This is in an extra
** script so that (configure reset) can install the default report.
*/
const char zRepositorySchemaDefaultReports[] =
@ INSERT INTO reportfmt(title,mtime,cols,sqlcode)
@ VALUES('All Tickets',julianday('1970-01-01'),'#ffffff Key:
@ #f2dcdc Active
@ #e8e8e8 Review
@ #cfe8bd Fixed
@ #bde5d6 Tested
@ #cacae5 Deferred
@ #c8c8c8 Closed','SELECT
@   CASE WHEN status IN (''Open'',''Verified'') THEN ''#f2dcdc''
@        WHEN status=''Review'' THEN ''#e8e8e8''
@        WHEN status=''Fixed'' THEN ''#cfe8bd''
@        WHEN status=''Tested'' THEN ''#bde5d6''
@        WHEN status=''Deferred'' THEN ''#cacae5''
@        ELSE ''#c8c8c8'' END AS ''bgcolor'',
@   substr(tkt_uuid,1,10) AS ''#'',
@   datetime(tkt_mtime) AS ''mtime'',
@   type,
@   status,
@   subsystem,
@   title
@ FROM ticket');
;

const char zRepositorySchema2[] =
@ -- Filenames
@ --
@ CREATE TABLE filename(
@   fnid INTEGER PRIMARY KEY,    -- Filename ID
@   name TEXT UNIQUE             -- Name of file page
@ );
@
@ -- Linkages between check-ins, files created by each check-in, and
@ -- the names of those files.
@ --
@ -- Each entry represents a file that changed content from pid to fid
@ -- due to the check-in that goes from pmid to mid.  fnid is the name
@ -- of the file in the mid check-in.  If the file was renamed as part
@ -- of the mid check-in, then pfnid is the previous filename.
@
@ -- There can be multiple entries for (mid,fid) if the mid check-in was
@ -- a merge.  Entries with isaux==0 are from the primary parent.  Merge
@ -- parents have isaux set to true.
@ --
@ -- Field name mnemonics:
@ --    mid = Manifest ID.  (Each check-in is stored as a "Manifest")
@ --    fid = File ID.
@ --    pmid = Parent Manifest ID.
@ --    pid = Parent file ID.
@ --    fnid = File Name ID.
@ --    pfnid = Parent File Name ID.
@ --    isaux = pmid IS AUXiliary parent, not primary parent
@ --
@ -- pid==0    if the file is added by check-in mid.
@ -- pid==(-1) if the file exists in a merge parents but not in the primary
@  --          parent.  In other words, if the file file was added by merge.
@ -- fid==0    if the file is removed by check-in mid.
@ --
@ CREATE TABLE mlink(
@   mid INTEGER,                       -- Check-in that contains fid
@   fid INTEGER,                       -- New file content. 0 if deleted
@   pmid INTEGER,                      -- Check-in that contains pid
@   pid INTEGER,                       -- Prev file content. 0 if new. -1 merge
@   fnid INTEGER REFERENCES filename,  -- Name of the file
@   pfnid INTEGER,                     -- Previous name. 0 if unchanged
@   mperm INTEGER,                     -- File permissions.  1==exec
@   isaux BOOLEAN DEFAULT 0            -- TRUE if pmid is the primary
@ );
@ CREATE INDEX mlink_i1 ON mlink(mid);
@ CREATE INDEX mlink_i2 ON mlink(fnid);
@ CREATE INDEX mlink_i3 ON mlink(fid);
@ CREATE INDEX mlink_i4 ON mlink(pid);
@
@ -- Parent/child linkages between check-ins
@ --
@ CREATE TABLE plink(
@   pid INTEGER REFERENCES blob,    -- Parent manifest
@   cid INTEGER REFERENCES blob,    -- Child manifest
@   isprim BOOLEAN,                 -- pid is the primary parent of cid
@   mtime DATETIME,                 -- the date/time stamp on cid.  Julian day.
@   baseid INTEGER REFERENCES blob, -- Baseline if cid is a delta manifest.
@   UNIQUE(pid, cid)
@ );
@ CREATE INDEX plink_i2 ON plink(cid,pid);
@
@ -- A "leaf" check-in is a check-in that has no children in the same
@ -- branch.  The set of all leaves is easily computed with a join,
@ -- between the plink and tagxref tables, but it is a slower join for
@ -- very large repositories (repositories with 100,000 or more check-ins)
@ -- and so it makes sense to precompute the set of leaves.  There is
@ -- one entry in the following table for each leaf.
@ --
@ CREATE TABLE leaf(rid INTEGER PRIMARY KEY);
@
@ -- Events used to generate a timeline.  Type meanings:
@ --     ci    Check-ins
@ --     e     Technotes
@ --     f     Forum posts
@ --     g     Tags
@ --     t     Ticket changes
@ --     w     Wiki page edit
@ --
@ CREATE TABLE event(
@   type TEXT,                      -- Type of event: ci, e, f, g, t, w
@   mtime DATETIME,                 -- Time of occurrence. Julian day.
@   objid INTEGER PRIMARY KEY,      -- Associated record ID
@   tagid INTEGER,                  -- Associated ticket or wiki name tag
@   uid INTEGER REFERENCES user,    -- User who caused the event
@   bgcolor TEXT,                   -- Color set by 'bgcolor' property
@   euser TEXT,                     -- User set by 'user' property
@   user TEXT,                      -- Name of the user
@   ecomment TEXT,                  -- Comment set by 'comment' property
@   comment TEXT,                   -- Comment describing the event
@   brief TEXT,                     -- Short comment when tagid already seen
@   omtime DATETIME                 -- Original unchanged date+time, or NULL
@ );
@ CREATE INDEX event_i1 ON event(mtime);
@
@ -- A record of phantoms.  A phantom is a record for which we know the
@ -- file hash but we do not (yet) know the file content.
@ --
@ CREATE TABLE phantom(
@   rid INTEGER PRIMARY KEY         -- Record ID of the phantom
@ );
@
@ -- A record of orphaned delta-manifests.  An orphan is a delta-manifest
@ -- for which we have content, but its baseline-manifest is a phantom.
@ -- We have to track all orphan manifests so that when the baseline arrives,
@ -- we know to process the orphaned deltas.
@ CREATE TABLE orphan(
@   rid INTEGER PRIMARY KEY,        -- Delta manifest with a phantom baseline
@   baseline INTEGER                -- Phantom baseline of this orphan
@ );
@ CREATE INDEX orphan_baseline ON orphan(baseline);
@
@ -- Unclustered records.  An unclustered record is a record (including
@ -- a cluster records themselves) that is not mentioned by some other
@ -- cluster.
@ --
@ -- Phantoms are usually included in the unclustered table.  A new cluster
@ -- will never be created that contains a phantom.  But another repository
@ -- might send us a cluster that contains entries that are phantoms to
@ -- us.
@ --
@ CREATE TABLE unclustered(
@   rid INTEGER PRIMARY KEY         -- Record ID of the unclustered file
@ );
@
@ -- Records which have never been pushed to another server.  This is
@ -- used to reduce push operations to a single HTTP request in the
@ -- common case when one repository only talks to a single server.
@ --
@ CREATE TABLE unsent(
@   rid INTEGER PRIMARY KEY         -- Record ID of the phantom
@ );
@
@ -- Each artifact can have one or more tags.  A tag
@ -- is defined by a row in the next table.
@ --
@ -- Wiki pages are tagged with "wiki-NAME" where NAME is the name of
@ -- the wiki page.  Tickets changes are tagged with "ticket-HASH" where
@ -- HASH is the indentifier of the ticket.  Tags used to assign symbolic
@ -- names to baselines are branches are of the form "sym-NAME" where
@ -- NAME is the symbolic name.
@ --
@ CREATE TABLE tag(
@   tagid INTEGER PRIMARY KEY,       -- Numeric tag ID
@   tagname TEXT UNIQUE              -- Tag name.
@ );
@ INSERT INTO tag VALUES(1, 'bgcolor');         -- TAG_BGCOLOR
@ INSERT INTO tag VALUES(2, 'comment');         -- TAG_COMMENT
@ INSERT INTO tag VALUES(3, 'user');            -- TAG_USER
@ INSERT INTO tag VALUES(4, 'date');            -- TAG_DATE
@ INSERT INTO tag VALUES(5, 'hidden');          -- TAG_HIDDEN
@ INSERT INTO tag VALUES(6, 'private');         -- TAG_PRIVATE
@ INSERT INTO tag VALUES(7, 'cluster');         -- TAG_CLUSTER
@ INSERT INTO tag VALUES(8, 'branch');          -- TAG_BRANCH
@ INSERT INTO tag VALUES(9, 'closed');          -- TAG_CLOSED
@ INSERT INTO tag VALUES(10,'parent');          -- TAG_PARENT
@ INSERT INTO tag VALUES(11,'note');            -- TAG_NOTE
@
@ -- Assignments of tags to artifacts.  Note that we allow tags to
@ -- have values assigned to them.  So we are not really dealing with
@ -- tags here.  These are really properties.  But we are going to
@ -- keep calling them tags because in many cases the value is ignored.
@ --
@ CREATE TABLE tagxref(
@   tagid INTEGER REFERENCES tag,   -- The tag being added, removed,
@                                   -- or propagated
@   tagtype INTEGER,                -- 0:-,cancel  1:+,single  2:*,propagate
@   srcid INTEGER REFERENCES blob,  -- Artifact tag originates from, or
@                                   -- 0 for propagated tags
@   origid INTEGER REFERENCES blob, -- Artifact holding propagated tag
@                                   -- (any artifact type with a P-card)
@   value TEXT,                     -- Value of the tag.  Might be NULL.
@   mtime TIMESTAMP,                -- Time of addition or removal. Julian day
@   rid INTEGER REFERENCE blob,     -- Artifact tag is applied to
@   UNIQUE(rid, tagid)
@ );
@ CREATE INDEX tagxref_i1 ON tagxref(tagid, mtime);
@
@ -- When a hyperlink occurs from one artifact to another (for example
@ -- when a check-in comment refers to a ticket) an entry is made in
@ -- the following table for that hyperlink.  This table is used to
@ -- facilitate the display of "back links".
@ --
@ CREATE TABLE backlink(
@   target TEXT,           -- Where the hyperlink points to
@   srctype INT,           -- 0=comment 1=ticket 2=wiki. See BKLNK_* below.
@   srcid INT,             -- EVENT.OBJID for the source document
@   mtime TIMESTAMP,       -- time that the hyperlink was added. Julian day.
@   UNIQUE(target, srctype, srcid)
@ );
@ CREATE INDEX backlink_src ON backlink(srcid, srctype);
@
@ -- Each attachment is an entry in the following table.  Only
@ -- the most recent attachment (identified by the D card) is saved.
@ --
@ CREATE TABLE attachment(
@   attachid INTEGER PRIMARY KEY,   -- Local id for this attachment
@   isLatest BOOLEAN DEFAULT 0,     -- True if this is the one to use
@   mtime TIMESTAMP,                -- Last changed.  Julian day.
@   src TEXT,                       -- Hash of the attachment.  NULL to delete
@   target TEXT,                    -- Object attached to. Wikiname or Tkt hash
@   filename TEXT,                  -- Filename for the attachment
@   comment TEXT,                   -- Comment associated with this attachment
@   user TEXT                       -- Name of user adding attachment
@ );
@ CREATE INDEX attachment_idx1 ON attachment(target, filename, mtime);
@ CREATE INDEX attachment_idx2 ON attachment(src);
@
@ -- Template for the TICKET table
@ --
@ -- NB: when changing the schema of the TICKET table here, also make the
@ -- same change in tktsetup.c.
@ --
@ CREATE TABLE ticket(
@   -- Do not change any column that begins with tkt_
@   tkt_id INTEGER PRIMARY KEY,
@   tkt_uuid TEXT UNIQUE,
@   tkt_mtime DATE,
@   tkt_ctime DATE,
@   -- Add as many field as required below this line
@   type TEXT,
@   status TEXT,
@   subsystem TEXT,
@   priority TEXT,
@   severity TEXT,
@   foundin TEXT,
@   private_contact TEXT,
@   resolution TEXT,
@   title TEXT,
@   comment TEXT
@ );
@ CREATE TABLE ticketchng(
@   -- Do not change any column that begins with tkt_
@   tkt_id INTEGER REFERENCES ticket,
@   tkt_rid INTEGER REFERENCES blob,
@   tkt_mtime DATE,
@   tkt_user TEXT,
@   -- Add as many fields as required below this line
@   login TEXT,
@   username TEXT,
@   mimetype TEXT,
@   icomment TEXT
@ );
@ CREATE INDEX ticketchng_idx1 ON ticketchng(tkt_id, tkt_mtime);
@
@ -- For tracking cherrypick merges
@ CREATE TABLE cherrypick(
@   parentid INT,
@   childid INT,
@   isExclude BOOLEAN DEFAULT false,
@   PRIMARY KEY(parentid, childid)
@ ) WITHOUT ROWID;
@ CREATE INDEX cherrypick_cid ON cherrypick(childid);
;

/*
** Allowed values for backlink.srctype
*/
#if INTERFACE
# define BKLNK_COMMENT    0   /* Check-in comment */
# define BKLNK_TICKET     1   /* Ticket body or title */
# define BKLNK_WIKI       2   /* Wiki */
# define BKLNK_EVENT      3   /* Technote */
# define BKLNK_FORUM      4   /* Forum post */
# define ValidBklnk(X)   (X>=0 && X<=4)  /* True if backlink.srctype is valid */
#endif

/*
** Allowed values for MIMEtype codes
*/
#if INTERFACE
# define MT_NONE       0   /* unspecified */
# define MT_WIKI       1   /* Wiki */
# define MT_MARKDOWN   2   /* Markdonw */
# define MT_UNKNOWN    3   /* unknown  */
# define ValidMTC(X)  ((X)>=0 && (X)<=3)  /* True if MIMEtype code is valid */
#endif

/*
** Predefined tagid values
*/
#if INTERFACE
# define TAG_BGCOLOR    1     /* Set the background color for display */
# define TAG_COMMENT    2     /* The check-in comment */
# define TAG_USER       3     /* User who made a check-in */
# define TAG_DATE       4     /* The date of a check-in */
# define TAG_HIDDEN     5     /* Do not display in timeline */
# define TAG_PRIVATE    6     /* Do not sync */
# define TAG_CLUSTER    7     /* A cluster */
# define TAG_BRANCH     8     /* Value is name of the current branch */
# define TAG_CLOSED     9     /* Do not display this check-in as a leaf */
# define TAG_PARENT     10    /* Change to parentage on a check-in */
# define TAG_NOTE       11    /* Extra text appended to a check-in comment */
#endif

/*
** The schema for the local FOSSIL database file found at the root
** of every check-out.  This database contains the complete state of
** the check-out.  See also the addendum in zLocalSchemaVmerge[].
*/
const char zLocalSchema[] =
@ -- The VVAR table holds miscellanous information about the local checkout
@ -- in the form of name-value pairs.  This is similar to the VAR table
@ -- table in the repository except that this table holds information that
@ -- is specific to the local check-out.
@ --
@ -- Important Variables:
@ --
@ --     repository        Full pathname of the repository database
@ --     user-id           Userid to use
@ --
@ CREATE TABLE vvar(
@   name TEXT PRIMARY KEY NOT NULL,  -- Primary name of the entry
@   value CLOB,                      -- Content of the named parameter
@   CHECK( typeof(name)='text' AND length(name)>=1 )
@ ) WITHOUT ROWID;
@
@ -- Each entry in the vfile table represents a single file in the
@ -- current check-out.
@ --
@ -- The file.rid field is 0 for files or folders that have been
@ -- added but not yet committed.
@ --
@ -- Vfile.chnged meaning:
@ --    0       File is unmodified
@ --    1       Manually edited and/or modified as part of a merge command
@ --    2       Replaced by a merge command
@ --    3       Added by a merge command
@ --    4,5     Same as 2,3 except merge using --integrate
@ --
@ CREATE TABLE vfile(
@   id INTEGER PRIMARY KEY,           -- ID of the checked-out file
@   vid INTEGER REFERENCES blob,      -- The check-in this file is part of.
@   chnged INT DEFAULT 0,  -- 0:unchng 1:edit 2:m-chng 3:m-add 4:i-chng 5:i-add
@   deleted BOOLEAN DEFAULT 0,        -- True if deleted
@   isexe BOOLEAN,                    -- True if file should be executable
@   islink BOOLEAN,                   -- True if file should be symlink
@   rid INTEGER,                      -- Originally from this repository record
@   mrid INTEGER,                     -- Based on this record due to a merge
@   mtime INTEGER,                    -- Mtime of file on disk. sec since 1970
@   pathname TEXT,                    -- Full pathname relative to root
@   origname TEXT,                    -- Original pathname. NULL if unchanged
@   mhash TEXT,                       -- Hash of mrid iff mrid!=rid
@   UNIQUE(pathname,vid)
@ );
@
@ -- Identifier for this file type.
@ -- The integer is the same as 'FSLC'.
@ PRAGMA application_id=252006674;
;

/* Additional local database initialization following the schema
** enhancement of 2019-01-19, in which the mhash column was added
** to vmerge and vfile.
*/
const char zLocalSchemaVmerge[] =
@ -- This table holds a record of uncommitted merges in the local
@ -- file tree.  If a VFILE entry with id has merged with another
@ -- record, there is an entry in this table with (id,merge) where
@ -- merge is the RECORD table entry that the file merged against.
@ -- An id of 0 or <-3 here means the version record itself.  When
@ -- id==(-1) that is a cherrypick merge, id==(-2) that is a
@ -- backout merge and id==(-4) is an integrate merge.
@ --
@
@ CREATE TABLE vmerge(
@   id INTEGER REFERENCES vfile,      -- VFILE entry that has been merged
@   merge INTEGER,                    -- Merged with this record
@   mhash TEXT                        -- SHA1/SHA3 hash for merge object
@ );
@ CREATE UNIQUE INDEX vmergex1 ON vmerge(id,mhash);
@
@ -- The following trigger will prevent older versions of Fossil that
@ -- do not know about the new vmerge.mhash column from updating the
@ -- vmerge table.  This must be done with a trigger, since legacy Fossil
@ -- uses INSERT OR IGNORE to update vmerge, and the OR IGNORE will cause
@ -- a NOT NULL constraint to be silently ignored.
@
@ CREATE TRIGGER vmerge_ck1 AFTER INSERT ON vmerge
@ WHEN new.mhash IS NULL BEGIN
@   SELECT raise(FAIL,
@   'trying to update a newer check-out with an older version of Fossil');
@ END;
@
;

/*
** The following table holds information about forum posts.  It
** is created on-demand whenever the manifest parser encounters
** a forum-post artifact.
*/
static const char zForumSchema[] =
@ CREATE TABLE repository.forumpost(
@   fpid INTEGER PRIMARY KEY,  -- BLOB.rid for the artifact
@   froot INT,                 -- fpid of the thread root
@   fprev INT,                 -- Previous version of this same post
@   firt INT,                  -- This post is in-reply-to
@   fmtime REAL                -- When posted.  Julian day
@ );
@ CREATE INDEX repository.forumthread ON forumpost(froot,fmtime);
;

/* Create the forum-post schema if it does not already exist */
void schema_forum(void){
  if( !db_table_exists("repository","forumpost") ){
    db_multi_exec("%s",zForumSchema/*safe-for-%s*/);
  }
}
