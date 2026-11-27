-- Print the tables of a Fossil repository that are derived from its
-- artifacts, with each record number replaced by the hash or name it
-- stands for and the rows in a fixed order.  Two repositories that hold
-- the same artifacts should print the same text, and so should one
-- repository before and after "fossil rebuild".  integrity.test runs it:
--
--     fossil sql -R REPOSITORY <test/derived.sql
--
SELECT 'event', (SELECT uuid FROM blob WHERE rid=objid), type, mtime,
       user, euser, comment, ecomment, bgcolor, brief
  FROM event ORDER BY 2;
SELECT 'plink', (SELECT uuid FROM blob WHERE rid=pid),
       (SELECT uuid FROM blob WHERE rid=cid), isprim, mtime
  FROM plink ORDER BY 2, 3;
SELECT 'mlink', (SELECT uuid FROM blob WHERE rid=mid),
       (SELECT uuid FROM blob WHERE rid=fid),
       (SELECT uuid FROM blob WHERE rid=pid),
       (SELECT name FROM filename WHERE fnid=mlink.fnid),
       (SELECT name FROM filename WHERE fnid=mlink.pfnid), mperm, isaux
  FROM mlink ORDER BY 2, 5, 3, 4;
SELECT 'tagxref', (SELECT tagname FROM tag WHERE tagid=tagxref.tagid),
       tagtype, (SELECT uuid FROM blob WHERE rid=srcid),
       (SELECT uuid FROM blob WHERE rid=origid), value, mtime,
       (SELECT uuid FROM blob WHERE rid=tagxref.rid)
  FROM tagxref ORDER BY 2, 8, 3;
SELECT 'leaf', (SELECT uuid FROM blob WHERE rid=leaf.rid)
  FROM leaf ORDER BY 2;
SELECT 'filename', name FROM filename ORDER BY 2;
SELECT 'tag', tagname FROM tag
 WHERE tagid IN (SELECT tagid FROM tagxref) ORDER BY 2;
SELECT 'backlink', target, srctype,
       (SELECT uuid FROM blob WHERE rid=srcid), mtime
  FROM backlink ORDER BY 2, 4;
SELECT 'attachment', isLatest, mtime, src, target, filename, comment, user
  FROM attachment ORDER BY 3, 4;
