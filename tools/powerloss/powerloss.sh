#!/bin/sh
#
# Usage: powerloss.sh FOSSIL-BINARY WORKDIR ?REPO-MODE?
#
# Dan Shearer, November 2026
#
# A standalone driver for powerloss.so, and the worked example in
# README-POWERLOSS.md.  test/powerloss.test in Fossil's test suite
# interrupts the same check-in with more checks, and other commands too.
#
# Runs a check-in once for each point where powerloss.so will stop it.
# After each stop, prints the integrity_check result of the repository and
# the check-out database, and whether the two agree on the latest check-in.
#
# FOSSIL-BINARY creates the repository and check-out, and reads both
# databases with its sqlite3 command.  Give it as an absolute path, because
# the script runs it from other directories.  WORKDIR is deleted and
# recreated.  REPO-MODE is "wal" (the default) or "delete".
#
# PL_COMMIT is the shell command that makes the check-in.  It defaults to
# "FOSSIL-BINARY commit -m two", and lets you test another check-in
# program against the same repository.
#
# With STOP_MODE=kill in the environment, powerloss.so also stops at each
# pwrite and write, and leaves the files as the program wrote them, as a
# killed process would.
#
# Exits with status 0, with 1 if a run did not stop at its stop point, or
# with 2 if the set-up failed.

usage(){ echo "usage: $0 FOSSIL-BINARY WORKDIR ?wal|delete?" >&2; exit 2; }
[ $# -ge 2 ] || usage
fossil=$1
work=$2
case ${3:-wal} in
  wal) walopt=--wal ;;
  delete) walopt= ;;
  *) usage ;;
esac
shim=$(cd "$(dirname "$0")" && pwd)/powerloss.so
if [ ! -f "$shim" ]; then
  echo "$0: build $shim from powerloss.c first" >&2
  exit 2
fi
repo=$work/run/r.fossil
ckout=$work/run/ck/.fslckout

sql(){ "$fossil" sqlite3 --no-repository -list "$@" 2>&1; }

rm -rf "$work"
mkdir -p "$work/home" "$work/run/ck"
export FOSSIL_HOME="$work/home" FOSSIL_USER=tester

(
  set -e
  cd "$work/run"
  "$fossil" new r.fossil
  "$fossil" rebuild $walopt r.fossil
  cd ck
  "$fossil" open ../r.fossil
  echo one >a.txt
  echo bee >b.txt
  echo sea >c.txt
  "$fossil" add a.txt b.txt c.txt
  "$fossil" commit -m one
  echo "second version" >a.txt
  echo new >d.txt
  "$fossil" add d.txt
  "$fossil" rm b.txt
  "$fossil" mv --hard c.txt e.txt
) >/dev/null
[ $? -eq 0 ] || exit 2
echo "repository journal mode: $(sql "$repo" 'PRAGMA journal_mode;')"
echo "check-out journal mode: $(sql "$ckout" 'PRAGMA journal_mode;')"
cp -pR "$work/run" "$work/template"

# Prints the check-in that .fslckout records, the repository's latest
# check-in, the integrity_check result of each database, and a verdict:
# AHEAD when the repository does not have the check-in that .fslckout
# records, BEHIND when the repository has a later one, and otherwise
# consistent.
state(){
  ckin=$(sql "$ckout" "SELECT value FROM vvar WHERE name='checkout';")
  have=$(sql "$repo" "SELECT count(*) FROM blob WHERE rid=0+'$ckin';")
  last=$(sql "$repo" "SELECT objid FROM event WHERE type='ci'
                      ORDER BY mtime DESC, objid DESC LIMIT 1;")
  repook=$(sql "$repo" 'PRAGMA integrity_check;')
  ckoutok=$(sql "$ckout" 'PRAGMA integrity_check;')
  if [ "$have" != 1 ]; then verdict=AHEAD
  elif [ "$ckin" != "$last" ]; then verdict=BEHIND
  else verdict=consistent; fi
  echo "checkout=$ckin repo-latest=$last integrity=$repook/$ckoutok $verdict"
}

# Makes the check-in from a fresh copy of the template.  powerloss.so
# stops it at stop point $1, or lets it finish if $1 is 0.  Returns the
# exit status of the check-in command.
run(){
  rm -rf "$work/run" "$work/shadow" "$work/log"
  cp -pR "$work/template" "$work/run"
  ( export STOP_DIR="$work" STOP_AT=$1 LD_PRELOAD="$shim"
    cd "$work/run/ck" || exit 2
    if [ -n "$PL_COMMIT" ]; then sh -c "$PL_COMMIT"
    else "$fossil" commit -m two; fi
  ) >/dev/null 2>&1
}

run 0
rc=$?
if [ ! -s "$work/log" ]; then
  echo "$0: powerloss.so logged no stop points (exit $rc)." \
       "A statically linked fossil does not load it." >&2
  exit 2
fi
n=$(($(wc -l <"$work/log")))
echo "commit without a stop: exit $rc, $n stop points"

# A run that does not stop at its stop point leaves the files of a
# finished command, and those files pass every check in state().  So the
# loop requires that the run logged the stop point, that in power mode it
# logged nothing after it, and that the exit status is not 0.  In kill
# mode the other processes of the command continue, and can log more.
# The status is 77 when the check-in command itself stops, and some other
# status when a process that the command started stops and the command
# then fails.  Each line of output has the call and the file that the
# stopped run logged at the stop point.
fail=0
i=0
while [ "$i" -lt "$n" ]; do
  i=$((i+1))
  run "$i"
  rc=$?
  logged=$(($(wc -l <"$work/log")))
  last=$(sed -n "${i}p" "$work/log" | cut -d' ' -f2,3)
  if [ "$rc" = 0 ] || [ -z "$last" ] ||
     { [ "$STOP_MODE" != kill ] && [ "$logged" != "$i" ]; }; then
    echo "$i $last DID NOT STOP THERE: exit $rc," \
         "$logged stop points logged"
    fail=1
    continue
  fi
  echo "$i $last $(state)"
done
exit $fail
