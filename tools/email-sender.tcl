#!/usr/bin/tcl
#
# Monitor the database file named by the DBFILE variable
# looking for email messages sent by Fossil.  Forward each
# to /usr/sbin/sendmail.
#
set POLLING_INTERVAL 10000   ;# milliseconds
set DBFILE /home/www/data/emailqueue.db
set PIPE "/usr/sbin/sendmail -ti"

package require sqlite3
# puts "SQLite version [sqlite3 -version]"
sqlite3 db $DBFILE
db timeout 5000
catch {db eval {PRAGMA journal_mode=WAL}}
db eval {
  CREATE TABLE IF NOT EXISTS email(
    emailid INTEGER PRIMARY KEY,
    msg TXT
  );
  CREATE TABLE IF NOT EXISTS sentlog(
    mtime INT,
    xto TEXT,
    xfrom TEXT,
    xsubject TEXT,
    xsize INT
  );
}
set ctr 0
while {1} {
  set n 0
  db transaction immediate {
    set emailid 0
    db eval {SELECT emailid, msg FROM email LIMIT 1} {
      set pipe $PIPE
      set to unk
      set subject none
      set size [string length $msg]
      regexp {To:[^\n]*<([^>]+)>} $msg all to
      regexp {\nSubject:[ ]*([^\r\n]+)} $msg all subject
      set subject [string trim $subject]
      if {[regexp {\nFrom:[^\n]*<([^>]+)>} $msg all from]} {
        append pipe " -f $from"
      }
      set out [open |$pipe w]
      puts -nonewline $out $msg
      flush $out
      close $out
      incr n
      incr ctr
    }
    if {$n>0} {
      db eval {DELETE FROM email WHERE emailid=$emailid}
      db eval {INSERT INTO sentlog(mtime,xto,xfrom,xsubject,xsize)
               VALUES(unixepoch(),$to,$from,$subject,$size)}
    }
  }
  if {$n==0} {
    if {$ctr>100} {
      db eval {DELETE FROM sentlog WHERE mtime<unixepoch('now','-30 days')}
      set ctr 0
    }
    after $POLLING_INTERVAL
  }
}
