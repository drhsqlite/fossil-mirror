# The test corpus: inputs for "fossil test-fuzz" and for scenario.test,
# all kept in the one file corpus/corpus.pack.  Many small files would
# each add a line to every check-in manifest of the project, so they are
# packed and unpacked into the test's own directory when a test runs.  The
# pack is in a directory of its own because merge2.test runs every file
# directly in this directory through a three-way merge.
#
#     tclsh corpus.tcl unpack corpus/corpus.pack DIR   write DIR/TYPE/NAME
#     tclsh corpus.tcl pack DIR corpus/corpus.pack     pack DIR/TYPE/NAME
#
# To add an input: unpack, add the file, pack, and check that the pack
# changed only by the new record.  A fuzzing engine such as AFL++ or
# libFuzzer can use an unpacked directory as its starting corpus, and
# "fossil test-fuzz DIR/TYPE" runs one directory through its target.
#
# The pack is a few lines that begin with "#", then one record per file:
#
#     == TYPE/NAME LENGTH
#     LENGTH bytes of content, then a newline
#
# Records are sorted by TYPE/NAME, so packing the same files gives the
# same pack.
#
# TYPE is the fuzz target the file is an input to, or "scenario".
#
# Each input is a case that its target must handle without a crash, a
# report from a run-time checker, or a failed property.  When a fuzzer
# finds an input that breaks something, the minimized input is added
# once the defect is fixed, named for what it showed.
#
# The delta targets take a pair of files as one input: two bytes giving
# the size of the first file, most significant byte first, then the
# first file, then the second.
#
# delta-roundtrip: the pair is a source and a target.  A delta is made
# from one to the other and must reproduce the target.
#
# delta-apply, delta-accept, delta-reject: the pair is a source and a
# delta.  All three apply the delta and check the same properties.
# delta-accept also requires that the delta be accepted, and
# delta-reject that it be refused, so those inputs are examples with a
# known answer.  The delta-accept inputs whose names begin with "ok-"
# hold deltas made by Fossil.  Each delta-reject input is named for what
# is wrong with it.  A fuzzing engine should be pointed at delta-apply,
# with all three as its starting corpus.
#
# manifest: each input is one structural artifact: a check-in, tag, wiki
# page, ticket, technote, attachment or forum post.  The inputs whose
# names begin with "ok-" were made by Fossil.  The rest are one of those
# with a single card changed, and a Z card that is still correct so that
# the parser reads as far as the change.  Names give the cards of the
# artifact, the card that was changed, and how.
#
# manifest-body: each input is an artifact without its Z card.  The
# target adds the right one, so every input reaches the parser proper.
# This is the corpus to give a fuzzing engine, which cannot produce a
# matching checksum by changing bytes.  The inputs named with card
# letters only are the "ok-" inputs of manifest with the Z card removed.
# A name with a suffix after the letters is an input that once showed a
# defect.  The "fuzz-" inputs were found by libFuzzer and are kept for
# the branches of manifest.c they reach: a run from the other inputs was
# reduced with -set_cover_merge=1, and from the result a small set was
# chosen that still takes every branch outcome in manifest.c the whole
# set takes.  Their names give the card letters that begin a line, then
# part of the SHA1 of the content.
#
# Every manifest and manifest-body input begins with two lines that
# start with "#".  They are not part of the input: the target removes
# them.  They are there because Fossil treats any file whose content is
# a well-formed artifact as a real one.  Without them, a repository that
# received an unpacked input by sync, or was rebuilt while holding it,
# would show it as a check-in, wiki page or ticket of its own.  A new
# input of either type must begin with the same two lines; fuzz.test
# checks that each one does, and that Fossil accepts neither the pack
# nor any unpacked input as an artifact as stored.
#
# xfer: each input is the body of one sync request as a client sends it
# to a server, not compressed.  The target serves it from a small
# repository that is the same for every input; fuzz_xfer_setup() in
# src/fuzz.c says what it holds.  The pull and push cards give that
# repository's project code, and the gimme cards and delta sources are
# its artifacts.  An input is named for the cards it holds.
#
# scenario: these are not inputs to a fuzz target.  Each is a random
# integration test of sync, run by scenario.test: a list of four-byte
# records, each one operation on one of three clones of a new
# repository: operation, clone, argument, option.  See scenario.test for
# the operations.  A sync can be told to lose its request, lose its
# reply or deliver half a reply.  A sync, pull, push or clone that is
# not told to go wrong must deliver everything at once.  The clones are
# synced at the end and must then be sound and identical.  Each input
# takes a second or two, because every operation is a separate run of
# fossil.  An input is named for what it tests.
#

# Write every record of the pack PACK as a file DIR/TYPE/NAME and return
# the list of TYPE directories made, sorted.  A malformed pack is an
# error.
#
proc corpus_unpack {pack dir} {
  set fd [open $pack rb]
  set data [read $fd]
  close $fd
  set at 0
  set end [string length $data]
  # The comment lines at the top.
  while {$at < $end && [string index $data $at] eq "#"} {
    set nl [string first \n $data $at]
    if {$nl < 0} { error "$pack: no newline after a comment line" }
    set at [expr {$nl + 1}]
  }
  set types {}
  while {$at < $end} {
    set nl [string first \n $data $at]
    if {$nl < 0} { error "$pack: no newline after a record header" }
    set header [string range $data $at [expr {$nl - 1}]]
    if {![regexp {^== ([a-z0-9-]+)/([A-Za-z0-9._+-]+) ([0-9]+)$} $header \
            all type name len]
        || [string index $name 0] eq "."} {
      error "$pack: malformed record header \"$header\""
    }
    set len [scan $len %d]
    set from [expr {$nl + 1}]
    set to [expr {$from + $len}]
    if {$to >= $end || [string index $data $to] ne "\n"} {
      error "$pack: record $type/$name is cut short"
    }
    set path [file join $dir $type $name]
    if {[file exists $path]} { error "$pack: record $type/$name repeated" }
    file mkdir [file join $dir $type]
    set out [open $path wb]
    puts -nonewline $out [string range $data $from [expr {$to - 1}]]
    close $out
    if {$type ni $types} { lappend types $type }
    set at [expr {$to + 1}]
  }
  return [lsort $types]
}

# Pack every file DIR/TYPE/NAME into the pack PACK.  Files directly in
# DIR are not packed.
#
proc corpus_pack {dir pack} {
  set records {}
  foreach typeDir [glob -nocomplain -types d [file join $dir *]] {
    foreach f [glob -nocomplain -types f [file join $typeDir *]] {
      lappend records [list [file tail $typeDir]/[file tail $f] $f]
    }
  }
  set out [open $pack wb]
  puts -nonewline $out "# Inputs for fuzz.test and scenario.test, packed by corpus.tcl.\n"
  puts -nonewline $out "# See corpus.tcl for the format and for what each type holds.\n"
  foreach r [lsort -index 0 $records] {
    lassign $r key f
    set fd [open $f rb]
    set content [read $fd]
    close $fd
    puts -nonewline $out "== $key [string length $content]\n$content\n"
  }
  close $out
  return [llength $records]
}

if {[info exists ::argv0]
    && [file normalize $::argv0] eq [file normalize [info script]]} {
  switch -- [lindex $::argv 0] {
    pack {
      if {[llength $::argv] != 3} { puts stderr "usage: pack DIR PACK"; exit 2 }
      puts "[corpus_pack [lindex $::argv 1] [lindex $::argv 2]] files packed"
    }
    unpack {
      if {[llength $::argv] != 3} { puts stderr "usage: unpack PACK DIR"; exit 2 }
      puts [corpus_unpack [lindex $::argv 1] [lindex $::argv 2]]
    }
    default {
      puts stderr "usage: tclsh corpus.tcl pack DIR PACK | unpack PACK DIR"
      exit 2
    }
  }
}
