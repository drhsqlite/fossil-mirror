sudo -v
for n in 0 -32000 -256000; do
  echo "sqlcache=$n" ; export FOSSIL_SQLCACHE=$n
  cp sqlite.fossil /tmp/sqlite.fossil
  sync; echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null # Linux only
  time ../fossil rebuild /tmp/sqlite.fossil
done
