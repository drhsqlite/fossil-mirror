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

set testfiledir [file normalize [file dirname [info script]]]
set testrundir [pwd]
set testdir [file normalize [file dirname $argv0]]
set fossilexe [file normalize [lindex $argv 0]]

if {$tcl_platform(platform) eq "windows" && \
    [string length [file extension $fossilexe]] == 0} {
  append fossilexe .exe
}

set argv [lrange $argv 1 end]

set i [lsearch $argv -keep]
if {$i>=0} {
  set KEEP 1
  set argv [lreplace $argv $i $i]
} else {
  set KEEP 0
}

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

set i [lsearch $argv -strict]
if {$i>=0} {
  set STRICT 1
  set argv [lreplace $argv $i $i]
} else {
  set STRICT 0
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

# write a dict with just enough formatting
# to make it human readable
#
proc protOutDict {dict {pattern *}} {
   set longest [tcl::mathfunc::max 0 {*}[lmap key [dict keys $dict $pattern] {string length $key}]]
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
# test expressions.
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
  foreach a $args {
    lappend cmd $a
  }
  protOut $cmd

  flush stdout
  if {[string length $answer] > 0} {
    protOut $answer
    set prompt_file [file join $::tempPath fossil_prompt_answer]
    write_file $prompt_file $answer\n
    if {$keepNewline} {
      set rc [catch {eval exec -keepnewline $cmd <$prompt_file} result]
    } else {
      set rc [catch {eval exec $cmd <$prompt_file} result]
    }
    file delete $prompt_file
  } else {
    if {$keepNewline} {
      set rc [catch {eval exec -keepnewline $cmd} result]
    } else {
      set rc [catch {eval exec $cmd} result]
    }
  }
  global RESULT CODE
  set CODE $rc
  if {($rc && !$expectError) || (!$rc && $expectError)} {
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

# Returns the list of all supported versionable settings.
#
proc get_versionable_settings {} {
  #
  # TODO: If the list of supported versionable settings in "db.c" is modified,
  #       this list (and procedure) most likely needs to be modified as well.
  #
  set result [list \
      allow-symlinks \
      binary-glob \
      clean-glob \
      crlf-glob \
      crnl-glob \
      dotfiles \
      empty-dirs \
      encoding-glob \
      ignore-glob \
      keep-glob \
      manifest \
      th1-setup \
      th1-uri-regexp]

  fossil test-th-eval "hasfeature tcl"

  if {[normalize_result] eq "1"} {
    lappend result tcl-setup
  }

  return [lsort -dictionary $result]
}

# Returns the list of all supported settings.
#
proc get_all_settings {} {
  #
  # TODO: If the list of supported settings in "db.c" is modified, this list
  #       (and procedure) most likely needs to be modified as well.
  #
  set result [list \
      access-log \
      admin-log \
      allow-symlinks \
      auto-captcha \
      auto-hyperlink \
      auto-shun \
      autosync \
      autosync-tries \
      binary-glob \
      case-sensitive \
      clean-glob \
      clearsign \
      crlf-glob \
      crnl-glob \
      default-perms \
      diff-binary \
      diff-command \
      dont-push \
      dotfiles \
      editor \
      empty-dirs \
      encoding-glob \
      exec-rel-paths \
      gdiff-command \
      gmerge-command \
      hash-digits \
      http-port \
      https-login \
      ignore-glob \
      keep-glob \
      localauth \
      main-branch \
      manifest \
      max-loadavg \
      max-upload \
      mtime-changes \
      pgp-command \
      proxy \
      relative-paths \
      repo-cksum \
      self-register \
      ssh-command \
      ssl-ca-location \
      ssl-identity \
      th1-setup \
      th1-uri-regexp \
      uv-sync \
      web-browser]

  fossil test-th-eval "hasfeature legacyMvRm"

  if {[normalize_result] eq "1"} {
    lappend result mv-rm-files
  }

  fossil test-th-eval "hasfeature tcl"

  if {[normalize_result] eq "1"} {
    lappend result tcl tcl-setup
  }

  fossil test-th-eval "hasfeature th1Docs"

  if {[normalize_result] eq "1"} {
    lappend result th1-docs
  }

  fossil test-th-eval "hasfeature th1Hooks"

  if {[normalize_result] eq "1"} {
    lappend result th1-hooks
  }

  return [lsort -dictionary $result]
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
  if {$na < $nb} then {
    return [string match "$a*" $b]
  }
  return [string match "$b*" $a]
}

# Return a prefix of a uuid, defaulting to 10 chars.
#
proc short_uuid {uuid {len 10}} {
  string range $uuid 0 $len-1
}


proc require_no_open_checkout {} {
  if {[info exists ::env(FOSSIL_TEST_DANGEROUS_IGNORE_OPEN_CHECKOUT)] && \
      $::env(FOSSIL_TEST_DANGEROUS_IGNORE_OPEN_CHECKOUT) eq "YES_DO_IT"} {
    return
  }
  catch {exec $::fossilexe info} res
  if {![regexp {use --repository} $res]} {
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

proc test_cleanup_then_return {} {
  uplevel 1 [list test_cleanup]
  return -code return
}

proc test_cleanup {} {
  if {$::KEEP} {return}; # All cleanup disabled?
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
  set scriptName [file tail [get_script_or_fail]]
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
  if {$::tcl_platform(platform) eq "windows"} {
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
please set TEMP variable in environment, error: $error"
  }
  if {![info exists ::tempSavedPwd]} {set ::tempSavedPwd [pwd]}; cd $repoPath
  if {[string length $filename] > 0} {
    exec $::fossilexe new $filename
    exec $::fossilexe open $filename
    exec $::fossilexe set mtime-changes off
  }
  return $repoPath
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

# This (rarely used) procedure is designed to run a test within the Fossil
# source checkout (e.g. one that does NOT modify any state), while saving
# and restoring the current directory (e.g. one used when running a test
# file outside of the Fossil source checkout).  Please do NOT use this
# procedure unless you are absolutely sure it does not modify the state of
# the repository or source checkout in any way.
#
proc run_in_checkout { script {dir ""} } {
  if {[string length $dir] == 0} {set dir $::testfiledir}
  set savedPwd [pwd]; cd $dir
  set code [catch {
    uplevel 1 $script
  } result]
  cd $savedPwd; unset savedPwd
  return -code $code $result
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

# Returns the value of the specified environment variable -OR- any empty
# string if it does not exist.
#
proc getEnvironmentVariable { name } {
  return [expr {[info exists ::env($name)] ? $::env($name) : ""}]
}

# Returns a usable temporary directory -OR- fails the testing process.
#
proc getTemporaryPath {} {
  #
  # NOTE: Build the list of "temporary directory" environment variables
  #       to check, including all reasonable "cases" of the environment
  #       variable names.
  #
  set names [list]

  #
  # TODO: Add more here, if necessary.
  #
  foreach name [list FOSSIL_TEST_TEMP FOSSIL_TEMP TEMP TMP] {
    lappend names [string toupper $name] [string tolower $name] \
        [string totitle $name]
  }

  #
  # NOTE: Check if we can use any of the environment variables.
  #
  foreach name $names {
    set value [getEnvironmentVariable $name]

    if {[string length $value] > 0} {
      set value [file normalize $value]

      if {[file exists $value] && [file isdirectory $value]} {
        return $value
      }
    }
  }

  #
  # NOTE: On non-Windows systems, fallback to /tmp if it is usable.
  #
  if {$::tcl_platform(platform) ne "windows"} {
    set value /tmp

    if {[file exists $value] && [file isdirectory $value]} {
      return $value
    }
  }

  #
  # NOTE: There must be a usable temporary directory to continue testing.
  #
  error "Cannot find a usable temporary directory, testing halted."
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
  return [file normalize [file join .fossil-settings th1-setup]]
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
  set fileName [getTh1SetupFileName]
  file mkdir [file dirname $fileName]
  return [write_file $fileName $data]
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
proc test {name expr {constraints ""}} {
  global bad_test ignored_test test_count RESULT
  incr test_count
  set knownBug [expr {"knownBug" in $constraints}]
  set r [uplevel 1 [list expr $expr]]
  if {$r} {
    if {$knownBug && !$::STRICT} {
      protOut "test $name OK (knownBug)?"
    } else {
      protOut "test $name OK"
    }
  } else {
    if {$knownBug && !$::STRICT} {
      protOut "test $name FAILED (knownBug)!" 1
      lappend ignored_test $name
    } else {
      protOut "test $name FAILED!" 1
      if {$::QUIET} {protOut "RESULT: $RESULT" 1}
      lappend bad_test $name
      if {$::HALT} exit
    }
  }
}
set bad_test {}
set ignored_test {}

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

# This procedure executes the "fossil server" command.  The return value
# is a list comprised of the new process identifier and the port on which
# the server started.  The varName argument refers to a variable
# where the "stop argument" is to be stored.  This value must eventually be
# passed to the [test_stop_server] procedure.
proc test_start_server { repository {varName ""} } {
  global fossilexe tempPath
  set command [list exec $fossilexe server --localhost]
  if {[string length $varName] > 0} {
    upvar 1 $varName stopArg
  }
  if {$::tcl_platform(platform) eq "windows"} {
    set stopArg [file join [getTemporaryPath] [appendArgs \
        [string trim [clock seconds] -] _ [getSeqNo] .stopper]]
    lappend command --stopper $stopArg
  }
  set outFileName [file join $tempPath [appendArgs \
      fossil_server_ [string trim [clock seconds] -] _ \
      [getSeqNo]]].out
  lappend command $repository >&$outFileName &
  set pid [eval $command]
  if {$::tcl_platform(platform) ne "windows"} {
    set stopArg $pid
  }
  after 1000; # output might not be there yet
  set output [read_file $outFileName]
  if {![regexp {Listening.*TCP port (\d+)} $output dummy port]} {
    puts stdout "Could not detect Fossil server port, using default..."
    set port 8080; # return the default port just in case
  }
  return [list $pid $port $outFileName]
}

# This procedure stops a Fossil server instance that was previously started
# by the [test_start_server] procedure.  The value of the "stop argument"
# will vary by platform as will the exact method used to stop the server.
# The fileName argument is the name of a temporary output file to delete.
proc test_stop_server { stopArg pid fileName } {
  if {$::tcl_platform(platform) eq "windows"} {
    #
    # NOTE: On Windows, the "stop argument" must be the name of a file
    #       that does NOT already exist.
    #
    if {[string length $stopArg] > 0 && \
        ![file exists $stopArg] && \
        [catch {write_file $stopArg [clock seconds]}] == 0} {
      while {1} {
        if {[catch {
          #
          # NOTE: Using the TaskList utility requires Windows XP or
          #       later.
          #
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
    #
    # NOTE: On Unix, the "stop argument" must be an integer identifier
    #       that refers to an existing process.
    #
    if {[regexp {^(?:-)?\d+$} $stopArg] && \
        [catch {exec kill -TERM $stopArg}] == 0} {
      while {1} {
        if {[catch {
          #
          # TODO: Is this portable to all the supported variants of
          #       Unix?  It should be, it's POSIX.
          #
          exec ps -p $pid
        } result] != 0 || ![regexp -- "(?:^$pid| $pid) " $result]} {
          break
        }
        after 1000; # wait a bit...
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

if {$tcl_platform(platform) eq "windows"} {
  set tempPath [string map [list \\ /] $tempPath]
}

if {[catch {
  set tempFile [file join $tempPath temporary.txt]
  write_file $tempFile [clock seconds]; file delete $tempFile
} error] != 0} {
  error "Could not write file \"$tempFile\" in directory \"$tempPath\",\
please set TEMP variable in environment, error: $error"
}

set tempHomePath [file join $tempPath home_[pid]]

if {[catch {
  file mkdir $tempHomePath
} error] != 0} {
  error "Could not make directory \"$tempHomePath\",\
please set TEMP variable in environment, error: $error"
}


protInit $fossilexe
set ::tempKeepHome 1
foreach testfile $argv {
  protOut "***** $testfile ******"
  if { [catch {source $testdir/$testfile.test} testerror testopts] } {
    test test-framework-$testfile 0
    protOut "!!!!! $testfile: $testerror"
    protOutDict $testopts"
  } else {
    test test-framework-$testfile 1
  }
  protOut "***** End of $testfile: [llength $bad_test] errors so far ******"
}
unset ::tempKeepHome; delete_temporary_home
set nErr [llength $bad_test]
if {$nErr>0 || !$::QUIET} {
  protOut "***** Final results: $nErr errors out of $test_count tests" 1
}
if {$nErr>0} {
  protOut "***** Considered failures: $bad_test" 1
}
set nErr [llength $ignored_test]
if {$nErr>0 || !$::QUIET} {
  protOut "***** Ignored results: $nErr ignored errors out of $test_count tests" 1
}
if {$nErr>0} {
  protOut "***** Ignored failures: $ignored_test" 1
}
