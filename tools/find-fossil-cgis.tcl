#!/usr/bin/tclsh
#
# This script scans a directory hierarchy looking for Fossil CGI files -
# the files that are used to launch Fossil as a CGI program.  For each
# such file found, in prints the name of the file and then prints the
# file content, indented.
#
#     tclsh find-fossil-cgis.tcl $DIRECTORY
#
# The argument is the directory from which to begin the search.
#


# Find the CGIs in directory $dir.  Invoke recursively to
# scan subdirectories.
#
proc find_in_one_dir {dir} {
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
    # 
    # At this point assume we have found a CGI file.
    #
    puts $obj
    regsub -all {\n} [string trim $txt] "\n   " out
    puts "   $out"
  }
}
foreach dir $argv {
  find_in_one_dir $dir
}
