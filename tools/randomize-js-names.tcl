#!/usr/bin/tclsh
#
# This script is run as part of "make wasm".  After emcc has
# run to generate extsrc/pikchr.wasm and extsrc/pikchr.js from
# extsrc/pikchr.c, we need to make changes to these filenames to
# work around caching problems.
#
#    (1)  in extsrc/pikchr.js ->  change "pikchr.wasm" into
#         "pikchr-vNNNNNNNN.wasm" where Ns are random digits.
#
#    (2)  in extsrc/pikchr-worker.js -> change "pikchr-vNNNNNNNN.js"
#         by altering the random digits N.
#
set DIR extsrc
if {[llength $argv]>0} {
  set DIR [lindex $argv 0]
}

set R [expr {int(rand()*10000000000)+1000000000}]
set in [open $DIR/pikchr.js rb]
set f1 [read $in]
close $in
set f1mod [regsub {\ypikchr(-v\d+)?\.wasm\y} $f1 "pikchr-v$R.wasm"]
set out [open $DIR/pikchr.js wb]
puts -nonewline $out $f1mod
close $out
puts "modified $DIR/pikchr.js to reference \"pikchr-v$R.wasm\""

set in [open $DIR/pikchr-worker.js rb]
set f1 [read $in]
close $in
set f1mod [regsub {\ypikchr(-v\d+)?\.js\y} $f1 "pikchr-v$R.js"]
set out [open $DIR/pikchr-worker.js wb]
puts -nonewline $out $f1mod
close $out
puts "modified $DIR/pikchr-worker.js to reference \"pikchr-v$R.js\""
