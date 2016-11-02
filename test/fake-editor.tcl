#
# Copyright (c) 2016 D. Richard Hipp
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the Simplified BSD License (also
# known as the "2-Clause License" or "FreeBSD License".)
#
# This program is distributed in the hope that it will be useful,
# but without any warranty; without even the implied warranty of
# merchantability or fitness for a particular purpose.
#
# Author contact information:
#   drh@hwaci.com
#   http://www.hwaci.com/drh/
#
############################################################################
#
# This is a fake text editor for use by tests.  To customize its behavior,
# set the FAKE_EDITOR_SCRIPT environment variable prior to evaluating this
# script file.  If FAKE_EDITOR_SCRIPT environment variable is not set, the
# default behavior will be used.  The default behavior is to append the
# process identifier and the current time, in seconds, to the file data.
#

if {![info exists argv] || [llength $argv] != 1} {
  error "Usage: \"[info nameofexecutable]\" \"[info script]\" <fileName>"
}

###############################################################################

proc makeBinaryChannel { channel } {
  fconfigure $channel -encoding binary -translation binary
}

proc readFile { fileName } {
  set channel [open $fileName RDONLY]
  makeBinaryChannel $channel
  set result [read $channel]
  close $channel
  return $result
}

proc writeFile { fileName data } {
  set channel [open $fileName {WRONLY CREAT TRUNC}]
  makeBinaryChannel $channel
  puts -nonewline $channel $data
  close $channel
  return ""
}

###############################################################################

set fileName [lindex $argv 0]

if {[file exists $fileName]} then {
  set data [readFile $fileName]
} else {
  set data ""
}

###############################################################################

if {[info exists env(FAKE_EDITOR_SCRIPT)]} {
  #
  # NOTE: If an error is caught while evaluating this script, catch
  #       it and return, which will also skip writing the (possibly
  #       modified) content back to the original file.
  #
  set script $env(FAKE_EDITOR_SCRIPT)
  set code [catch $script error]

  if {$code != 0} then {
    if {[info exists env(FAKE_EDITOR_VERBOSE)]} {
      if {[info exists errorInfo]} {
        puts stdout "ERROR ($code): $errorInfo"
      } else {
        puts stdout "ERROR ($code): $error"
      }
    }

    return
  }
} else {
  #
  # NOTE: The default behavior is to append the process identifier
  #       and the current time, in seconds, to the file data.
  #
  append data " " [pid] " " [clock seconds]
}

###############################################################################

writeFile $fileName $data
