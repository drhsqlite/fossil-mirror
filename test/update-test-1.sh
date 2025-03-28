#!/bin/sh
#
# Run this script in an empty directory.  A single argument is the full
# pathname of the fossil binary.  Example:
#
#     sh update-test-1.sh /home/drh/fossil/m1/fossil
#
export FOSSIL=$1
rm -rf aaa bbb update-test-1.fossil

# Create a test repository
$FOSSIL new update-test-1.fossil

# In checkout aaa, add file one.txt
mkdir aaa
cd aaa
$FOSSIL open ../update-test-1.fossil
echo one >one.txt
$FOSSIL add one.txt
$FOSSIL commit -m add-one --tag add-one

# Open checkout bbb.
mkdir ../bbb
cd ../bbb
$FOSSIL open ../update-test-1.fossil

# Back in aaa, add file two.txt
cd ../aaa
echo two >two.txt
$FOSSIL add two.txt
$FOSSIL commit -m add-two --tag add-two

# In bbb, delete file one.txt.  Then update the change from aaa that
# adds file two.  Verify that one.txt says deleted.
cd ../bbb
$FOSSIL rm one.txt
$FOSSIL changes
echo '========================================================================'
$FOSSIL update
echo '======== The previous should show "ADD two.txt" ========================'
$FOSSIL changes
echo '======== The previous should show "DELETE one.txt" ====================='
$FOSSIL commit --test -m check-in
echo '======== Only file two.txt is checked in ==============================='
