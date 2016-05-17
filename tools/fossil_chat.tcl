#!/home/drh/bin/tobe
#
# Simple chat client for Tcl/Tk.
#
package require Tk

set SERVERHOST fossil-scm.hwaci.com
# set SERVERHOST 127.0.0.1
#set SERVERHOST 64.5.53.192
set SERVERPORT 8615

# set to correct values if you have to use a proxy
set PROXYHOST {}
set PROXYPORT {}

# Setup the user interface
wm title . Fossil-Chat
wm iconname . [wm title .]

menu .mb -type menubar
if {$tcl_platform(platform)=="unix" && $tcl_platform(os)!="Darwin"} {
  pack .mb -side top -fill x
} else {
  . config -menu .mb
}
.mb add cascade -label File -underline 0 -menu .mb.file
menu .mb.file -tearoff 0
.mb.file add command -label Send -command send_message
.mb.file add command -label {Remove older messages} -command cleanup_record
.mb.file add separator
.mb.file add command -label {Exit} -command exit

frame .who
pack .who -side right -anchor n -fill y
label .who.title -text {Users:      }
pack .who.title -side top -anchor nw
label .who.list -anchor w -justify left -text {}
pack .who.list -side top -anchor nw -expand 1 -padx 5
label .who.time -text {} -justify right
proc update_time {} {
  after 1000 update_time
  set now [clock seconds]
  set time [clock format $now -format %H:%M -gmt 1]
  .who.time config -text "UTC: $time"
}
update_time
pack .who.time -side bottom -anchor sw

frame .input
pack .input -side bottom -fill x
text .input.t -bd 1 -relief sunken -bg white -fg black -width 60 -height 3 \
   -wrap word -yscrollcommand [list .input.sb set] -takefocus 1
bind .input.t <Key-Return> {send_message; break}
pack .input.t -side left -fill both -expand 1
scrollbar .input.sb -orient vertical -command [list .input.t yview]
pack .input.sb -side left -fill y

frame .msg
pack .msg -side top -fill both -expand 1
text .msg.t -bd 1 -relief sunken -bg white -fg black -width 60 -height 20 \
   -wrap word -yscrollcommand [list .msg.sb set] -takefocus 0
bindtags .msg.t [list .msg.t . all]
.msg.t tag config error -foreground red
.msg.t tag config meta -foreground forestgreen
.msg.t tag config norm -foreground black
pack .msg.t -side left -fill both -expand 1
scrollbar .msg.sb -orient vertical -command [list .msg.t yview]
pack .msg.sb -side left -fill y

update

# Send periodic messages to keep the TCP/IP link up
#
proc keep_alive {} {
  global TIMER SOCKET
  catch {after cancel $TIMER}
  set TIMER [after 300000 keep_alive]
  catch {puts $SOCKET noop; flush $SOCKET}
}

# Connect to the server
proc connect {} {
  global SOCKET tcl_platform
  catch {close $SOCKET}
  if {[catch {
      if {$::PROXYHOST ne {}} {
          set SOCKET [socket $::PROXYHOST $::PROXYPORT]
          puts $SOCKET "CONNECT $::SERVERHOST:$::SERVERPORT HTTP/1.1"
          puts $SOCKET "Host:  $::SERVERHOST:$::SERVERPORT"
          puts $SOCKET ""
      } else {
          set SOCKET [socket $::SERVERHOST $::SERVERPORT]
      }
    fconfigure $SOCKET -translation binary -blocking 0
    puts $SOCKET [list login $tcl_platform(user) fact,fuzz]
    flush $SOCKET
    fileevent $SOCKET readable handle_input
    keep_alive
  } errmsg]} {
    if {[tk_messageBox -icon error -type yesno -parent . -message \
           "Unable to connect to server.  $errmsg.\n\nTry again?"]=="yes"} {
      after 100 connect
    }
  }
}
connect

# Send the message text contained in the .input.t widget to the server.
#
proc send_message {} {
  set txt [.input.t get 1.0 end]
  .input.t delete 1.0 end
  regsub -all "\[ \t\n\f\r\]+" [string trim $txt] { } txt
  if {$txt==""} return
  global SOCKET
  puts $SOCKET [list message $txt]
  flush $SOCKET
}

.mb add cascade -label "Transfer" -underline 0 -menu .mb.files
menu .mb.files -tearoff 0
.mb.files add command -label "Send file..." -command send_file
.mb.files add command -label "Delete files" -command delete_files \
    -state disabled
.mb.files add separator

# Encode a string (possibly containing binary and \000 characters) into
# single line of text.
#
proc encode {txt} {
  return [string map [list % %25 + %2b " " + \n %0a \t %09 \000 %00] $txt]
}

# Undo the work of encode.  Convert an encoded string back into its original
# form.
#
proc decode {txt} {
  return [string map [list %00 \000 %09 \t %0a \n + " " %2b + %25 %] $txt]
}

# Delete all of the downloaded files we are currently holding.
#
proc delete_files {} {
  global FILES
  .mb.files delete 3 end
  array unset FILES
  .mb.files entryconfigure 1 -state disabled
}

# Prompt the user to select a file from the disk.  Then send that
# file to all chat participants.
#
proc send_file {} {
  global SOCKET
  set openfile [tk_getOpenFile]
  if {$openfile==""} return
  set f [open $openfile]
  fconfigure $f -translation binary
  set data [read $f]
  close $f
  puts $SOCKET [list file [file tail $openfile] [encode $data]]
  flush $SOCKET
  set time [clock format [clock seconds] -format {%H:%M} -gmt 1]
  .msg.t insert end "\[$time\] sent file [file tail $openfile]\
        - [string length $data] bytes\n" meta
  .msg.t see end
}

# Save the named file to the disk.
#
 proc save_file {filename} {
  global FILES
  set savefile [tk_getSaveFile -initialfile $filename]
  if {$savefile==""} return
  set f [open $savefile w]
  fconfigure $f -translation binary
  puts -nonewline $f [decode $FILES($filename)]
  close $f
}

# Handle a "file" message from the chat server.
#
proc handle_file {from filename data} {
  global FILES
  foreach prior [array names FILES] {
    if {$filename==$prior} break
  }
  if {![info exists prior] || $filename!=$prior} {
    .mb.files add command -label "Save \"$filename\"" \
        -command [list save_file $filename]
  }
  set FILES($filename) $data
  .mb.files entryconfigure 1 -state active
  set time [clock format [clock seconds] -format {%H:%M} -gmt 1]
  .msg.t insert end "\[$time $from\] " meta "File: \"$filename\"\n" norm
  .msg.t see end
}

# Handle input from the server
#
proc handle_input {} {
  global SOCKET
  if {[eof $SOCKET]} {
    disconnect
    return
  }
  set line [gets $SOCKET]
  if {$line==""} return
  set cmd [lindex $line 0]
  if {$cmd=="userlist"} {
    set ulist {}
    foreach u [lrange $line 1 end] {
      append ulist $u\n
    }
    .who.list config -text [string trim $ulist]
  } elseif {$cmd=="message"} {
    set time [clock format [clock seconds] -format {%H:%M} -gmt 1]
    set from [lindex $line 1]
    .msg.t insert end "\[$time $from\] " meta [lindex $line 2]\n norm
    .msg.t see end
    bell
    wm deiconify .
    update
    raise .
  } elseif {$cmd=="noop"} {
    # do nothing
  } elseif {$cmd=="meta"} {
    set now [clock seconds]
    set time [clock format $now -format {%H:%M} -gmt 1]
    .msg.t insert end "\[$time\] [lindex $line 1]\n" meta
    .msg.t see end
  } elseif {$cmd=="file"} {
    if {[info commands handle_file]=="handle_file"} {
      handle_file [lindex $line 1] [lindex $line 2] [lindex $line 3]
    }
  }
}

# Handle a broken socket connection
#
proc disconnect {} {
  global SOCKET
  close $SOCKET
  set q [tk_messageBox -icon error -type yesno -parent . -message \
           "TCP/IP link lost.  Try to reconnet?"]
  if {$q=="yes"} {
    connect
  } else {
    exit
  }
}

# Remove all but the most recent 100 message from the message log
#
proc cleanup_record {} {
  .msg.t delete 1.0 {end -100 lines}
}
