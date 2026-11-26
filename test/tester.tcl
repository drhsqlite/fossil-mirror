#
# Copyright (c) 2006 D. Richard Hipp
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
# This is the main test script.  "make test" runs it from a build
# directory, or run it by hand:
#
#     tclsh ../test/tester.tcl ./fossil
#     tclsh ../test/tester.tcl ./fossil -quiet merge1 update
#
# The first argument is the fossil executable to test, followed by
# the test files, so "update" runs test/update.test.  See test/README.md
# for details of options such as -strict, -halt, -prot, -trials,
# -assurance etc.
#

# Minimum Tcl is 8.6. auto.def should have found us a suitable Tcl
# installation. Care is needed to keep compatible with both 8.6 and 9.0.
package require Tcl 8.6-

set clock_start [clock milliseconds]
set testfiledir [file normalize [file dirname [info script]]]
set testrundir [pwd]
set fossilexe [file normalize [lindex $argv 0]]
set is_windows [expr {$::tcl_platform(platform) eq "windows"}]
set is_cygwin [regexp {^CYGWIN} $::tcl_platform(os)]

if {$::is_windows} {
  if {[string length [file extension $fossilexe]] == 0} {
    append fossilexe .exe
  }
  set outside_fossil_repo [expr ![file exists "$::testfiledir\\..\\_FOSSIL_"]]
} else {
  set outside_fossil_repo [expr ![file exists "$::testfiledir/../.fslckout"]]
}
if {![file isfile $fossilexe]} {
  puts stderr "usage: tclsh [info script] FOSSIL ?OPTIONS? ?TESTFILE ...?"
  exit 1
}

set dirty_ckout 1
if {!$outside_fossil_repo} {
  set savedPwd [pwd]
  cd $testfiledir
  if {![catch {exec $::fossilexe changes --changed} res]} {
    set dirty_ckout [string length $res]
  }
  cd $savedPwd
  unset savedPwd
}

set argv [lrange $argv 1 end]

foreach option {keep halt prot verbose quiet strict} {
  set i [lsearch -exact $argv -$option]
  set [string toupper $option] [expr {$i >= 0}]
  if {$i >= 0} {
    set argv [lreplace $argv $i $i]
  }
}

# Default random-trial count for fuzz loops (delta1, merge2, …). Override
# with -trials N. When the flag is omitted, expensive inputs ("large",
# "repeats") use fewer trials; an explicit -trials applies to all inputs.
set TRIALS 20
set TRIALS_EXPLICIT 0
set i [lsearch -exact $argv -trials]
if {$i >= 0} {
  if {$i + 1 >= [llength $argv]} {
    puts stderr "missing value for -trials"
    exit 1
  }
  set TRIALS [lindex $argv [expr {$i + 1}]]
  if {![string is integer -strict $TRIALS] || $TRIALS < 1} {
    puts stderr "-trials requires a positive integer"
    exit 1
  }
  set TRIALS_EXPLICIT 1
  set argv [lreplace $argv $i [expr {$i + 1}]]
}

# Assurance level from -assurance.
# Unrelated to -trials. smoke ⊂ ci ⊂ stress; default is smoke.
set ASSURANCE smoke
set i [lsearch -exact $argv -assurance]
if {$i >= 0} {
  if {$i + 1 >= [llength $argv]} {
    puts stderr "missing value for -assurance"
    exit 1
  }
  set ASSURANCE [lindex $argv [expr {$i + 1}]]
  if {$ASSURANCE ni {smoke ci stress}} {
    puts stderr "-assurance requires smoke, ci, or stress"
    exit 1
  }
  set argv [lreplace $argv $i [expr {$i + 1}]]
}

if {[llength $argv]==0} {
  foreach f [lsort [glob $testfiledir/*.test]] {
    set base [file root [file tail $f]]
    lappend argv $base
  }
}

# start protocol
#
proc protInit {cmd} {
  if {$::PROT} {
    set out [open [file join $::testrundir prot] w]
    fconfigure $out -translation platform
    puts $out "starting tests with: $cmd"
    close $out
  }
}

# write protocol
#
proc protOut {msg {noQuiet 0}} {
  if {$noQuiet || !$::QUIET} {
    puts stdout $msg
  }
  if {$::PROT} {
    set out [open [file join $::testrundir prot] a]
    fconfigure $out -translation platform
    puts $out $msg
    close $out
  }
}

# print elapsed time
proc printElapsedTime {{prependMsg "Time elapsed: "}} {
  global clock_start
  set clock_end [clock milliseconds]
  set millis [expr {$clock_end - $clock_start}]
  set h [expr {$millis / 3600000}]
  set m [expr {($millis - (3600000 * $h)) / 60000}]
  set s [expr {($millis - (3600000 * $h) - (60000 * $m)) / 1000.0}]
  puts -nonewline $prependMsg
  if {$h>0} {puts -nonewline "$h hours "}
  if {$m>0} {puts -nonewline "$m mins "}
  puts [format "%.3f secs" $s]
}

# write a dict with enough formatting
# to make it human readable
#
proc protOutDict {dict {pattern *}} {
   set longest [tcl::mathfunc::max 0 \
       {*}[lmap key [dict keys $dict $pattern] {string length $key}]]
   dict for {key value} $dict {
      protOut [format "%-${longest}s = %s" $key $value]
   }
}


# Run the Fossil program with the specified arguments.
#
# Consults the VERBOSE global variable to determine if
# diagnostics should be emitted when no error is seen.
# Sets the CODE and RESULT global variables for use in
# test expressions.
#
proc fossil {args} {
  return [uplevel 1 fossil_maybe_answer [list ""] $args]
}

# Run the Fossil program with the specified arguments
# and possibly answer the first prompt, if any.
#
# Consults the VERBOSE global variable to determine if
# diagnostics should be emitted when no error is seen.
# Sets the CODE and RESULT global variables for use in
# test expressions, and the LASTCMD global variable to the
# command, for the report of a failed test.  CODE is 0 if the
# command succeeded and did not write anything to standard error,
# and 1 otherwise.
#
# A command that exits with the wrong status gets an ERROR line.
# Where that status is a known bug, -knownBug among the arguments
# limits the ERROR line to -strict runs.
#
proc fossil_maybe_answer {answer args} {
  global fossilexe
  set cmd $fossilexe
  set expectError 0
  set index [lsearch -exact $args -expectError]
  if {$index != -1} {
    set expectError 1
    set args [lreplace $args $index $index]
  }
  set keepNewline 0
  set index [lsearch -exact $args -keepNewline]
  if {$index != -1} {
    set keepNewline 1
    set args [lreplace $args $index $index]
  }
  set knownBug 0
  set index [lsearch -exact $args -knownBug]
  if {$index != -1} {
    set knownBug 1
    set args [lreplace $args $index $index]
  }
  foreach a $args {
    lappend cmd $a
  }
  protOut [list fossil {*}$args]

  flush stdout
  set execCmd [list eval exec]
  if {$keepNewline} {lappend execCmd -keepnewline}
  lappend execCmd $cmd
  set prompt_file [file join $::tempPath fossil_prompt_answer]
  if {[string length $answer] > 0} {
    protOut $answer
    write_file $prompt_file $answer\n
    lappend execCmd <$prompt_file
  }
  set rc [catch $execCmd result]
  file delete $prompt_file
  set ab(str) {child process exited abnormally}
  set ab(len) [string length $ab(str)]
  set ab(off) [expr {$ab(len) - 1}]
  if {$rc && $expectError && \
      [string range $result end-$ab(off) end] eq $ab(str)} {
    set result [string range $result 0 end-$ab(len)]
  }
  global RESULT CODE LASTCMD
  set LASTCMD [list fossil {*}$args]
  set CODE $rc
  set wrongStatus [expr {($rc != 0) != $expectError}]
  if {$wrongStatus && (!$knownBug || $::STRICT)} {
    protOut "ERROR ($rc): $result" 1
  } elseif {$::VERBOSE} {
    protOut "RESULT ($rc): $result"
  }
  set RESULT $result
}

# Run the Fossil program with the arguments ARGS.  fossil_raw does not
# report a failed command as an error, which the "fossil" procedure does.
# Use it where a test reads the output as bytes, expects text on
# standard error, gives fossil input, or has to tell a crash from a
# failure.  ARGS can begin with either or both of these options:
#
#   -seconds N    Kill fossil after N seconds.  The default is 600.
#   -input TEXT   Give fossil TEXT on standard input.  The default is
#                 an empty standard input.
#
# Sets RESULT to standard output and standard error together, as bytes.
# Sets CODE to the exit status, to "crash" when a signal ended fossil, or
# to "timeout" when fossil was killed at the time limit.  The Tcl event
# loop runs while fossil does, so a server that the test file has made
# with "socket -server" can answer fossil.
#
proc fossil_raw {args} {
  global fossilexe RESULT CODE LASTCMD fossilRawState
  set seconds 600
  set input ""
  while {[lindex $args 0] in {-seconds -input}} {
    set args [lassign $args option value]
    switch -- $option {
      -seconds {set seconds $value}
      -input   {set input $value}
    }
  }
  set LASTCMD [list fossil {*}$args]
  protOut $LASTCMD
  flush stdout

  set RESULT ""
  set fossilRawState running
  set pipe [open "|[list $fossilexe {*}$args << $input 2>@1]" rb]
  fconfigure $pipe -blocking 0
  fileevent $pipe readable [list fossil_raw_read $pipe]
  set timer [after [expr {$seconds*1000}] {set fossilRawState late}]
  vwait fossilRawState
  after cancel $timer

  if {$fossilRawState eq "late"} {
    fossil_raw_kill $pipe
    set CODE timeout
  } else {
    set CODE [fossil_raw_status $pipe]
  }
  if {$::VERBOSE} {
    protOut "RESULT ($CODE): $RESULT"
  }
}

# Adds what fossil has written on PIPE to RESULT, and ends the wait in
# fossil_raw when fossil has closed its output.
#
proc fossil_raw_read {pipe} {
  append ::RESULT [read $pipe]
  if {[eof $pipe]} {set ::fossilRawState done}
}

# Kills the fossil process that writes to PIPE.
#
proc fossil_raw_kill {pipe} {
  foreach process [pid $pipe] {
    if {$::is_windows} {
      catch {exec taskkill /F /PID $process}
    } else {
      catch {exec kill -9 $process}
    }
  }
  catch {close $pipe}
}

# Closes PIPE and returns the exit status of its fossil process, or
# "crash" when a signal ended the process.
#
proc fossil_raw_status {pipe} {
  fconfigure $pipe -blocking 1
  if {![catch {close $pipe} message options]} {
    return 0
  }
  lassign [dict get $options -errorcode] kind process status
  return [expr {$kind eq "CHILDSTATUS" ? $status : "crash"}]
}

# Returns 1 if TEXT holds every string in WANTED, and 0 otherwise.  A
# string in WANTED that begins with "!" must be absent from TEXT.
#
proc holds_strings {text wanted} {
  foreach string $wanted {
    if {[string index $string 0] eq "!"} {
      if {[string first [string range $string 1 end] $text] >= 0} {return 0}
    } elseif {[string first $string $text] < 0} {
      return 0
    }
  }
  return 1
}

# Listens on a port on 127.0.0.1 that the system chooses, and returns
# the port.  Calls HANDLER with each connection and the request on it,
# once the whole request has arrived or the client has closed its side,
# and then closes the connection.  The request is whole when it has its
# header lines and as many bytes after them as Content-Length says.
#
set testListeners [list]
proc test_listen {handler} {
  set server [socket -server [list test_listen_accept $handler] \
                  -myaddr 127.0.0.1 0]
  lappend ::testListeners $server
  return [lindex [fconfigure $server -sockname] 2]
}

proc test_listen_accept {handler chan address port} {
  fconfigure $chan -translation binary -blocking 0
  set ::testListenRequest($chan) ""
  fileevent $chan readable [list test_listen_receive $handler $chan]
}

proc test_listen_receive {handler chan} {
  append ::testListenRequest($chan) [read $chan]
  set request $::testListenRequest($chan)
  set end [string first "\r\n\r\n" $request]
  set length 0
  regexp -nocase -line {^content-length:\s*(\d+)} $request -> length
  if {![eof $chan] &&
      ($end < 0 || [string length $request] < $end + 4 + $length)} {
    return
  }
  unset ::testListenRequest($chan)
  fileevent $chan readable {}
  fconfigure $chan -blocking 1
  catch {{*}$handler $chan $request}
  catch {close $chan}
}

# Closes every server that test_listen has started.
#
proc test_listen_stop {} {
  update
  foreach server $::testListeners {close $server}
  set ::testListeners [list]
}

proc test_standin {name destination} {
  file copy -force [file join $::testfiledir standin $name] $destination
  file attributes $destination -permissions 0755
  return $destination
}

# The cards with which a sync client and a sync server state their
# version, for a test that writes a request or a reply.  A server treats
# a client that does not send the card as older than 2.0, and does not
# send it SHA3 hashes.
#
set XFER_VERSION "22900 20261110 124827"
set CLIENT_VERSION_CARD "pragma client-version $XFER_VERSION\n"
set SERVER_VERSION_CARD "pragma server-version $XFER_VERSION\n"

# Read a file into memory.
#
proc read_file {filename} {
  set in [open $filename r]
  fconfigure $in -translation binary
  set txt [read $in [file size $filename]]
  close $in
  return $txt
}

# Write a file to disk
#
proc write_file {filename txt} {
  set out [open $filename w]
  fconfigure $out -translation binary
  puts -nonewline $out $txt
  close $out
}

# Write CONTENT to the file NAME, add the file, and commit it with the
# comment MSG.  ARGS are further options for "fossil commit".
#
proc commit_file {name content msg args} {
  write_file $name $content
  fossil add $name
  fossil commit -m $msg {*}$args
}
proc write_file_indented {filename txt} {
  write_file $filename [string trim [string map [list "\n  " \n] $txt]]\n
}

# Returns the directory of Fossil source files that the settings come
# from: src/, beside the directory of this script.
#
proc get_source_dir {} {
  return [file join [file dirname $::testfiledir] src]
}

# Returns a dictionary of the settings defined in the source files, with
# the setting name as key and a list {VERSIONABLE CONDITIONAL} as value.
#
# A setting is defined by a "** SETTING: NAME ?FLAGS?" comment, which
# tools/mkindex.c reads to build the table of settings compiled into
# fossil.  The setting is versionable if FLAGS include "versionable".
# As in mkindex, a setting is conditional when the last preprocessor
# line before it starts with "#if", and a line starting with "#e", such
# as "#else" or "#endif", ends that condition.  A setting defined more
# than once, such as case-sensitive, is defined under alternative
# conditions, so it is not conditional.
#
proc get_source_settings {} {
  set count [dict create]
  set result [dict create]
  foreach fileName [glob -nocomplain -directory [get_source_dir] *.c] {
    set inIf 0
    foreach line [split [read_file $fileName] \n] {
      if {[regexp {^#\s*(\S)(\S)?} $line dummy c1 c2]} {
        if {"$c1$c2" eq "if"} {
          set inIf 1
        } elseif {$c1 eq "e"} {
          set inIf 0
        }
      } elseif {[regexp -- {^\*\* SETTING:\s+(\S+)(.*)$} $line \
                    dummy name flags]} {
        set flags [regexp -all -inline {\S+} $flags]
        set versionable [expr {"versionable" in $flags}]
        dict incr count $name
        dict set result $name [list $versionable $inIf]
      }
    }
  }
  if {[dict size $result] == 0} {
    error "No \"** SETTING:\" comments found in [get_source_dir]/*.c"
  }
  dict for {name n} $count {
    if {$n > 1} {
      dict set result $name [list [lindex [dict get $result $name] 0] 0]
    }
  }
  return $result
}

# Returns the settings defined in the source files that this fossil
# binary has: every unconditional setting, and each conditional setting
# that "fossil settings NAME --exact" accepts.  With VERSIONABLE true,
# returns only the versionable ones.
#
proc get_source_settings_in_binary {versionable} {
  set result [list]
  dict for {name info} [get_source_settings] {
    lassign $info isVersionable isConditional
    if {$versionable && !$isVersionable} continue
    if {$isConditional && \
        [catch {exec $::fossilexe settings $name --exact}]} continue
    lappend result $name
  }
  return [lsort -dictionary $result]
}

# Returns the list of all supported versionable settings.
#
proc get_versionable_settings {} {
  return [get_source_settings_in_binary 1]
}

# Returns the list of all supported settings.
#
proc get_all_settings {} {
  return [get_source_settings_in_binary 0]
}

# Returns the hash of the check-in of the open repository whose comment
# is COMMENT.
#
proc checkin_hash {comment} {
  fossil timeline -t ci -n 0 -F {%H %c}
  regexp -line "^(\[0-9a-f\]+) [string map {. \\.} $comment]\$" \
      $::RESULT -> hash
  return $hash
}

# Returns the path of the repository of the open checkout, from
# "fossil info", or an empty string when there is none.
#
proc repository_path {} {
  set repository ""
  regexp -line -- {^repository:   (.*)$} [fossil info] -> repository
  return $repository
}

# Returns the command that runs fake-editor.tcl, for the "editor"
# setting and the VISUAL environment variable.
#
proc fake_editor_command {} {
  return [appendArgs [info nameofexecutable] " " \
      [file join $::testfiledir fake-editor.tcl]]
}

# Returns the SHA1 hash of the file FILENAME, from "fossil sha1sum".
#
proc sha1_file {filename} {
  return [string range [exec $::fossilexe sha1sum $filename] 0 39]
}

# Returns the SHA1 hash of the string TXT.
#
proc sha1_text {txt} {
  set filename [file join $::tempPath sha1_text_[pid]]
  write_file $filename $txt
  set hash [sha1_file $filename]
  file delete $filename
  return $hash
}

# Return true if two files are the same
#
proc same_file {a b} {
  set x [read_file $a]
  regsub -all { +\n} $x \n x
  set y [read_file $b]
  regsub -all { +\n} $y \n y
  if {$x == $y} {
    return 1
  } else {
    if {$::VERBOSE} {
      protOut "NOT_SAME_FILE($a): \{\n$x\n\}"
      protOut "NOT_SAME_FILE($b): \{\n$y\n\}"
    }
    return 0
  }
}

# Return true if two strings refer to the
# same uuid. That is, the shorter is a prefix
# of the longer.
#
proc same_uuid {a b} {
  set na [string length $a]
  set nb [string length $b]
  if {$na == $nb} {
    return [expr {$a eq $b}]
  }
  if {$na < $nb} {
    return [string match "$a*" $b]
  }
  return [string match "$b*" $a]
}


proc require_no_open_checkout {} {
  if {[info exists ::env(FOSSIL_TEST_DANGEROUS_IGNORE_OPEN_CHECKOUT)] && \
      $::env(FOSSIL_TEST_DANGEROUS_IGNORE_OPEN_CHECKOUT) eq "YES_DO_IT"} {
    return
  }
  catch {exec $::fossilexe info} res
  if {[regexp {local-root:} $res]} {
    set projectName <unknown>
    set localRoot <unknown>
    regexp -line -- {^project-name: (.*)$} $res dummy projectName
    set projectName [string trim $projectName]
    regexp -line -- {^local-root: (.*)$} $res dummy localRoot
    set localRoot [string trim $localRoot]
    error "Detected an open checkout of project \"$projectName\",\
rooted at \"$localRoot\", testing halted."
  }
}

proc get_script_or_fail {} {
  set fileName [file normalize [info script]]
  if {[string length $fileName] == 0 || ![file exists $fileName]} {
    error "Failed to obtain the file name of the test being run."
  }
  return $fileName
}

proc robust_delete { path {force ""} } {
  set error "unknown error"
  for {set try 0} {$try < 10} {incr try} {
    if {$force eq "YES_DO_IT"} {
      if {[catch {file delete -force $path} error] == 0} {
        return
      }
    } else {
      if {[catch {file delete $path} error] == 0} {
        return
      }
    }
    after [expr {$try * 100}]
  }
  error "Could not delete \"$path\", error: $error"
}

# Skip the rest of the current test file because of REASON, which the
# final summary reports beside the file name.  Call it from the top
# level of a test file.
#
proc test_skip {reason} {
  test_skip_part $reason
  uplevel 1 [list test_cleanup]
  return -code return
}

# Record that some tests of the current test file are skipped because
# of REASON, which says which tests.  The final summary reports REASON
# beside the file name, and the test file continues.
#
proc test_skip_part {reason} {
  global skipped_tests testfile
  lappend skipped_tests [list $testfile $reason]
  protOut "SKIPPED $testfile: $reason" 1
}

# Record test NAME as failed because of REASON, then stop the current
# test file.  Call it from the top level of a test file when a step
# that the rest of the file depends on has failed.
#
proc test_fail_then_return {name reason} {
  test_report $name 0 "" [list "reason: $reason"]
  uplevel 1 [list test_cleanup]
  return -code return
}

# Deletes the temporary repositories that test_setup made for the test
# file SCRIPTNAME, by default the file being sourced.
proc test_cleanup {{scriptName ""}} {
  if {$::KEEP} {
      # To avoid errors with require_no_open_checkout, cd out of here.
      if {[info exists ::tempSavedPwd]} \
          {cd $::tempSavedPwd; unset ::tempSavedPwd}
      return
  }
  if {![info exists ::tempRepoPath]} {return}
  if {![file exists $::tempRepoPath]} {return}
  if {![file isdirectory $::tempRepoPath]} {return}
  set tempPathEnd [expr {[string length $::tempPath] - 1}]
  if {[string length $::tempPath] == 0 || \
      [string range $::tempRepoPath 0 $tempPathEnd] ne $::tempPath} {
    error "Temporary repository path has wrong parent during cleanup."
  }
  if {[info exists ::tempSavedPwd]} {cd $::tempSavedPwd; unset ::tempSavedPwd}
  # First, attempt to delete the specific temporary repository directories
  # for this test file.
  if {$scriptName eq ""} {
    set scriptName [file tail [get_script_or_fail]]
  }
  foreach repoSeed $::tempRepoSeeds {
    set repoPath [file join $::tempRepoPath $repoSeed $scriptName]
    robust_delete $repoPath YES_DO_IT; # FORCE, arbitrary children.
    set seedPath [file join $::tempRepoPath $repoSeed]
    robust_delete $seedPath; # NO FORCE.
  }
  # Next, attempt to gracefully delete the temporary repository directory
  # for this process.
  robust_delete $::tempRepoPath
  # Finally, attempt to gracefully delete the temporary home directory,
  # unless forbidden by external forces.
  if {![info exists ::tempKeepHome]} {delete_temporary_home}
}

proc delete_temporary_home {} {
  if {$::KEEP} {return}; # All cleanup disabled?
  if {$::is_windows || $::is_cygwin} {
    robust_delete [file join $::tempHomePath _fossil]
  } else {
    robust_delete [file join $::tempHomePath .fossil]
  }
  robust_delete $::tempHomePath
}

proc is_home_elsewhere {} {
  return [expr {[info exists ::env(FOSSIL_HOME)] && \
      $::env(FOSSIL_HOME) eq $::tempHomePath}]
}

proc set_home_to_elsewhere {} {
  #
  # Fossil will write data on $HOME (or $FOSSIL_HOME).  We need not
  # to clutter the real $HOME (or $FOSSIL_HOME) of the test caller.
  #
  if {[is_home_elsewhere]} {return}
  set ::env(FOSSIL_HOME) $::tempHomePath
}

#
# Create and open a new Fossil repository and clean the checkout
#
proc test_setup {{filename ".rep.fossil"}} {
  set_home_to_elsewhere
  if {![info exists ::tempRepoPath]} {
    set ::tempRepoPath [file join $::tempPath repo_[pid]]
  }
  set repoSeed [appendArgs [string trim [clock seconds] -] _ [getSeqNo]]
  lappend ::tempRepoSeeds $repoSeed
  set repoPath [file join \
      $::tempRepoPath $repoSeed [file tail [get_script_or_fail]]]
  if {[catch {
    file mkdir $repoPath
  } error] != 0} {
    error "Could not make directory \"$repoPath\",\
please set TMPDIR or TEMP variable in environment, error: $error"
  }
  if {![info exists ::tempSavedPwd]} {set ::tempSavedPwd [pwd]}; cd $repoPath
  if {[string length $filename] > 0} {
    exec $::fossilexe new $filename
    exec $::fossilexe open $filename
    exec $::fossilexe set mtime-changes off
  }
  return $repoPath
}

# How many random trials to run in a fuzz loop. Pass a true $heavy for
# expensive inputs such as "large" and "repeats": at the default -trials
# scale those use 5 iterations; -trials N uses N for every input kind.
proc test_trials {{heavy 0}} {
  if {$heavy && !$::TRIALS_EXPLICIT} {
    return [expr {min(5, $::TRIALS)}]
  }
  return $::TRIALS
}

# Return the -assurance level: smoke, ci, or stress.
proc test_assurance {} {
  return $::ASSURANCE
}

# After writing PATH A under DIR, return 1 if PATH B names the same
# directory entry on this volume.
proc fs_paths_collide {dir a b} {
  set pa [file join $dir $a]
  set pb [file join $dir $b]
  set parent [file dirname $pa]
  if {![file isdirectory $parent]} {
    file mkdir $parent
  }
  write_file $pa "fs-probe"
  return [file exists $pb]
}

# Classify the temporary volume once: whether ASCII case, NFC vs NFD,
# and sharp-s vs "ss" collapse to one directory entry.  Stores a dict
# on ::fs_type with keys ascii_case, normalization, and sharp_s, each
# 1 when the two spellings are one entry.  Called at harness startup
# for every run, independent of -assurance.
proc fs_classify {} {
  set dir [file join $::tempPath fs_classify_[pid]]
  file mkdir $dir
  set savedEncoding [encoding system]
  encoding system utf-8
  set ascii [fs_paths_collide $dir Readme.txt readme.txt]
  catch {file delete -force {*}[glob -nocomplain -directory $dir *]}
  set norm [fs_paths_collide $dir "\u00e9.txt" "e\u0301.txt"]
  catch {file delete -force {*}[glob -nocomplain -directory $dir *]}
  set sharp [fs_paths_collide $dir "stra\u00dfe.txt" strasse.txt]
  encoding system $savedEncoding
  catch {file delete -force $dir}
  set ::fs_type [dict create \
      ascii_case $ascii \
      normalization $norm \
      sharp_s $sharp]
  protOut "***** filesystem type:\
ascii_case=$ascii normalization=$norm sharp_s=$sharp" 1
}

# This procedure only returns non-zero if the Tcl integration feature was
# enabled at compile-time and is now enabled at runtime.
proc is_tcl_usable_by_fossil {} {
  fossil test-th-eval "hasfeature tcl"
  if {[normalize_result] ne "1"} {return 0}
  fossil test-th-eval "setting tcl"
  if {[normalize_result] eq "1"} {return 1}
  fossil test-th-eval --open-config "setting tcl"
  if {[normalize_result] eq "1"} {return 1}
  return [info exists ::env(TH1_ENABLE_TCL)]
}

# This procedure only returns non-zero if the TH1 hooks feature was enabled
# at compile-time and is now enabled at runtime.
proc are_th1_hooks_usable_by_fossil {} {
  fossil test-th-eval "hasfeature th1Hooks"
  if {[normalize_result] ne "1"} {return 0}
  fossil test-th-eval "setting th1-hooks"
  if {[normalize_result] eq "1"} {return 1}
  fossil test-th-eval --open-config "setting th1-hooks"
  if {[normalize_result] eq "1"} {return 1}
  return [info exists ::env(TH1_ENABLE_HOOKS)]
}

# Run the given command script inside the Fossil source repo checkout.
#
# Callers of this function must ensure two things:
#
# 1. This test run is in fact being done from within a Fossil repo
#    checkout directory.  If you are unsure, test $::outside_fossil_repo
#    or call one of the test_* wrappers below which do that for you.
#
#    As a rule, you should not be calling this function directly!
#
# 2. This test run is being done from a repo checkout directory that
#    doesn't have any uncommitted changes.  If it does, that affects the
#    output of any test based on the output of "fossil status",
#    "... diff", etc., which is likely to make the test appear to fail.
#    If you must call this function directly, test $::dirty_ckout and
#    skip the call if it's true.  The test_* wrappers do this for you.
#
# 3. The test does NOT modify the Fossil checkout tree in any way.
proc run_in_checkout { script {dir ""} } {
  if {[string length $dir] == 0} {set dir $::testfiledir}
  set savedPwd [pwd]; cd $dir
  set code [catch {
    uplevel 1 $script
  } result]
  cd $savedPwd; unset savedPwd
  return -code $code $result
}

# Wrapper for the above function pair.  The tscript parameter is an
# optional post-run test script.  Some callers choose instead to put
# the tests inline with the rscript commands.
#
# Be sure to adhere to the requirements of run_in_checkout!
proc test_block_in_checkout { name rscript {tscript ""} } {
  if {$::outside_fossil_repo || $::dirty_ckout} {
    set ::CODE 0
    set ::RESULT ""
  } else {
    uplevel 1 [list run_in_checkout $rscript]
    if {[string length $tscript] == 0} {
      return ""
    } else {
      set code [catch {
        uplevel 1 $tscript
      } result]
      return -code $code $result
    }
  }
}

# Single-test wrapper for the above.
proc test_in_checkout { name rscript tscript args } {
  return [uplevel 1 [list test_block_in_checkout $name $rscript \
      [list test $name $tscript {*}$args]]]
}

# Normalize file status lists (like those returned by 'fossil changes')
# so they can be compared using simple string comparison
#
proc normalize_status_list {list} {
  set normalized [list]
  set matches [regexp -all -inline -line {^\s*([A-Z_]+:?)\x20+(\S.*)$} $list]
  foreach {_ status file} $matches {
    lappend normalized [list $status [string trim $file]]
  }
  set normalized [lsort -index 1 $normalized]
  return $normalized
}

# Perform a test comparing two status lists
#
proc test_status_list {name result expected {constraints ""}} {
  set expected [normalize_status_list $expected]
  set result [normalize_status_list $result]
  if {$result eq $expected} {
    test $name 1 $constraints
  } else {
    protOut "  Expected:\n    [join $expected "\n    "]" 1
    protOut "  Got:\n    [join $result "\n    "]" 1
    test $name 0 $constraints
  }
}

# Perform a test on the contents of a file
#
proc test_file_contents {name path expected {constraints ""}} {
  if {[file exists $path]} {
    set result [read_file $path]
    set passed [expr {$result eq $expected}]
    if {!$passed} {
      set expectedLines [split $expected "\n"]
      set resultLines [split $result "\n"]
      protOut "  Expected:\n    [join $expectedLines "\n    "]" 1
      protOut "  Got:\n    [join $resultLines "\n    "]" 1
    }
  } else {
    set passed 0
    protOut "  File does not exist: $path" 1
  }
  test $name $passed $constraints
}

# Append all arguments into a single value and then returns it.
#
proc appendArgs {args} {
  eval append result $args
}

# Returns the first directory given by the environment variables
# FOSSIL_TEST_TEMP, FOSSIL_TEMP, TMPDIR, TEMP and TMP, each read in
# upper, lower and title case, or /tmp except on Windows.  Raises an
# error if none of those are directories.
#
proc getTemporaryPath {} {
  set values [list]
  foreach name {FOSSIL_TEST_TEMP FOSSIL_TEMP TMPDIR TEMP TMP} {
    foreach spelling \
        [list $name [string tolower $name] [string totitle $name]] {
      if {[info exists ::env($spelling)] && $::env($spelling) ne ""} {
        lappend values [file normalize $::env($spelling)]
      }
    }
  }
  if {!$::is_windows} {
    lappend values /tmp
  }
  foreach value $values {
    if {[file isdirectory $value]} {
      return $value
    }
  }
  error "Cannot find a usable temporary directory, testing halted."
}

# Return VALUE as lines for the report of a failed test, after LABEL.
# A value on one line is shown in double quotes.  Each line of a value
# on several lines is shown between "|" characters, so that leading and
# trailing white space is visible.
#
proc test_value_lines {label value} {
  if {[string first \n $value] < 0} {
    return [list "$label \"$value\""]
  }
  set lines [list [string trimright $label]]
  foreach line [split $value \n] {
    lappend lines "  |$line|"
  }
  return $lines
}

# Return FILE:LINE of the statement at the top level of the test file
# that led to the current test, outside the procedures that the test
# file defines.
#
proc test_location {} {
  set where ""
  for {set i [info frame]} {$i > 0} {incr i -1} {
    set frame [info frame $i]
    if {![dict exists $frame file]} continue
    set file [dict get $frame file]
    if {![string match *.test $file]} continue
    set where [file tail $file]:[dict get $frame line]
    if {![dict exists $frame proc]} break
  }
  return $where
}

# Count test NAME as passed if OK is true, and as failed otherwise.
# For a failure that is not a known bug, write each line of DETAIL
# after the failure line.  A known bug is reported with the place in
# its test file from test_location.
#
proc test_report {name ok constraints detail} {
  global bad_test ignored_test test_count
  incr test_count
  set knownBug [expr {"knownBug" in $constraints}]
  if {$ok} {
    if {$knownBug && !$::STRICT} {
      protOut "test $name OK (knownBug)? [test_location]"
    } else {
      protOut "test $name OK"
    }
  } else {
    if {$knownBug && !$::STRICT} {
      protOut "test $name FAILED (knownBug)! [test_location]" 1
      lappend ignored_test $name
    } else {
      protOut "test $name FAILED!" 1
      foreach line $detail {
        protOut "  $line" 1
      }
      lappend bad_test $name
      if {$::HALT} {printElapsedTime; exit 1}
    }
  }
}

# Perform a test: test NAME passes if the expression EXPR, evaluated in
# the caller, is true.  A failure is reported with EXPR, the value of
# each variable in EXPR except RESULT, the last fossil command run
# and its output.  The commands in EXPR are not run again.
#
set test_count 0
set LASTCMD ""
proc test {name expr {constraints ""}} {
  set r [uplevel 1 [list expr $expr]]
  set detail [list]
  if {!$r} {
    lappend detail "expression: [string trim $expr]"
    set shown [list]
    foreach ref [regexp -all -inline \
        {\$\{[^\}]*\}|\$(?:::)?[A-Za-z_][A-Za-z0-9_:]*(?:\([^)]*\))?} $expr] {
      if {$ref in $shown ||
          $ref in {$RESULT $::RESULT ${RESULT} ${::RESULT}}} continue
      lappend shown $ref
      if {![catch {uplevel 1 [list subst -nocommands $ref]} value]} {
        lappend detail {*}[test_value_lines "$ref:" $value]
      }
    }
    lappend detail "last command: $::LASTCMD"
    if {[info exists ::RESULT]} {
      lappend detail {*}[test_value_lines "its output:" $::RESULT]
    }
  }
  test_report $name $r $constraints $detail
}

# Test NAME passes if ACTUAL equals EXPECTED.  A failure is reported
# with both values.
#
proc test_eq {name actual expected {constraints ""}} {
  set detail [concat [test_value_lines "expected:" $expected] \
                     [test_value_lines "actual:  " $actual]]
  test_report $name [expr {$actual eq $expected}] $constraints $detail
}

# Test NAME passes if ACTUAL matches the regular expression PATTERN.  A
# failure is reported with the pattern and the actual value.
#
# Tests that ACTUAL equals EXPECTED, as test_eq does, and counts the
# result in the global variable PREFIXPredMet or PREFIXPredUnmet.
#
proc test_predict {prefix name actual expected} {
  if {$actual eq $expected} {
    incr ::${prefix}PredMet
  } else {
    incr ::${prefix}PredUnmet
  }
  test_eq $name $actual $expected
}

proc test_match {name actual pattern {constraints ""}} {
  set detail [concat [list "pattern:  $pattern"] \
                     [test_value_lines "actual:  " $actual]]
  test_report $name [regexp -- $pattern $actual] $constraints $detail
}
set bad_test {}
set ignored_test {}
set skipped_tests {}

# Return a random string N characters long.
#
set vocabulary 01234567890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
append vocabulary "       ()*^!.eeeeeeeeaaaaattiioo   "
set nvocabulary [string length $vocabulary]
proc rand_str {N} {
  global vocabulary nvocabulary
  set out {}
  while {$N>0} {
    incr N -1
    set i [expr {int(rand()*$nvocabulary)}]
    append out [string index $vocabulary $i]
  }
  return $out
}

# Make random changes to a file.
#
# The file is divided into blocks of $blocksize lines each.  The first
# block is number 0.  Changes are only made within blocks where
# the block number divided by $count has a remainder of $index.
#
# For any given line that meets the block count criteria, the probability
# of a change is $prob
#
# Changes do not add or remove newlines
#
proc random_changes {body blocksize count index prob} {
  set out {}
  set blockno 0
  set lineno -1
  foreach line [split $body \n] {
    incr lineno
    if {$lineno==$blocksize} {
      incr blockno
      set lineno 0
    }
    if {$blockno%$count==$index && rand()<$prob} {
      set n [string length $line]
      if {$n>5 && rand()<0.5} {
        # delete part of the line
        set n [expr {int(rand()*$n)}]
        set i [expr {int(rand()*$n)}]
        set k [expr {$i+$n}]
        set line [string range $line 0 $i][string range $line $k end]
      } else {
        # insert something into the line
        set stuff [rand_str [expr {int(rand()*($n-5))-1}]]
        set i [expr {int(rand()*$n)}]
        set ip1 [expr {$i+1}]
        set line [string range $line 0 $i]$stuff[string range $line $ip1 end]
      }
    }
    append out \n$line
  }
  return [string range $out 1 end]
}

# Return $nline lines of text for use as test input.  Each line is
# indented words of one to nine letters, and the letters are the
# $nletter characters that start at $base.  A line has at most
# $maxwords words.
# About one line in eight is empty and about one in eight repeats one
# of the three lines before it.  The same arguments always give the same text.
# The procedure does not call rand(), so it does not change the
# caller's sequence of random numbers.
#
proc fixed_word_lines {nline {base 97} {maxwords 12} {nletter 24}} {
  set seed $nline
  set lines {}
  for {set i 0} {$i<$nline} {incr i} {
    set kind [next_fixed_int seed 8]
    if {$kind==0} {
      lappend lines {}
    } elseif {$kind==1 && $i>=3} {
      lappend lines [lindex $lines end-[next_fixed_int seed 3]]
    } else {
      set words {}
      for {set w [next_fixed_int seed $maxwords]} {$w>=0} {incr w -1} {
        set word {}
        for {set c [next_fixed_int seed 9]} {$c>=0} {incr c -1} {
          append word [format %c [expr {$base+[next_fixed_int seed $nletter]}]]
        }
        lappend words $word
      }
      lappend lines [string repeat "  " [next_fixed_int seed 4]][join $words]
    }
  }
  return [join $lines \n]\n
}

# Return the next integer from 0 to $n-1 in the sequence whose state is
# in the variable $seedVar.
#
proc next_fixed_int {seedVar n} {
  upvar 1 $seedVar seed
  set seed [expr {($seed*1103515245+12345) & 0x7fffffff}]
  return [expr {($seed>>16) % $n}]
}

# Return true if a TCP connection can be made over the loopback interface,
# as the tests that run "fossil server" need.  Listening succeeds even when
# the interface is down, so this procedure also connects.
proc loopback_available {} {
  if {[catch {socket -server {apply {{c a p} {close $c}}} \
                  -myaddr 127.0.0.1 0} srv]} {
    return 0
  }
  set port [lindex [fconfigure $srv -sockname] 2]
  set ok [expr {[catch {socket 127.0.0.1 $port} cli] == 0}]
  if {$ok} { close $cli }
  close $srv
  return $ok
}

# This procedure executes the "fossil server" command.  The return value
# is a list comprised of the new process identifier and the port on which
# the server started.  The varName argument refers to a variable
# where the "stop argument" is to be stored.  This value must eventually be
# passed to the [test_stop_server] procedure.  Any further arguments are
# options for "fossil server".
proc test_start_server { repository {varName ""} args } {
  global fossilexe tempPath
  set command [list exec $fossilexe server --localhost]
  if {[string length $varName] > 0} {
    upvar 1 $varName stopArg
  }
  if {$::is_windows} {
    set stopArg [file join [getTemporaryPath] [appendArgs \
        [string trim [clock seconds] -] _ [getSeqNo] .stopper]]
    lappend command --stopper $stopArg
  }
  set outFileName [file join $tempPath [appendArgs \
      fossil_server_ [string trim [clock seconds] -] _ \
      [getSeqNo]]].out
  lappend command {*}$args $repository >&$outFileName &
  set pid [eval $command]
  if {!$::is_windows} {
    set stopArg $pid
  }
  # The server prints its port in a line such as "Listening for HTTP
  # requests on TCP port localhost:8081, IPv4 and IPv6".  The port is
  # followed by a comma or the end of the line.
  set listening {Listening for [^\n]+ requests on }
  append listening {(?:TCP port )?(?:\S*:)?(\d+)[,\r\n]}
  set port ""
  for {set i 0} {$i < 1000 && $port eq ""} {incr i} {
    after 10
    set output [read_file $outFileName]
    regexp $listening $output -> port
  }
  if {$port eq ""} {
    test_stop_server $stopArg $pid $outFileName
    error "fossil server did not report its port: $output"
  }
  return [list $pid $port $outFileName]
}

# Stops a Fossil server that test_start_server started.  On Windows the
# stop argument is the name of a file that must not exist yet; creating
# it makes the server exit.  Elsewhere it is the server's process ID.
# FILENAME is a temporary output file to delete.
proc test_stop_server { stopArg pid fileName } {
  if {$::is_windows} {
    if {[string length $stopArg] > 0 && \
        ![file exists $stopArg] && \
        [catch {write_file $stopArg [clock seconds]}] == 0} {
      while {1} {
        if {[catch {
          exec tasklist.exe /FI "PID eq $pid"
        } result] != 0 || ![regexp -- " $pid " $result]} {
          break
        }
        after 1000; # wait a bit...
      }
      file delete $stopArg
      if {[string length $fileName] > 0} {
        file delete $fileName
      }
      return true
    }
  } else {
    if {[regexp {^(?:-)?\d+$} $stopArg] && \
        [catch {exec kill -TERM $stopArg}] == 0} {
      while {1} {
        if {[catch {
          exec ps -p $pid
        } result] != 0 || ![regexp -- "(?:^$pid| $pid) " $result]} {
          break
        }
        after 10; # wait a bit...
      }
      if {[string length $fileName] > 0} {
        file delete $fileName
      }
      return true
    }
  }
  return false
}

# Executes the "fossil http" command.  The entire content of the HTTP request
# is read from the data file name, with [subst] being performed on it prior to
# submission.  Temporary input and output files are created and deleted.  The
# result will be the contents of the temporary output file.
proc test_fossil_http { repository dataFileName url } {
  set suffix [appendArgs [pid] - [getSeqNo] - [clock seconds] .txt]
  set inFileName [file join $::tempPath [appendArgs test-http-in- $suffix]]
  set outFileName [file join $::tempPath [appendArgs test-http-out- $suffix]]
  set data [subst [read_file $dataFileName]]

  write_file $inFileName $data

  fossil http --in $inFileName --out $outFileName --ipaddr 127.0.0.1 \
      $repository --localauth --th-trace -expectError

  set result [expr {[file exists $outFileName] ? [read_file $outFileName] : ""}]

  catch {file delete $inFileName}
  catch {file delete $outFileName}

  return $result
}

# obtains and increments a "sequence number" for this test run.
proc getSeqNo {} {
  upvar #0 seqNo seqNo
  if {![info exists seqNo]} {
    set seqNo 0
  }
  return [incr seqNo]
}

# fixup the whitespace in the result to make it easier to compare.
proc normalize_result {} {
  return [string map [list \r\n \n] [string trim $::RESULT]]
}

# fixup the line-endings in the result to make it easier to compare.
proc normalize_result_no_trim {} {
  return [string map [list \r\n \n] $::RESULT]
}

# returns the first line of the normalized result.
proc first_data_line {} {
  return [lindex [split [normalize_result] \n] 0]
}

# returns the second line of the normalized result.
proc second_data_line {} {
  return [lindex [split [normalize_result] \n] 1]
}

# returns the third line of the normalized result.
proc third_data_line {} {
  return [lindex [split [normalize_result] \n] 2]
}

# returns the last line of the normalized result.
proc last_data_line {} {
  return [lindex [split [normalize_result] \n] end]
}

# returns the second to last line of the normalized result.
proc next_to_last_data_line {} {
  return [lindex [split [normalize_result] \n] end-1]
}

# returns the third to last line of the normalized result.
proc third_to_last_data_line {} {
  return [lindex [split [normalize_result] \n] end-2]
}

set tempPath [getTemporaryPath]

if {$is_windows} {
  set tempPath [string map [list \\ /] $tempPath]
}

if {[catch {
  set tempFile [file join $tempPath temporary.txt]
  write_file $tempFile [clock seconds]; file delete $tempFile
} error] != 0} {
  error "Could not write file \"$tempFile\" in directory \"$tempPath\",\
please set TMPDIR or TEMP variable in environment, error: $error"
}

set tempHomePath [file join $tempPath home_[pid]]

# Close stdin to avoid errors on wrapped text for narrow terminals.
# Closing stdin means that terminal detection returns 0 width, in turn
# causing the relevant strings to be printed on a single line.
# However, closing stdin makes file descriptor 0 available on some systems
# and/or TCL implementations, which triggers fossil to complain about opening
# databases using fd 0. Avoid this by opening the script, consuming fd 0.
close stdin
set possibly_fd0 [open [info script] r]

if {[catch {
  file mkdir $tempHomePath
} error] != 0} {
  error "Could not make directory \"$tempHomePath\",\
please set TEMP variable in environment, error: $error"
}


protInit $fossilexe
protOut "***** Testing $fossilexe"
protOut "***** assurance: $::ASSURANCE" 1

# Classify the temp volume before any .test file runs.  Always, for
# every -assurance level and even when unicode-pathnames is not
# among the files under test.
fs_classify

set ::tempKeepHome 1

# Start in tempHomePath to help avoid errors with require_no_open_checkout
set startPwd [pwd]
cd $tempHomePath

# Remove every global variable that is not in the list BEFORE, apart from
# the ones that Tcl makes for itself.  Run after each test file, this
# stops a variable that one test file sets from changing what a later
# test file does.
#
proc unset_new_globals {before} {
  foreach name [info globals] {
    if {$name in $before} continue
    if {[regexp {^(tcl_|auto_|error)} $name]} continue
    uplevel #0 [list unset -nocomplain $name]
  }
}
set seqNo 0

foreach testfile $argv {
  set globalsBefore [info globals]
  lappend globalsBefore globalsBefore
  protOut "***** $testfile ******"
  if { [catch {source $testfiledir/$testfile.test} testerror testopts] } {
    test_report test-framework-$testfile 0 "" [list "error: $testerror"]
    protOut "!!!!! $testfile: $testerror"
    protOutDict $testopts
    # A test file that raises an error stops before its own test_cleanup,
    # and the directories it leaves make the next file's test_cleanup fail.
    if {[catch {test_cleanup $testfile.test} cleanuperror]} {
      protOut "!!!!! $testfile cleanup: $cleanuperror"
    }
  } else {
    test test-framework-$testfile 1
  }
  protOut "***** End of $testfile: [llength $bad_test] errors so far ******"
  unset_new_globals $globalsBefore
}
cd $startPwd
unset ::tempKeepHome; delete_temporary_home

# Clean up the file descriptor
close $possibly_fd0

set nErr [llength $bad_test]
if {$nErr>0 || !$::QUIET} {
  protOut "***** Final results: $nErr errors out of $test_count tests" 1
}
if {$nErr>0} {
  protOut "***** Considered failures: $bad_test" 1
}
set nIgnored [llength $ignored_test]
if {$nIgnored>0 || !$::QUIET} {
  protOut "***** Ignored results: $nIgnored ignored errors out of\
      $test_count tests" 1
}
if {$nIgnored>0} {
  protOut "***** Ignored failures: $ignored_test" 1
}
set nSkipped [llength $skipped_tests]
if {$nSkipped>0} {
  protOut "***** Skipped tests: [lmap s $skipped_tests {lindex $s 0}]" 1
  foreach s $skipped_tests {
    protOut "*****   [lindex $s 0]: [lindex $s 1]" 1
  }
}
if {[llength $bad_test]>0} {
  printElapsedTime
  exit 1
}
printElapsedTime
