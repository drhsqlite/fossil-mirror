#!/bin/sh
#
# This is a TCL script that tries to sync the changes in a local
# Fossil checkout to another machine.  The changes are gathered into
# a tarball, then sent via ssh to the remote and unpacked.
#
# Usage:
#
#     co-rsync.tcl REMOTE
#
# Where REMOTE is the root of the remote repository into which changes
# are to be moved.
#
# Use Case:
#
# Sometimes while in the middle of an edit it is useful to transfer
# the incomplete changes to another machine for testing.  This could
# be accomplished using scp, but doing it that was is tedious if many
# files in multiple directories have changed.  This command does all
# the necessary transfer using a single command.
#
# A Tcl comment, whose contents don't matter \
exec tclsh "$0" "$@"

# Begin by changing directories to the root of the check-out.
#
set remote {}
set dryrun 0
proc usage {} {
  puts stderr "Usage: $::argv0 REMOTE"
  puts stderr "Options:"
  puts stderr "  --dryrun      No-op but print what would have happened"
  exit 1
}
foreach arg $argv {
  if {$arg=="--dryrun" || $arg=="--dry-run"} {
    set dryrun 1
    continue
  }
  if {$remote!=""} {
    usage
  }
  set remote $arg
}
if {$remote==""} usage

set in [open {|fossil status} rb]
set status [read $in]
if {[catch {close $in} msg]} {
  puts stderr $msg
  exit 1
}
set root {}
regexp {local-root: +([^\n]+)} $status all root
if {$root==""} {
  puts stderr "not in a fossil check-out"
  exit 1
}
cd $root
set tmpname filelist-
for {set i 0} {$i<3} {incr i} {
  append tmpname [format %08x [expr {int(rand()*0xffffffff)}]]
}
set out [open $tmpname wb]
puts $out [exec fossil changes --no-classify --no-merge]
close $out
set cmd "rsync -v --files-from=$tmpname . $remote"
puts $cmd
if {!$dryrun} {
  exec {*}$cmd
}
file delete $tmpname
