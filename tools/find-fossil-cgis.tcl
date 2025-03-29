#!/usr/bin/tclsh
#
# This script scans a directory hierarchy looking for Fossil CGI files -
# the files that are used to launch Fossil as a CGI program.  For each
# such file found, in prints the name of the file and then prints the
# file content, indented.
#
#     tclsh find-fossil-cgis.tcl [OPTIONS] $DIRECTORY
#
# The argument is the directory from which to begin the search.
#
# OPTIONS can be zero or more of the following:
#
#    --has REGEXP               Only show the CGI if the body matches REGEXP.
#                               May be repeated multiple times, in which case
#                               all must match.
#
#    --hasnot REGEXP            Only show the CGI if it does NOT match the
#                               REGEXP.
#


# Find the CGIs in directory $dir.  Invoke recursively to
# scan subdirectories.
#
proc find_in_one_dir {dir} {
  global HAS HASNOT
  foreach obj [lsort [glob -nocomplain -directory $dir *]] {
    if {[file isdir $obj]} {
      find_in_one_dir $obj
      continue
    }
    if {![file isfile $obj]} continue
    if {[file size $obj]>5000} continue
    if {![file readable $obj]} continue
    set fd [open $obj rb]
    set txt [read $fd]
    close $fd
    if {![string match #!* $txt]} continue
    if {![regexp {fossil} $txt]} continue
    if {![regexp {\nrepository: } $txt] &&
        ![regexp {\ndirectory: } $txt] &&
        ![regexp {\nredirect: } $txt]} continue
    set ok 1
    foreach re $HAS {
      if {![regexp $re $txt]} {set ok 0; break;}
    }
    if {!$ok} continue
    foreach re $HASNOT {
      if {[regexp $re $txt]} {set ok 0; break;}
    }
    if {!$ok} continue
    # 
    # At this point assume we have found a CGI file.
    #
    puts $obj
    regsub -all {\n} [string trim $txt] "\n   " out
    puts "   $out"
  }
}
set HAS [list]
set HASNOT [list]
set N [llength $argv]
for {set i 0} {$i<$N} {incr i} {
  set dir [lindex $argv $i]
  if {($dir eq "-has" || $dir eq "--has") && $i<[expr {$N-1}]} {
    incr i
    lappend HAS [lindex $argv $i]
    continue
  }
  if {($dir eq "-hasnot" || $dir eq "--hasnot") && $i<[expr {$N-1}]} {
    incr i
    lappend HASNOT [lindex $argv $i]
    continue
  }
  find_in_one_dir $dir
}
