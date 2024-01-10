#!/usr/bin/env tclsh

# Script to anonymise test results for comparison.
# - Replaces hashes, pids and similar with fixed strings
# - Rewrites temporary paths to standardise them in output

# Pick up options
set EXTRA 0
set i [lsearch $argv -extra]
while { $i >= 0 } {
  incr EXTRA
  set argv [lreplace $argv $i $i]
  set i [lsearch $argv -extra]
}

# With no arguments or "-", use stdin.
set fname "-"
if { [llength $argv] > 0 } {
  set fname [lindex $argv 0]
}

# Any -options, or an empty first argument, is an error.
if { [llength $argv] > 1 || [regexp {^-.+} $fname] } {
  puts stderr "Error: argument error"
  puts stderr "usage: \[-extra\] [file tail $argv0] ?FILE"
  puts stderr "       Rewrite test output to ease comparison of outputs."
  puts stderr "       With -extra, more output is rewritten as is summaries"
  puts stderr "       to make diff(1) mor euseful across runs and platforms."
  exit 1
} elseif { $fname ne "-" && ! [file exists $fname] } {
  puts stderr "File does not exist: '$fname'"
  exit 1
}

proc common_rewrites { line testname } {
  # Normalise the fossil commands with path as just fossil
  regsub {^(?:[A-Z]:)?/.*?/fossil(?:\.exe)? } $line {fossil } line
  if {[string match "Usage: *" $line]} {
    regsub {^(Usage: )/.*?/fossil(?:\.exe)? } $line {\1fossil } line
    regsub {^(Usage: )[A-Z]:\\.*?\\fossil(?:\.exe)? } $line {\1fossil } line
  }

  # Accept 40 and 64 byte hashes as such
  regsub -all {[[:<:]][0-9a-f]{40}[[:>:]]} $line HASH line
  regsub -all {[[:<:]][0-9a-f]{64}[[:>:]]} $line HASH line

  # Date and time
  regsub -all {[[:<:]]\d{4}-\d\d-\d\d \d\d:\d\d:\d\d[[:>:]]} $line {YYYY-mm-dd HH:MM:SS} line
  if { [lsearch -exact {"amend" "wiki"} $testname] >= 0 } {
    # With embedded T and milliseconds
    regsub { \d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}$} $line { YYYY-mm-ddTHH:MM:SS.NNN} line
  }
  if { [lsearch -exact {"amend" "th1-hooks" "wiki"} $testname] >= 0 } {
    regsub {[[:<:]]\d{4}-\d\d-\d\d[[:>:]]} $line {YYYY-mm-dd} line
  }

  # Timelines have HH:MM:SS [HASH], but don't mess with the zero'ed version.
  regsub {^(?!00:00:00 \[0000000000\])\d\d:\d\d:\d\d \[[0-9a-f]{10}\] } $line {HH:MM:SS [HASH] } line

  # Temporary directories
  regsub -all {(?:[A-Z]:)?/.*?/repo_\d+/\d+_\d+} $line {/TMP/repo_PID/SEC_SEQ} line
  # Home directories only seem present with .fossil or _fossil. Simplify to .fossil.
  regsub -all {(?:[A-Z]:)?/.*?/home_\d+/[._]fossil[[:>:]]} $line {/TMP/home_PID/.fossil} line

  # Users in output
  regsub { (\(user: )[^\)]*\)$} $line { \1USER)} line

  return $line
}

#
# tests/tests_unix/tests_windows contain tuples of
#
# 1. A regular expression to match current line
# 2. A substitution for the current line
#
# Some common patterns applicable to multiples tests are appended below.
#
# The common_rewrites procedure is run first, so use e.g. HASH as needed.
#

dict set tests "amend" {
  {^(fossil artifact) [0-9a-f]{10}}
      {\1 HASH}
  {^U [^ ]+$}
      {U USER}
  {^Z [0-9a-f]{32}$}
      {Z CHECKSUM}
  {^(ed -s \./ci-comment-).*?(\.txt)$}
      {\1UNIQ\2}
  {^(fossil amend HASH -date \{?)\d\d/\d\d/\d{4}}
      {\1dd/mm/YYYY}
  {^(fossil amend HASH -date \{.* )\d{4}(\})$}
      {\1YYYY\2}
  {^(fossil amend HASH -date \{.* )\d\d:}
      {\1HH:}
  {^(fossil amend HASH -date \{)[A-Z][a-z]{2} [A-Z][a-z]{2} [ 0-9]\d }
      {\1Day Mon dd }
  {(\] Edit \[)[0-9a-f]{16}.[0-9a-f]{10}(\]: )}
      {\1HASH1|HASH2\2}
  {(\] Edit \[.*?&dp=)[0-9a-f]{16}}
      {\1dp=HASH}
}

dict set tests "cmdline" {
  {^(fossil test-echo --args) .*/}
      {\1 /TMP/}
  {^(g\.nameOfExe =) \[[^\]]+[/\\]fossil(?:\.exe)?\]$}
      {\1 [/PATH/FOSSILCMD]}
  {^(argv\[0\] =) \[[^\]]+[/\\]fossil(?:\.exe)?\]$}
      {\1 [/PATH/FOSSILCMD]}
}

dict set tests "contains-selector" {
  {^(fossil test-contains-selector) .*?/(compare-selector.css )}
      {\1 /TMP/\2}
}

dict set tests "json" {
  {^(Content-Length) \d+$}
      {\1 LENGTH}
  {^(Cookie: fossil-)[0-9a-f]{16}(\=HASH%2F)\d+\.\d+(%2Fanonymous)$}
      {\1CODE\2NOW\3}
  {^(GET /json/cap\?authToken\=HASH)/\d+\.\d+/(anonymous )}
      {\1/NOW/\2}
  {^(Cookie: fossil-)[0-9a-f]{16}\=[0-9A-F]{50}%2F[0-9a-f]{16}%2F(.*)$}
      {\1CODE=SHA1%2FCODE%2F\2}
  {("authToken":").+?(")}
      {\1AUTHTOKEN\2}
  {("averageArtifactSize":)\d+()}
      {\1SIZE\2}
  {("compiler":").+?(")}
      {\1COMPILER\2}
  {("loginCookieName":").+?(")}
      {\1COOKIE\2}
  {("manifestVersion":"\[)[0-9a-f]{10}(\]")}
      {\1HASH\2}
  {("manifestYear":")\d{4}(")}
      {\1YYYY\2}
  {("name":").+?(")}
      {\1NAME\2}
  {("password":")[0-9a-f]+(")}
      {\1PASSWORD\2}
  {("projectCode":")[0-9a-f]{40}(")}
      {\1HASH\2}
  {("procTimeMs":)\d+}
      {\1MSEC}
  {("procTimeUs":)\d+}
      {\1USEC}
  {("releaseVersion":")\d+\.\d+(")}
      {\1VERSION\2}
  {("releaseVersionNumber":")\d+(")}
      {\1VERSION_NUMBER\2}
  {("timestamp":)\d+}
      {\1SEC}
  {("seed":)\d+()}
      {\1SEED\2}
  {("uid":)\d+()}
      {\1UID\2}
  {("uncompressedArtifactSize":)\d+()}
      {\1SIZE\2}
  {("user":").+?(")}
      {\1USER\2}
  {("version":"YYYY-mm-dd HH:MM:SS )\[[0-9a-f]{10}\] \(\d+\.\d+\.\d+\)"}
      {\1[HASH] (major.minor.patch)}
  {^(Date:) [A-Z][a-z]{2}, \d\d? [A-Z][a-z]{2} \d{4} \d\d:\d\d:\d\d [-+]\d{4}$}
      {\1 Day, dd Mon YYYY HH:MM:SS TZ}
}

dict set tests "merge_renames" {
  {^(size: {7})\d+( bytes)$}
      {\1N\2}
  {^(type: {7}Check-in by ).+?( on YYYY-mm-dd HH:MM:SS)$}
      {\1USER\2}
}

dict set tests "set-manifest" {
  {^(project-code: )[0-9a-f]{40}$}
      {\1HASH} line
}

dict set tests "stash" {
  {^(---|\+\+\+) NUL$}
      {\1 /dev/null}
  {(^    1: \[)[0-9a-f]{14}(\] on YYYY-mm-dd HH:MM:SS)$}
      {\1HASH\2}
  {(^    1: \[)[0-9a-f]{14}(\] from YYYY-mm-dd HH:MM:SS)$}
      {\1HASH\2}
}

dict set tests "th1" {
  {^(fossil test-th-source) (?:[A-Z]:)?.*?/(th1-)\d+([.]th1)$}
      {\1 /TMP/\2PID\3}
  {^(?:[A-Z]:)?[/\\].*?[/\\]fossil(?:\.exe)?$}
      {/PATH/FOSSILCMD}
  {[[:<:]](Content-Security-Policy[[:>:]].*'nonce-)[0-9a-f]{48}(';)}
      {\1NONCE\2}
  {^(<link rel="stylesheet" href="/style.css\?id=)[0-9a-f]+(" type="text/css">)$}
      {\1ID\2}
  {^\d+\.\d{3}(s by)$}
      {N.MMM\1}
  {^(Fossil) \d+\.\d+ \[[0-9a-f]{10}\] (YYYY-mm-dd HH:MM:SS)$}
      {\1 N.M [HASH] \2}
  {^(<script nonce=")[0-9a-f]{48}(">/\* style\.c:)\d+}
      {\1NONCE\2LINENO}
}

dict set tests "th1-docs" {
  {^(check-ins:    ).*}
      {\1COUNT}
  {^(local-root:   ).*}
      {\1/PATH/}
  {^(repository:   ).*}
      {\1/PATH/REPO}
  {^(comment:      ).*}
      {\1/COMMENT/}
  {^(tags:         ).*}
      {\1/TAGS/}
  {(--ipaddr 127\.0\.0\.1) .*? (--localauth)}
      {\1 REPO \2}
}

dict set tests "th1-hooks" {
  {^(?:/[^:]*/fossil|[A-Z]:\\[^:]*\\fossil\.exe): (unknown command:|use \"help\")}
      {fossil: \1}
  {^(project-code: )[0-9a-f]{40}$}
      {\1HASH}
}

dict set tests "th1-tcl" {
  {^(fossil test-th-render --open-config) \{?.*?[/\\]test[/\\]([^/\\]*?)\}?$}
      {\1 /CHECKOUT/test/\2}
  {^(fossil)(?:\.exe)?( 3 \{test-th-render --open-config )(?:\{[A-Z]:)?[/\\].*?[/\\]test[/\\](th1-tcl9.txt\})\}?$}
      {\1\2/CHECKOUT/test/\3}
  {^\d{10}$}
      {SEC}
}

dict set tests "unversioned" {
  {^(fossil user new uvtester.*) \d+$}
      {\1 PASSWORD}
  {^(fossil .*http://uvtester:)\d+(@localhost:)\d+}
      {\1PASSWORD\2PORT}
  {^(Pull from http://uvtester@localhost:)\d+}
      {\1PORT}
  {^(ERROR \(1\): Usage:) .*?[/\\]fossil(?:\.exe)? (unversioned)}
      {\1 /PATH/fossil \2}
  {^(Started Fossil server, pid \")\d+(\", port \")\d+}
      {\1PID\2PORT}
  {^(Now in client directory \")(?:[A-Z]:)?/.*?/uvtest_\d+_\d+\"}
      {\1/TMP/uvtest_SEC_SEQ}
  {^(Stopped Fossil server, pid \")\d+(\", using argument \")(?:\d+|[^\"]*\.stopper)(\")}
      {\1PID\2PID_OR_SCRIPT\3}
  {^(This is unversioned file #4\.) \d+ \d+}
      {\1 PID SEC}
  {^(This is unversioned file #4\. PID SEC) \d+ \d+}
      {\1 PID SEC}
  {^[0-9a-f]{12}( YYYY-mm-dd HH:MM:SS *)(\d+)( *)\2( unversioned4.txt)$}
      {HASH        \1SZ\3SZ\4}
  {^[0-9a-f]{40}$}
      {\1HASH}
  {^((?:Clone|Pull)? done, wire bytes sent: )\d+(  received: )\d+(  remote: )(?:127\.0.0\.1|::1)$}
      {\1SENT\2RECV\3LOCALIP}
  {^(project-id: )[0-9a-f]{40}$}
      {\1HASH}
  {^(server-id:  )[0-9a-f]{40}$}
      {\1HASH}
  {^(admin-user: uvtester \(password is ").*("\))$}
      {\1PASSWORD\2}
  {^(repository:   ).*?/uvtest_\d+_\d+/(uvrepo.fossil)$}
      {\1/TMP/uvtest_SEC_SEQ/\2}
  {^(local-root:   ).*?/uvtest_\d+_\d+/$}
      {\1/TMP/uvtest_SEC_SEQ/}
  {^(project-code: )[0-9a-f]{40}$}
      {\1HASH}
}

dict set tests "utf" {
  {^(fossil test-looks-like-utf) (?:[A-Z]:)?/.*?/([^/\\]*?)\}?$}
      {\1 /TMP/test/\2}
  {^(File ")(?:[A-Z]:)?/.*?/(utf-check-\d+-\d+-\d+-\d+.jnk" has \d+ bytes\.)$}
      {\1/TMP/\2}
}

dict set tests "wiki" {
  {^(fossil (?:attachment|wiki) .*--technote )[0-9a-f]{21}$}
      {\1HASH}
  {^(fossil (?:attachment|wiki) .* (?:a13|f15|fa) --technote )[0-9a-f]+$}
      {\1ID}
  {^[0-9a-f]{40}( YYYY-mm-dd HH:MM:SS)}
      {HASH\1}
  {(\] Add attachment \[/artifact/)[0-9a-f]{16}(|)}
      {\1HASH\2}
  { (to tech note \[/technote/)[0-9a-f]{16}\|[0-9a-f]{10}(\] \(user:)}
      {\1HASH1|HASH2\2}
  {^(ambiguous tech note id: )[0-9a-f]+$}
      {\1ID}
  {^(Attached fa to tech note )[0-9a-f]{21}(?:[0-9a-f]{19})?\.$}
      {\1HASH.}
  {^(Date:) [A-Z][a-z]{2}, \d\d? [A-Z][a-z]{2} \d{4} \d\d:\d\d:\d\d [-+]\d{4}$}
      {\1 Day, dd Mon YYYY HH:MM:SS TZ}
  {(Content-Security-Policy.*'nonce-)[0-9a-f]{48}(';)}
      {\1NONCE\2}
  {^(<link rel="stylesheet" href="/style.css\?id=)[0-9a-f]+(" type="text/css">)$}
      {\1ID\2}
  {^(added by )[^ ]*( on)$}
      {\1USER\2}
  {^(<script nonce=['\"])[0-9a-f]{48}(['\"]>/\* [a-z]+\.c:)\d+}
      {\1NONCE\2LINENO}
  {^(<script nonce=['\"])[0-9a-f]{48}(['\"]>)$}
      {\1NONCE\2}
  {^(projectCode: ")[0-9a-f]{40}(",)$}
      {\1HASH\2}
  {^\d+\.\d+(s by)$}
      {N.SUB\1}
  {^(window\.fossil.version = ")\d+\.\d+ \[[0-9a-f]{10}\] (YYYY-mm-dd HH:MM:SS(?: UTC";)?)$}
      {\1N.M [HASH] \2}
  {^(Fossil) \d+\.\d+ \[[0-9a-f]{10}\]( YYYY-mm-dd HH:MM:SS)$}
      {\1 N.M [HASH]\2}
  {^(type:       Wiki-edit by ).+?( on YYYY-mm-dd HH:MM:SS)$$}
      {\1USER\2}
  {^(size:       )\d+( bytes)$}
      {\1N\2}
  {^U [^ ]+$}
      {U USER}
  {^Z [0-9a-f]{32}$}
      {Z CHECKSUM}
}

#
# Some pattersn are used in multiple groups
#

set testnames {"th1" "th1-docs" "th1-hooks"}
set pat {^((?:ERROR \(1\): )?/[*]{5} Subprocess) \d+ (exit)}
set sub {\1 PID \2}
foreach testname $testnames {
  dict lappend tests $testname $pat $sub
}

set testnames {"th1-docs" "th1-hooks"}
set pat {(?:[A-Z]:)?/.*?/(test-http-(?:in|out))-\d+-\d+-\d+(\.txt)}
set sub {/TMP/\1-PID-SEQ-SEC\2}
foreach testname $testnames {
  dict lappend tests $testname $pat $sub
}

set testnames {"json" "th1" "wiki"}
set pat {^(Content-Length:) \d+$}
set sub {\1 LENGTH}
foreach testname $testnames {
  dict lappend tests $testname $pat $sub
}

set testnames {"th1" "wiki"}
set pat {^\d+\.\d+(s by)$}
set sub {N.SUB\1}
foreach testname $testnames {
  dict lappend tests $testname $pat $sub
}

#
# Main
#

if { $fname eq "-" } {
  set fd stdin
} else {
  set fd [open $fname r]
}

# Platforms we detect
set UNKOWN_PLATFORM 0
set UNIX 1
set WINDOWS 2
set CYGWIN 3

# One specific wiki test creates repetitive output of varying length
set wiki_f13_cmd1 "fossil wiki create {timestamp of 2399999} f13 --technote 2399999"
set wiki_f13_cmd2 "fossil wiki list --technote --show-technote-ids"
set wiki_f13_cmd3 "fossil wiki export a13 --technote ID"
set collecting_f3 0
set collecting_f3_verbose 0

# Collected lines for summaries in --extra mode
set amend_ed_lines [list]
set amend_ed_failed 0
set symlinks_lines [list]
set symlinks_failed 0
set test_simplify_name_lines [list]
set test_simplify_name_failed 0

# State information s we progress
set check_json_empty_line 0
set lineno 0
set platform $UNKOWN_PLATFORM
set prev_line ""
set testname ""

while { [gets $fd line] >= 0 } {   
  incr lineno

  if { $lineno == 1 } {
    if { [string index $line 0] in {"\UFFEF" "\UFEFF"} } {
      set line [string range $line 1 end]
    }
  }

  # Remove RESULT status while matching (inserted again in output).
  # If collecting lines of output, include $result_prefix as needed.
  regexp {^(RESULT \([01]\): )?(.*)} $line match result_prefix line

  if { [regsub {^\*{5} ([^ ]+) \*{6}$} $line {\1} new_testname] } {
    # Pick up test name for special handling below
    set testname "$new_testname"
  } elseif { [regexp {^\*{5} End of } $line] } {
    # Test done.  Handle --extra before resetting.
    if { $EXTRA } {
      if { $testname eq "symlinks" } {
        if { $symlinks_failed } {
          foreach l $symlinks_lines {
            puts "$l"
          }
        } else {
          puts "All symlinks tests OK (not run on Windows)"
        }
      }
      regsub {(: )\d+( errors so far)} $line {\1N\2} line
    }
    set testname ""
  } elseif { $testname ne "" } {
    if { $platform == $UNKOWN_PLATFORM } {
      if { [regexp {^[A-Z]:/.*?/fossil\.exe } $line] } {
        set platform $WINDOWS
      } elseif { [regexp {^/.*?/fossil\.exe } $line] } {
        # No drive, but still .exe - must be CYGWIN
        set platform $CYGWIN
      } elseif { [regexp {^/.*?/fossil } $line] } {
        set platform $UNIX
      }
    }

    # Do common and per testname rewrites
    set line [common_rewrites $line $testname]
    if { [dict exists $tests $testname] } {
      foreach {pat sub} [dict get $tests $testname] {
        regsub $pat $line $sub line
      }
    }

    # On Windows, HTTP headers may get printed with an extra newline
    if { $testname eq "json" } {
      if { $check_json_empty_line == 1 } {
        if { "$result_prefix$line" eq "" } {
          set check_json_empty_line 2
          continue
        }
        set check_json_empty_line 0
      } elseif { [regexp {^(?:$|GET |POST |[A-Z][A-Za-z]*(?:-[A-Z][A-Za-z]*)*: )} $line] } {
        set check_json_empty_line 1
      } else {
        if { $check_json_empty_line == 2 } {
          # The empty line we skipped was meant to be followed by a new
          # HTTP header or empty line, but it was not.
          puts ""
        }
        set check_json_empty_line 0
      }
    }

    # Summarise repetitive output of varying length for f13 in wiki test
    if { $testname eq "wiki" } {
      if { $collecting_f3 == 2 } {
        if { $collecting_f3_verbose == 1 && [regexp {^HASH } $line] } {
          incr collecting_f3_verbose
        } elseif { $line eq $wiki_f13_cmd3 } {
          incr collecting_f3
          puts "\[...\]"
        } else {
          continue
        }
      } elseif { $collecting_f3 == 1 } {
        if { $line eq $wiki_f13_cmd2 } {
          incr collecting_f3
        } elseif { $collecting_f3_verbose == 0 } {
          incr collecting_f3_verbose
        }
      } elseif { $line eq $wiki_f13_cmd1 } {
        incr collecting_f3
      }
    }

    if { $EXTRA } {
      if { $line eq "ERROR (0): " && $platform == $WINDOWS } {
        if { [string match "fossil http --in *" $prev_line] } {
          continue
        }
      }
      if { $testname eq "amend" } {
        # The amend-comment-5.N tests are not run on Windows
        if { $line eq "fossil amend {} -close" } {
          if { $amend_ed_failed } {
            foreach l $amend_ed_lines {
              puts "$l"
            }
          } else {
            puts "All amend tests based on ed -s OK (not run on Windows)"
          }
          set amend_ed_lines [list]
        } elseif { [llength $amend_ed_lines] } {
          if { [regexp {^test amend-comment-5\.\d+ (.*)} $line match status] } {
            lappend amend_ed_lines "$result_prefix$line"
            if { $status ne "OK" } {
              incr amend_ed_failed
            }
            continue
          } elseif { [string range $line 0 4] eq "test " } {
            # Handle change in tests by simply emitting what we got
            foreach l $amend_ed_lines {
              puts "$l"
            }
            set amend_ed_lines [list]
          } else {
            lappend amend_ed_lines "$result_prefix$line"
            continue
          }
        } elseif { $line eq "fossil settings editor {ed -s}" } {
          lappend amend_ed_lines "$result_prefix$line"
          continue
        }
      } elseif { $testname eq "cmdline" } {
        if { [regexp {^(fossil test-echo) (.*)} $line match test args] } {
          if { ($platform == $UNIX && $args in {"*" "*.*"})
               || ($platform == $WINDOWS && $args eq "--args /TMP/fossil-cmd-line-101.txt")
               || ($platform == $CYGWIN && $args in {"*" "*.*"}) } {
            set line "$test ARG_FOR_PLATFORM"
          }
        }
      } elseif { $testname eq "commit-warning" } {
        if { [regexp {^(micro-smile|pale facepalm) .*} $line match desc] } {
          set line "$desc PLATFORM_SPECIFIC_BYTES"
        }
      } elseif { $testname eq "file1" } {
        # test-simplify-name with question marks is specific to Windows
        # They all immediately preceed "fossil test-relative-name --chdir . ."
        if { $line eq "fossil test-relative-name --chdir . ." } {
          if { $test_simplify_name_failed } {
            foreach l $test_simplify_name_lines {
              puts "$l"
            }
          } else {
            puts "ALL Windows specific test-relative-name tests OK (if on Windows)"
          }
          set test_simplify_name_lines [list]
        } elseif { [regexp {^fossil test-simplify-name .*([/\\])\?\1} $line] } {
          lappend test_simplify_name_lines $line
          continue
        } elseif { [llength $test_simplify_name_lines] } {
          if { [regexp {^test simplify-name-\d+ (.*)} $line match status] } {
            if { $status ne "OK" } {
              incr test_simplify_name_failed
            }
          }
          lappend test_simplify_name_lines "$result_prefix$line"
          continue
        }
      } elseif { $testname eq "settings-repo" } {
        if { [regexp {^fossil test-th-eval (?:--open-config )?\{setting case-sensitive\}$} $prev_line] } {
          if { ($platform == $UNIX && $line eq "on")
               || ($platform == $WINDOWS && $line eq "off")
               || ($platform == $CYGWIN && $line eq "off")
               } {
            set line "EXPECTED_FOR_PLATFORM"
          }
        }
      } elseif { $testname eq "symlinks" } {
        # Collect all lines and post-process at the end
        lappend symlinks_lines "$result_prefix$line"
        if { [regexp {^test symlinks-[^ ]* (.*)} $line match status] } {
          if { $status ne "OK" } {
            #TODO: incr symlinks_failed
          }
        }
        continue
      } elseif { $testname in {"th1" "th1-docs" "th1-hooks"} } {
        # Special case that spans a couple of tests
        # "Subprocess PID exit(0)" is sent on stderr on Unix. On Windows, there is no output
        if { [regexp {^(ERROR \(1\): )?/\*{5} Subprocess PID exit\(0\) \*{5}/$} $line match prefix] } {
          if { $prefix eq "" } {
            continue
          } elseif { $prefix eq "ERROR (1): " } {
            set line "RESULT (0): "
          }
        } elseif { $testname eq "th1" } {
          if { [regexp {^fossil test-th-eval --vfs ([^ ]+) \{globalState vfs\}$} $line match vfs] } {
            if { ($platform == $UNIX && $vfs == "unix-dotfile")
                 || ($platform == $WINDOWS && $vfs == "win32-longpath")
                 || ($platform == $CYGWIN && $vfs == "win32-longpath") } {
              regsub $vfs $line {EXEPECTED_VFS} line
            }
          } elseif { $prev_line eq "fossil test-th-eval --vfs EXEPECTED_VFS {globalState vfs}" } {
            # Replace $vfs from previous line
            regsub "^$vfs\$" $line {EXEPECTED_VFS} line
          } elseif { $prev_line eq "fossil test-th-eval {set tcl_platform(platform)}" } {
            if { $platform == $UNIX } {
              regsub {^unix$} $line {EXPECTED_PLATFORM} line
            } elseif { $platform == $WINDOWS } {
              regsub {^windows$} $line {EXPECTED_PLATFORM} line
            } elseif { $platform == $CYGWIN } {
              regsub {^unix$} $line {EXPECTED_PLATFORM} line
            }
          } elseif { [string match "fossil test-th-eval --th-trace *" $prev_line] } {
            if { ($result_prefix eq "RESULT (1): " && $line eq "")
                 || ($result_prefix eq "" && $line eq "ERROR (0): ") } {
              set result_prefix ""
              set line "RESULT (0): / ERROR (1): "
            }
          }
        } elseif { $testname eq "th1-docs" } {
          # In th1-docs, the fossil check-out is exposed in various states.
          regsub {(^project-code:) CE59BB9F186226D80E49D1FA2DB29F935CCA0333} $line {\1 HASH} line
          if { [regexp {^merged-from:  HASH YYYY-mm-dd HH:MM:SS UTC$} $line] } {
            continue
          }
        }
      }
    }
  } elseif { $EXTRA } {
    # Fix up summaries to be generic and easy to diff(1)
    if { [regsub {(^\*{5} (Final|Ignored) results: )\d+} $line {\1N} line] } {
      regsub {\d+} $line {N} line
    } elseif { [regexp {^(\*{5} (?:Considered failure|Ignored failure|Skipped test))s: (.*)} $line match desc vals] } {
      if { $vals ne ""} {
        foreach val [split $vals " "] {
          puts "$desc: $val"
        }
        continue
      }
    }
  }

  # Not exactly correct if we continue'd, but OK for the purpose
  set prev_line "$result_prefix$line"
  puts "$prev_line"
}
