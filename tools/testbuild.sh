#!/bin/sh
#
# Make an out-of-tree build of Fossil for testing, or report on one.
#
#     tools/testbuild.sh asan DIR     Build with ASan and UBSan in DIR
#     tools/testbuild.sh cov DIR      Build with Clang coverage in DIR
#     tools/testbuild.sh report DIR   Print branch coverage for a "cov" build
#     tools/testbuild.sh missed DIR FILE...
#                                     List untested branches in source files
#
# DIR is created if it does not exist.  The source tree is not changed.
# Extra arguments after DIR are passed to "make" (for example: -j8).
#
# The "asan" build stops at the first error that a sanitizer reports.
#
# A "cov" build adds to a few profile files in DIR/prof on every run of
# DIR/fossil, as long as DIR does not move.  The runs share the files, so
# their size does not grow with the number of runs; one file for every
# run filled a disk.  Run whatever tests are of interest, for example
# "make test" in DIR, then use "report".  The report has one line per source file,
# showing the number of branch outcomes, the number never taken, and the
# percentage taken.  The files that implement the artifact store and the
# sync protocol are listed first.  Delete DIR/prof to start over.
#
# The "missed" command prints every line of the named source files (for
# example: delta.c) that holds a branch which was never taken, or never
# not taken.  A line is left out if test/coverage-marks.txt lists it with
# the reason why it cannot be tested.  The exit status is 1 if any line is
# printed, so the command can be used to hold a file at full coverage.
#
# The "cov", "report" and "missed" commands need Clang, llvm-profdata and
# llvm-cov.
#
usage(){
  sed -n '3,9p' "$0" | sed 's/^# \{0,1\}//' >&2
  exit 1
}
die(){
  echo "$0: $*" >&2
  exit 1
}

test $# -ge 2 || usage
cmd=$1
dir=$2
shift 2
top=`dirname "$0"`/..
top=`cd "$top" && pwd` || exit 1

# Find an LLVM tool.  On macOS the tools are reached through xcrun.
llvm(){
  if command -v "$1" >/dev/null 2>&1; then
    "$@"
  elif command -v xcrun >/dev/null 2>&1; then
    xcrun "$@"
  else
    die "cannot find $1"
  fi
}

case $cmd in
asan)
  mkdir -p "$dir" && cd "$dir" || exit 1
  "$top/configure" --fossil-debug --with-sanitizer=address,undefined || exit 1
  make "$@"
  ;;
cov)
  mkdir -p "$dir" && cd "$dir" || exit 1
  dir=`pwd`
  # Not --fossil-debug, because that option replaces CFLAGS.
  prof="-fprofile-instr-generate=$dir/prof/fossil-%4m.profraw"
  # The build's own helper programs are compiled with the same flags, so
  # they need the same compiler.
  CC=${CC:-clang} CC_FOR_BUILD=${CC:-clang} LDFLAGS="$prof" \
  CFLAGS="-g -O0 -Wall -DFOSSIL_DEBUG $prof -fcoverage-mapping" \
    "$top/configure" || exit 1
  make "$@"
  ;;
report)
  cd "$dir" || exit 1
  ls prof/*.profraw >/dev/null 2>&1 || die "no profiles in $dir/prof"
  llvm llvm-profdata merge -sparse prof/*.profraw -o prof/merged.profdata \
    || exit 1
  # The LCOV form of the coverage data has a documented layout: a record
  # for each source file, from its "SF:" line to "end_of_record", in which
  # "BRF:" gives the number of branch outcomes and "BRH:" how many of them
  # were taken.
  llvm llvm-cov export ./fossil -instr-profile=prof/merged.profdata \
       -format=lcov -ignore-filename-regex='(extsrc|/tools)/' |
  awk '
    function show(name){
      if( name in nBr ){
        printf "%-20s %8d %8d %7.1f%%\n", name, nBr[name], nMiss[name],
               nBr[name] ? 100.0*(nBr[name]-nMiss[name])/nBr[name] : 100.0
        delete nBr[name]
      }
    }
    /^SF:/ {
      name = substr($0, 4)
      sub(/^.*\//, "", name)
      sub(/_\.c$/, ".c", name)
      found = 0
      hit = 0
    }
    /^BRF:/ { found = substr($0, 5) + 0 }
    /^BRH:/ { hit = substr($0, 5) + 0 }
    /^end_of_record/ && name ~ /\.c$/ {
      nBr[name] = found
      nMiss[name] = found - hit
      order[++n] = name
      totBr += found
      totMiss += found - hit
    }
    END{
      if( n==0 ){ print "no coverage data" > "/dev/stderr"; exit 1 }
      printf "%-20s %8s %8s %8s\n", "File", "Branches", "Missed", "Taken"
      show("delta.c"); show("content.c"); show("manifest.c"); show("xfer.c")
      print "--"
      for(i=1; i<=n; i++) show(order[i])
      print "--"
      printf "%-20s %8d %8d %7.1f%%\n", "Total", totBr, totMiss,
             totBr ? 100.0*(totBr-totMiss)/totBr : 100.0
    }'
  ;;
missed)
  test $# -ge 1 || usage
  cd "$dir" || exit 1
  ls prof/*.profraw >/dev/null 2>&1 || die "no profiles in $dir/prof"
  llvm llvm-profdata merge -sparse prof/*.profraw -o prof/merged.profdata \
    || exit 1
  rc=0
  for file in "$@"; do
    src=bld/`basename "$file" .c`_.c
    test -f "$src" || die "no such source file in the build: $file"
    # This reads the text that "llvm-cov show" prints, because the LCOV
    # form does not say which branches the compiler folded away, as it
    # does for "while( 1 )", and would report those as never taken.
    llvm llvm-cov show ./fossil -instr-profile=prof/merged.profdata \
         -show-branches=count "$src" |
    awk -v file="$file" -v marks="$top/test/coverage-marks.txt" '
      function trim(x){ sub(/^[ \t]+/, "", x); sub(/[ \t]+$/, "", x); return x }
      BEGIN{
        FS = "|"
        # The fields of a mark are separated by " | ".  The source line
        # in the middle can hold "|" and "||" of its own.
        while( (getline line < marks)>0 ){
          if( line ~ /^#/ ) continue
          i = index(line, " | ")
          if( i==0 ) continue
          name = trim(substr(line, 1, i-1))
          line = substr(line, i+3)
          j = 0
          while( (k = index(substr(line, j+1), " | "))>0 ) j += k
          if( j==0 ) continue
          if( name==file ) isMarked[trim(substr(line, 1, j-1))] = 1
        }
        FS = "|"
      }
      /^ *[0-9]+\|/ {
        ln = $1 + 0
        txt = $0
        sub(/^[^|]*\|[^|]*\|/, "", txt)
        text[ln] = trim(txt)
        next
      }
      /Branch \(/ && ( /True: 0,/ || /False: 0\]/ ) {
        ln = $0
        sub(/^.*Branch \(/, "", ln)
        sub(/:.*$/, "", ln)
        ln += 0
        if( !(ln in seen) ){ seen[ln] = 1; order[++nMiss] = ln }
      }
      END{
        for(i=1; i<=nMiss; i++){
          ln = order[i]
          if( text[ln] in isMarked ) continue
          printf "%s:%d: %s\n", file, ln, text[ln]
          bad = 1
        }
        exit bad
      }' || rc=1
  done
  exit $rc
  ;;
*)
  usage
  ;;
esac
