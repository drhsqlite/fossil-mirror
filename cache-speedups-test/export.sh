sudo -v
for n in 500 2000 5000; do
  echo "cache_n=$n" ; export FOSSIL_CACHE_N=$n
  export FOSSIL_CACHE_SZ=250000000 # This needs to be high but otherwise doesnt matter
  cp sqlite.fossil /tmp/sqlite.fossil
  sync; echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null # Linux only
  time ../fossil export -R /tmp/sqlite.fossil > /dev/null
done
