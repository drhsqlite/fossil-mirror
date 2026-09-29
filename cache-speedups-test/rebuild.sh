#!/bin/sh
# Throw-away cache size test scripts by Dan Shearer, Sep 2026
# Needs ../fossil built from the cache-speedups branch
set -eu

repository=${1:?usage: $0 repository.fossil}
copy=${TMPDIR:-/tmp}/${repository##*/}
printf 'command\trepetition\tsqlcache_kib\telapsed\tuser\tsystem\tmax_rss_kb\n'
for repetition in $(seq 1 "${REPEATS:-3}"); do
  for n in $(printf '%s\n' ${CACHE_VALUES:-0 -16000 -24000 -32000 -64000} | shuf); do
    export FOSSIL_SQLCACHE=$n
    cp "$repository" "$copy"
    /usr/bin/time -f "rebuild\t$repetition\t$n\t%e\t%U\t%S\t%M" \
      ../fossil rebuild "$copy" --quiet
  done
done
