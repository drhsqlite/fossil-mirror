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
}
while {1} {
  set n 0
  db transaction immediate {
    set emailid 0
    db eval {SELECT emailid, msg FROM email LIMIT 1} {
      set pipe $PIPE
      if {[regexp {\nFrom:[^\n]*<([^>]+)>} $msg all addr]} {
        append pipe " -f $addr"
      }
      set out [open |$pipe w]
      puts -nonewline $out $msg
      flush $out
      close $out
      incr n
    }
    if {$n>0} {
      db eval {DELETE FROM email WHERE emailid=$emailid}
    }
  }
  if {$n==0} {
    after $POLLING_INTERVAL
  }
}
