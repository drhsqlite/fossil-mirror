#
# Sourced by test files with tests of faults that are fixed on trunk.  A
# fossil built from an older trunk has those faults.  Each such test
# passes $trunkMark as its constraint: "knownBug" for a fossil with the
# faults, so that their tests are reported but not counted as failures,
# and empty for a fossil without them.
#
# A fossil with the fixes names randhex when TH1 randhex is given too many
# arguments.  The check needs no repository or check-out.
#
if {![info exists trunkMark]} {
  catch {exec $::fossilexe test-th-eval {randhex 1 2} 2>@1} trunkFixesReply
  if {[string first {should be "randhex} $trunkFixesReply] >= 0} {
    set trunkMark ""
  } else {
    set trunkMark knownBug
  }
}

# Some tests send input on which the address or undefined-behaviour
# sanitizer reports a fault that is not fixed on trunk.  Those tests are
# skipped on a fossil built with a sanitizer.  sanitizerBuild is 1 for
# such a fossil: its file holds the sanitizer's entry points.
#
if {![info exists sanitizerBuild]} {
  set trunkFixesFd [open $::fossilexe rb]
  set trunkFixesBytes [read $trunkFixesFd]
  close $trunkFixesFd
  set sanitizerBuild [expr {
    [string first __asan_init $trunkFixesBytes] >= 0 ||
    [string first __ubsan_handle $trunkFixesBytes] >= 0}]
  unset trunkFixesBytes trunkFixesFd
}

# Return trunkMark if VALUE is one of VALUES, and an empty string
# otherwise: the constraint for a test made in a loop, only some of whose
# rounds test a fix on trunk.
#
proc trunk_mark {value values} {
  if {$value in $values} {
    return $::trunkMark
  }
  return ""
}

# The constraint for a test of a fix on trunk whose fault only a sanitizer
# reports: trunkMark on a sanitizer build, and an empty string otherwise.
# With VALUE and VALUES, as for trunk_mark.
#
proc sanitizer_trunk_mark {args} {
  if {!$::sanitizerBuild} {
    return ""
  }
  if {[llength $args] == 0} {
    return $::trunkMark
  }
  return [trunk_mark {*}$args]
}
