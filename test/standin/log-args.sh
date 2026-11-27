#!/bin/sh
# Stands in for a program that fossil runs, such as ssh or a merge tool.
# Appends each of its arguments to the file $ARGS_LOG, on a line of its
# own inside <>, and exits with status 1 without doing anything else.
for argument in "$@"; do
  printf '<%s>\n' "$argument"
done >> "$ARGS_LOG"
exit 1
