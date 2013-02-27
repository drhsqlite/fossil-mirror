#!/bin/sh
#
# Run this script in an empty directory.  A single argument is the full
# pathname of the fossil binary.  Example:
#
#     sh update-test-2.sh /home/drh/fossil/m1/fossil
#
export FOSSIL=$1
rm -rf aaa bbb update-test-2.fossil

# Create a test repository
$FOSSIL new update-test-2.fossil

# In checkout aaa, add file one.txt.
mkdir aaa
cd aaa
$FOSSIL open ../update-test-2.fossil
echo one >one.txt
$FOSSIL add one.txt
$FOSSIL commit -m add-one --tag add-one

# Create checkout bbb.
mkdir ../bbb
cd ../bbb
$FOSSIL open ../update-test-2.fossil

# Back in aaa, make changes to one.txt.  Add file two.txt.
cd ../aaa
echo change >>one.txt
echo two >two.txt
$FOSSIL add two.txt
$FOSSIL commit -m 'chng one and add two' --tag add-two

# In bbb, remove one.txt, then update.
cd ../bbb
$FOSSIL rm one.txt
$FOSSIL changes
echo '========================================================================'
$FOSSIL update
echo '======== Previous should show "ADD two.txt" and conflict on one.txt ===='
$FOSSIL changes
echo '======== The previous should show "DELETE one.txt" ====================='
$FOSSIL commit --test -m 'check-in'
echo '======== Only file two.txt is checked in ==============================='
