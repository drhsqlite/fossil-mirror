#!/usr/bin/tcl
#
# Monitor the database file named by the DBFILE variable
# looking for email messages sent by Fossil.  Forward each
# to /usr/sbin/sendmail.
#
set POLLING_INTERVAL 10000   ;# milliseconds
set DBFILE /home/www/fossil/emailqueue.db
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
  db transaction immediate {
    set n 0
    db eval {SELECT msg FROM email} {
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
      db eval {DELETE FROM email}
    }
  }
  after $POLLING_INTERVAL
}
