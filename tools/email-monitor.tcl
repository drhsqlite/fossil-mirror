#!/usr/bin/tcl
#
# Monitor the database file named on the command line for
# incoming email messages.  Print the "To:" line of each
# email on standard output as it is received.
#
# It should be relatively easy to modify this scribe to actually
# deliver the emails to a real email transfer agent such as
# Postfix.
#
# For long-term use, set the polling interval to something
# greater than the default 100 milliseconds.  Polling once
# every 10 seconds is probably sufficient.
#
set POLLING_INTERVAL 100   ;# milliseconds

set dbfile [lindex $argv 0]
if {[llength $argv]!=1} {
  puts stderr "Usage: $argv0 DBFILE"
  exit 1
}
package require sqlite3
puts "SQLite version [sqlite3 -version]"
sqlite3 db $dbfile
db timeout 2000
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
      set email ???
      regexp {To: \S*} $msg to
      puts "$to ([string length $msg] bytes)"
      incr n
    }
    if {$n>0} {
      db eval {DELETE FROM email}
    }
    # Hold the write lock a little longer in order to exercise
    # the SQLITE_BUSY handling logic on the writing inside of
    # Fossil.  Probably comment-out this line for production use.
    after 100
  }
  after $POLLING_INTERVAL
}
