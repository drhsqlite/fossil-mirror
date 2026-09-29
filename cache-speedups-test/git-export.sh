#!/bin/sh
# Throw-away cache size test scripts by Dan Shearer, Sep 2026
# Needs ../fossil built from the cache-speedups branch
set -eu
unset FOSSIL_SQLCACHE

repository=${1:?usage: $0 repository.fossil}
copy=${TMPDIR:-/tmp}/${repository##*/}
mirror=$PWD/git-export-mirror
trap 'rm -rf "$mirror" "$copy"' EXIT HUP INT TERM
printf 'command\trepetition\tcache_n\telapsed\tuser\tsystem\tmax_rss_kb\n'
cache_values=${CACHE_VALUES:-"500 1000 1500 2000 3000 5000"}
if [ -n "${GIT_LIMIT:-}" ]; then
  set -- --limit "$GIT_LIMIT"
else
  set --
fi
for repetition in $(seq 1 "${REPEATS:-3}"); do
  for n in $(printf '%s\n' $cache_values | shuf); do
    export FOSSIL_CACHE_N=$n FOSSIL_CACHE_SZ=${CACHE_SZ:-1000000000}
    rm -rf "$mirror"
    git init -q "$mirror"
    cp "$repository" "$copy"
    /usr/bin/time -f "git-export\t$repetition\t$n\t%e\t%U\t%S\t%M" \
      ../fossil git export "$mirror" -R "$copy" "$@" -q -q
  done
done
