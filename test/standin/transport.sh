#!/bin/sh
# Stands in for a server.  A test gives it to "fossil sync", "pull" or
# "push" as the --transport-command, and fossil calls it for each request:
#
#     transport.sh URL REQUESTFILE REPLYFILE
#
# Request N is saved as request.N in the directory $TRANSPORT_DIR and
# answered with reply.N from there.  Where the test wrote no reply.N, the
# answer is a bare timestamp, which ends the sync.  Request 21 fails, so
# that a client that keeps asking is stopped.
count=$(cat "$TRANSPORT_DIR/count" 2>/dev/null || echo 0)
count=$((count + 1))
echo $count > "$TRANSPORT_DIR/count"
cp "$2" "$TRANSPORT_DIR/request.$count"
if test $count -gt 20; then exit 1; fi
if test -f "$TRANSPORT_DIR/reply.$count"; then
  cp "$TRANSPORT_DIR/reply.$count" "$3"
else
  printf '# timestamp %s errors 0\n' "$(date -u +%Y-%m-%dT%H:%M:%S)" > "$3"
fi
