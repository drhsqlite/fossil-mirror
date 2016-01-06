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
# This is the main test script.  To run a regression test, do this:
#
#     tclsh ../test/tester.tcl ../bld/fossil
#
# Where ../test/tester.tcl is the name of this file and ../bld/fossil
# is the name of the executable to be tested.
#

set testrundir [pwd]
set testdir [file normalize [file dir $argv0]]
set fossilexe [file normalize [lindex $argv 0]]
set argv [lrange $argv 1 end]

set i [lsearch $argv -halt]
if {$i>=0} {
  set HALT 1
  set argv [lreplace $argv $i $i]
} else {
  set HALT 0
}

set i [lsearch $argv -prot]
if {$i>=0} {
  set PROT 1
  set argv [lreplace $argv $i $i]
} else {
  set PROT 0
}

set i [lsearch $argv -verbose]
if {$i>=0} {
  set VERBOSE 1
  set argv [lreplace $argv $i $i]
} else {
  set VERBOSE 0
}

set i [lsearch $argv -quiet]
if {$i>=0} {
  set QUIET 1
  set argv [lreplace $argv $i $i]
} else {
  set QUIET 0
}

if {[llength $argv]==0} {
  foreach f [lsort [glob $testdir/*.test]] {
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
# test expressions.
#
proc fossil_maybe_answer {answer args} {
  global fossilexe
  set cmd $fossilexe
  foreach a $args {
    lappend cmd $a
  }
  protOut $cmd

  flush stdout
  if {[string length $answer] > 0} {
    set prompt_file [file join $::tempPath fossil_prompt_answer]
    write_file $prompt_file $answer\n
    set rc [catch {eval exec $cmd <$prompt_file} result]
    file delete $prompt_file
  } else {
    set rc [catch {eval exec $cmd} result]
  }
  global RESULT CODE
  set CODE $rc
  if {$rc} {
    protOut "ERROR: $result" 1
  } elseif {$::VERBOSE} {
    protOut "RESULT: $result"
  }
  set RESULT $result
}

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
proc write_file_indented {filename txt} {
  write_file $filename [string trim [string map [list "\n  " \n] $txt]]\n
}

# Return true if two files are the same
#
proc same_file {a b} {
  set x [read_file $a]
  regsub -all { +\n} $x \n x
  set y [read_file $b]
  regsub -all { +\n} $y \n y
  return [expr {$x==$y}]
}

# Create and open a new Fossil repository and clean the checkout
#
proc repo_init {{filename ".rep.fossil"}} {
  if {$::env(HOME) ne [pwd]} {
    catch {exec $::fossilexe info} res
    if {![regexp {use --repository} $res]} {
      error "In an open checkout: cannot initialize a new repository here."
    }
    # Fossil will write data on $HOME, running 'fossil new' here.
    # We need not to clutter the $HOME of the test caller.
    #
    set ::env(HOME) [pwd]
  }
  catch {exec $::fossilexe close -f}
  file delete $filename
  exec $::fossilexe new $filename
  exec $::fossilexe open $filename
  exec $::fossilexe clean -f
  exec $::fossilexe set mtime-changes off
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
proc test_status_list {name result expected} {
  set expected [normalize_status_list $expected]
  set result [normalize_status_list $result]
  if {$result eq $expected} {
    test $name 1
  } else {
    protOut "  Expected:\n    [join $expected "\n    "]" 1
    protOut "  Got:\n    [join $result "\n    "]" 1
    test $name 0
  }
}

# Append all arguments into a single value and then returns it.
#
proc appendArgs {args} {
  eval append result $args
}

# Return the name of the versioned settings file containing the TH1
# setup script.
#
proc getTh1SetupFileName {} {
  #
  # NOTE: This uses the "testdir" global variable provided by the
  #       test suite; alternatively, the root of the source tree
  #       could be obtained directly from Fossil.
  #
  return [file normalize [file join [file dirname $::testdir] \
      .fossil-settings th1-setup]]
}

# Return the saved name of the versioned settings file containing
# the TH1 setup script.
#
proc getSavedTh1SetupFileName {} {
  return [appendArgs [getTh1SetupFileName] . [pid]]
}

# Sets the TH1 setup script to the one provided.  Prior to calling
# this, the [saveTh1SetupFile] procedure should be called in order to
# preserve the existing TH1 setup script.  Prior to completing the test,
# the [restoreTh1SetupFile] procedure should be called to restore the
# original TH1 setup script.
#
proc writeTh1SetupFile { data } {
  return [write_file [getTh1SetupFileName] $data]
}

# Saves the TH1 setup script file by renaming it, based on the current
# process ID.
#
proc saveTh1SetupFile {} {
  set oldFileName [getTh1SetupFileName]
  if {[file exists $oldFileName]} {
    set newFileName [getSavedTh1SetupFileName]
    catch {file delete $newFileName}
    file rename $oldFileName $newFileName
  }
}

# Restores the original TH1 setup script file by renaming it back, based
# on the current process ID.
#
proc restoreTh1SetupFile {} {
  set oldFileName [getSavedTh1SetupFileName]
  set newFileName [getTh1SetupFileName]
  if {[file exists $oldFileName]} {
    catch {file delete $newFileName}
    file rename $oldFileName $newFileName
  } else {
    #
    # NOTE: There was no TH1 setup script file, delete the test one.
    #
    file delete $newFileName
  }
}

# Perform a test
#
set test_count 0
proc test {name expr} {
  global bad_test test_count RESULT
  incr test_count
  set r [uplevel 1 [list expr $expr]]
  if {$r} {
    protOut "test $name OK"
  } else {
    protOut "test $name FAILED!" 1
    if {$::QUIET} {protOut "RESULT: $RESULT" 1}
    lappend bad_test $name
    if {$::HALT} exit
  }
}
set bad_test {}

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
# For any given line that mets the block count criteria, the probably
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

# Executes the "fossil http" command.  The entire content of the HTTP request
# is read from the data file name, with [subst] being performed on it prior to
# submission.  Temporary input and output files are created and deleted.  The
# result will be the contents of the temoprary output file.
proc test_fossil_http { repository dataFileName url } {
  set suffix [appendArgs [pid] - [getSeqNo] - [clock seconds] .txt]
  set inFileName [file join $::tempPath [appendArgs test-http-in- $suffix]]
  set outFileName [file join $::tempPath [appendArgs test-http-out- $suffix]]
  set data [subst [read_file $dataFileName]]

  write_file $inFileName $data
  fossil http $inFileName $outFileName 127.0.0.1 $repository --localauth
  set result [expr {[file exists $outFileName] ? [read_file $outFileName] : ""}]

  if {1} {
    catch {file delete $inFileName}
    catch {file delete $outFileName}
  }

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

set tempPath [expr {[info exists env(TEMP)] ? \
    $env(TEMP) : [file dirname [info script]]}]

if {$tcl_platform(platform) eq "windows"} {
  set tempPath [string map [list \\ /] $tempPath]
}

if {[catch {
  write_file [file join $tempPath temporary.txt] [clock seconds]
} error] != 0} {
  error "could not write file to directory \"$tempPath\",\
please set TEMP variable in environment: $error"
}

protInit $fossilexe
foreach testfile $argv {
  set dir [file root [file tail $testfile]]
  file delete -force $dir
  file mkdir $dir
  set origwd [pwd]
  cd $dir
  protOut "***** $testfile ******"
  source $testdir/$testfile.test
  protOut "***** End of $testfile: [llength $bad_test] errors so far ******"
  cd $origwd
}
set nErr [llength $bad_test]
if {$nErr>0 || !$::QUIET} {
  protOut "***** Final result: $nErr errors out of $test_count tests" 1
}
if {$nErr>0} {
  protOut "***** Failures: $bad_test" 1
}
