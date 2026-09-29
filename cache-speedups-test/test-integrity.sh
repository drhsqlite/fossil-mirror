#!/bin/sh
# Throw-away cache size test scripts by Dan Shearer, Sep 2026
# Needs ../fossil built from the cache-speedups branch
set -eu
unset FOSSIL_SQLCACHE

repository=${1:?usage: $0 repository.fossil}
copy=${TMPDIR:-/tmp}/${repository##*/}
printf 'command\trepetition\tcache_n\telapsed\tuser\tsystem\tmax_rss_kb\n'
for repetition in $(seq 1 "${REPEATS:-3}"); do
  for n in $(printf '%s\n' ${CACHE_VALUES:-500 1000 1500 2000 3000 5000} | shuf); do
    export FOSSIL_CACHE_N=$n FOSSIL_CACHE_SZ=${CACHE_SZ:-1000000000}
    cp "$repository" "$copy"
    /usr/bin/time -f "test-integrity\t$repetition\t$n\t%e\t%U\t%S\t%M" \
      ../fossil test-integrity -R "$copy" >/dev/null
  done
done
