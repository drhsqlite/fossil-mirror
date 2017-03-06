Web-Page Examples
=================

Here are just a few examples of the many web pages supported
by Fossil.  Follow hyperlinks on the examples below to see many
other examples.
<style>
.exbtn {
  border: 1px solid #000;
  margin: 1ex;
  border-radius: 1ex;
  padding: 0 1ex;
  background-color: #eee;
}
</style>

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?y=ci&n=100'>Example</a>
     100 most recent check-ins.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/finfo?name=src/file.c'>Example</a>
     All changes to the <b>src/file.c</b> source file.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?n=200&uf=0c3c2d086a'>Example</a>
     All check-ins using a particular version of the <b>src/file.c</b>
     source file.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?n=11&y=ci&c=2014-01-01'>Example</a>
     Check-ins proximate to an historical point in time (2014-01-01).

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?n=11&y=ci&c=2014-01-01&v=1'>Example</a>
     The previous example augmented with file changes.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?n=25&y=ci&a=1970-01-01'>Example</a>
     First 25 check-ins after 1970-01-01.  (The first 25 check-ins of
     the project.)

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?n=200&r=svn-import'>Example</a>
     All check-ins of the "svn-import" branch together with check-ins
     that merge with that branch.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?n=200&t=svn-import'>Example</a>
     All check-ins of the "svn-import" branch only.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?n=100&y=ci&ubg'>Example</a>
     100 most recent check-ins color coded by committer rather than by branch.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?from=version-1.27&to=version-1.28'>Example</a>
     All check-ins on the most direct path from
     version-1.27 to version-1.28

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?namechng'>Example</a>
     Show check-ins that contain file name changes

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?u=drh&c=2014-01-08&y=ci'>Example</a>
     Show check-ins circa 2014-01-08 by user "drh".

  *  <a target='_blank' class='exbtn'
     href='$ROOT/timeline?from=version-1.34&to=version-1.35&chng=src/timeline.c,src/doc.c'>Example</a>
     Show all check-ins between version-1.34 and version-1.35 that make
     changes to either of the files src/timeline.c or src/doc.c.

     <big><b>&rarr;</b></big> (Hint:  In the pages above, click the graph nodes
     for any two check-ins or files to see a diff.)
     <big><b>&larr;</b></big>

  *  <a target='_blank' class='exbtn'
     href='$ROOT/search?s=interesting+pages'>Example</a>
     Full-text search for "interesting pages".

  *  <a target='_blank' class='exbtn'
     href='$ROOT/tree?ci=daff9d20621&type=tree'>Example</a>
     All files for a particular check-in (daff9d20621480)

  *  <a target='_blank' class='exbtn'
     href='$ROOT/tree?ci=trunk&type=tree&mtime=1'>Example</a>
     All files for the latest check-in on a branch (trunk) sorted by
     last modification time.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/fileage?name=svn-import'>Example</a>
     Age of all files in the latest checking for branch "svn-import".

  *  <a target='_blank' class='exbtn'
     href='$ROOT/brlist'>Example</a>
     Table of branches.  (Click on column headers to sort.)

  *  <a target='_blank' class='exbtn'
     href='$ROOT/stat'>Example</a>
     Overall repository status.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/reports?type=ci&view=byuser'>Example</a>
     Number of check-ins per committer.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/reports?view=byfile'>Example</a>
     Number of check-ins for each source file.
     (Click on column headers to sort.)

  *  <a target='_blank' class='exbtn'
     href='$ROOT/blame?checkin=5260fbf63287&filename=src/rss.c&limit=-1'>
       Example</a>
     Most recent change to each line of a particular source file in a
     particular check-in.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/taglist'>Example</a>
     List of tags on check-ins.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/bigbloblist'>Example</a>
     The largest objects in the repository.

  *  <a target='_blank' class='exbtn'
     href='$ROOT/hash-collisions'>Example</a>
     Hash prefix collisions

  *  <a target='_blank' class='exbtn'
     href='$ROOT/sitemap'>Example</a>
     The "sitemap" containing links to many other pages
