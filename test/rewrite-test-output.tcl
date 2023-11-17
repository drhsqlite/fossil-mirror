#!/usr/bin/env tclsh

# Script to anonymise test results for comparison.
# - Replaces hashes, pids and similar with fixed strings
# - Rewrites temporary paths to standardise them in output

# With no arguments or "-", use stdin.
set fname "-"
if { $argc > 0 } {
  set fname [lindex $argv 0]
}

# Any -options, or an empty first argument, is an error.
if { $argc > 1 || [regexp {^-.+} $fname] } {
  puts stderr "usage: [file tail $argv0] ?FILE"
  puts stderr "       Rewrite test output to ease comparison of outputs."
  exit 1
} elseif { $fname ne "-" && ! [file exists $fname] } {
  puts stderr "File does not exist: '$fname'"
  exit 1
} else {
  if { $fname eq "-" } {
    set fd stdin
  } else {
    set fd [open $fname r]
  }

  set first_f13_line "fossil wiki create {timestamp of 2399999} f13 --technote 2399999"
  set collecting_f3 0

  set testname ""
  while { [gets $fd line] >= 0 } {   
    if { [regsub {^\*{5} ([^ ]+) \*{6}$} $line {\1} new_testname] } {
      # Pick up test naeme for special handling below
      set testname "$new_testname"
    } elseif { [regexp {^\*{5} End of } $line] } {
      # Test done
      set testname ""
    } elseif { [regsub {^/.*?/(fossil )} $line {\1} line] } {
      # Handle all fossil commands in one place

      # We get varying amounts of lines for wiki "f13"
      if { $collecting_f3 > 2 } {
        # Already collected
      } elseif { $collecting_f3 > 0 } {
        if { $collecting_f3 == 1 } {
          # Print a continuation line here.
          # The line is printed, but works as end line then.
          puts "\[...\]"
        } elseif [regexp {^fossil wiki (?:create .* f13|list --technote --show-technote-ids)} $line] {
          continue
        }
        incr collecting_f3
      } elseif { $first_f13_line eq $line } {
        incr collecting_f3
      }

      # Hashes
      regsub {^(fossil .*) [0-9a-f]{40}($| )} $line {\1 HASH\2} line
      regsub {^(fossil artifact) [0-9a-f]{64}} $line {\1 HASH} line
      regsub {^(fossil artifact) [0-9a-f]{10}} $line {\1 HASH} line
      regsub {^(fossil (?:attachment|wiki) .*--technote )[0-9a-f]{21}$} $line {\1HASH} line
      regsub {^(fossil json artifact) [0-9a-f]{64}} $line {\1 HASH} line
      regsub {^(fossil json wiki diff) [0-9a-f]{64} [0-9a-f]{64}} $line {\1 HASH1 HASH2} line

      # PIDs, Sequence numbers and seconds
      regsub {^(fossil test-th-source .*/th1-)\d+([.]th1)$} $line {\1PID\2} line
      if { [regexp {^fossil http --in } $line] } {
        regsub -all {/(test-http-(?:in|out))-\d+-\d+-\d+(\.txt)} $line {/\1-PID-SEQ-SEC\2} line
      }

      # Tech notes ids
      regsub {^(fossil (?:attachment|wiki) .* (?:a13|f15|fa) --technote )[0-9a-f]+$} $line {\1ID} line

      # Special technote timestamp (mostly they are fixed)
      regsub {^(fossil attachment add f11 --technote) [{]\d{4}-\d\d-\d\d \d\d:\d\d:\d\d[}]} $line {\1 {YYYY-mm-dd HH:MM:SS}} line

      # Unversioned test has passowrds and ports
      regsub {^(fossil user new uvtester.*) \d+$} $line {\1 PASSWORD} line
      regsub {^(fossil .*http://uvtester:)\d+(@localhost:)\d+} $line {\1PASSWORD\2PORT} line
    } elseif { $testname eq "json" } {
      regsub {^(Content-Length) \d+$} $line {\1 LENGTH} line
      regsub {^(   \"authToken\":\")[0-9a-f]{40}/\d+\.\d+/(anonymous\")$} $line {\1SHA1/NOW/\2} line
      regsub {^(Cookie: fossil-)[0-9a-f]{16}\=[0-9a-f]{40}%2F\d+\.\d+%2F(anonymous)$} $line {\1CODE=SHA1%2FNOW%2F\2} line
      regsub {^(GET /json/cap\?authToken\=)[0-9a-f]{40}/\d+\.\d+/(anonymous )} $line {\1SHA1/NOW/\2} line
      regsub {^(Cookie: fossil-)[0-9a-f]{16}\=[0-9A-F]{50}%2F[0-9a-f]{16}%2F(.*)$} $line {\1CODE=SHA1%2FCODE%2F\2} line
    } elseif { $testname eq "merge_renames" } {
      regsub {^(    (?:MERGED_WITH|BACKOUT)) [0-9a-f]{64}$} $line {\1 HASH} line
    } elseif { $testname eq "unversioned" } {
      regsub {^(Started Fossil server, pid \")\d+(\", port \")\d+} $line {\1PID\2PORT} line
      regsub {^(Now in client directory \")/.*?/uvtest_\d+_\d+\"} $line {\1/TMP/uvtest_SEC_SEQ} line
      regsub {^(Stopped Fossil server, pid \")\d+(\", using argument \")\d+} $line {\1PID\2PID} line
    }

    # Some common lines not tied to fossil or specific tests.
    regsub {^((?:ERROR \(1\): )?/[*]{5} Subprocess) \d+ (exit)} $line {\1 PID \2} line

    # Temporary directories
    regsub -all {/.*?/repo_\d+/\d+_\d+} $line {/TMP/repo_PID/SEC_SEQ} line

    puts "$line"
  }
}
