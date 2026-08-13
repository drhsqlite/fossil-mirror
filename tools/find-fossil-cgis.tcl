#!/usr/bin/tclsh
#
# This script scans a directory hierarchy looking for Fossil CGI files -
# the files that are used to launch Fossil as a CGI program.  For each
# such file found, in prints the name of the file and also the file
# content, indented, if the --print option is used.
#
#     tclsh find-fossil-cgis.tcl [OPTIONS] DIRECTORY
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
#    --print                    Show the content of the CGI, indented by
#                               three spaces
#
#    --symlink                  Process DIRECTORY arguments that are symlinks.
#                               Normally symlinks are silently ignored.
#
#    -v                         Show progress information for debugging
#
# EXAMPLE USE CASES:
#
# Find all CGIs that do not have the "errorlog:" property set
#
#    find-fossil-cgis.tcl *.website --has '\nrepository:' \
#           --hasnot '\nerrorlog:'
#
# Add the errorlog: property to any CGI that does not have it:
#
#    find-fossil-cgis.tcl *.website --has '\nrepository:' \
#           --hasnot '\nerrorlog:' | while read x
#    do
#      echo 'errorlog: /logs/errors.txt' >>$x
#    done
#
# Find and print all CGIs that do redirects
#
#    find-fossil-cgis.tcl *.website --has '\nredirect:' --print
#


# Find the CGIs in directory $dir.  Invoke recursively to
# scan subdirectories.
#
proc find_in_one_dir {dir} {
  global HAS HASNOT PRINT V
  if {$V>0} {
    puts "# $dir"
  }
  foreach obj [lsort [glob -nocomplain -directory $dir *]] {
    if {[file isdir $obj]} {
      find_in_one_dir $obj
      continue
    }
    if {![file isfile $obj]} continue
    if {[file size $obj]>5000} continue
    if {![file exec $obj]} continue
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
    if {$PRINT} {
      regsub -all {\n} [string trim $txt] "\n   " out
      puts "   $out"
    }
  }
}
set HAS [list]
set HASNOT [list]
set PRINT 0
set SYMLINK 0
set V 0
set N [llength $argv]
set DIRLIST [list]

# First pass:  Gather all the command-line arguments but do no
# processing.
#
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
  if {$dir eq "-print" || $dir eq "--print"} {
    set PRINT 1
    continue
  }
  if {$dir eq "-symlink" || $dir eq "--symlink"} {
    set SYMLINK 1
    continue
  }
  if {$dir eq "-v"} {
    set V 1
    continue
  }
  if {[file type $dir]=="directory"} {
    lappend DIRLIST $dir
  }
}

# Second pass: Process the non-option arguments.
#
foreach dir $DIRLIST {
  set type [file type $dir]
  if {$type eq "directory" || ($SYMLINK && $type eq "link")} {
    find_in_one_dir $dir
  }
}
