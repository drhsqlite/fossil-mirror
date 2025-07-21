#!/usr/bin/tclsh
#
# This script is a testing aid for working on the Relay notification method
# in Fossil.
#
# This script listens for connections on port 25 or probably some other TCP
# port specified by the "--port N" option.  It pretend to be an SMTP server,
# though it does not actually relay any email.  Instead, it just prints the
# SMTP conversation on stdout.
#
# If the "--max N" option is used, then the fake SMTP server shuts down
# with an error after receiving N messages from the client.  This can be
# used to test retry capabilities in the client.
#
# Suggested Test Procedure For Fossil Relay Notifications
#
#    1.  Bring up "fossil ui"
#    2.  Configure notification for relay to localhost:8025
#    3.  Start this script in a separate window.  Something like:
#             tclsh fake-smtpd.tcl -port 8025 -max 100
#    4.  Send test messages using Fossil
#
proc conn_puts {chan txt} {
  puts "S: $txt"
  puts $chan $txt
  flush $chan
}
set mxMsg 100000000
proc connection {chan ip port} {
  global mxMsg
  set nMsg 0
  puts "*** begin connection from $ip:$port ***"
  conn_puts $chan "220 localhost fake-SMTPD"
  set inData 0
  while {1} {
    set line [string trimright [gets $chan]]
    if {$line eq ""} {
      if {[eof $chan]} break
    }
    puts "C: $line"
    incr nMsg
    if {$inData} {
      if {$line eq "."} {
        set inData 0
        conn_puts $chan "250 Ok"
      }
    } elseif {$nMsg>$mxMsg} {
      conn_puts $chan "999 I'm done!"
      break
    } elseif {[string match "HELO *" $line]} {
      conn_puts $chan "250 Ok"
    } elseif {[string match "EHLO *" $line]} {
      conn_puts $chan "250-SIZE 100000"
      conn_puts $chan "250 HELP"
    } elseif {[string match "DATA*" $line]} {
      conn_puts $chan "354 End data with <CR><LF>.<CR><LF>"
      set inData 1
    } elseif {[string match "QUIT*" $line]} {
      conn_puts $chan "221 Bye"
      break
    } else {
      conn_puts $chan "250 Ok"
    }
  }
  puts "*** connection closed ($nMsg messages) ***"
  close $chan
}
set port 25
set argc [llength $argv]
for {set i 0} {$i<$argc-1} {incr i} {
   set arg [lindex $argv $i]
   if {$arg eq "-port" || $arg eq "--port"} {
     incr i
     set port [lindex $argv $i]
   }
   if {$arg eq "-max" || $arg eq "--max"} {
     incr i
     set mxMsg [lindex $argv $i]
   }
}
puts "listening on localhost:$port"
socket -server connection $port
set forever 0
vwait forever
