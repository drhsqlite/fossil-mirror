#!/bin/sh
# This script creates the two repositories used by the bundle test tools.
# Syntax: make-bundle-data.sh oldrepo.fossil newrepo.fossil
# Warning! The two repositories will be *destroyed*!

set -e -x

oldrepo=$(readlink -f "$1")
newrepo=$(readlink -f "$2")

tmpdir=/tmp/$$.make-bundle-data

rm -rf $oldrepo $newrepo $tmpdir
fossil init $oldrepo
mkdir $tmpdir
(cd $tmpdir && fossil open $oldrepo && fossil settings -R $oldrepo autosync off)
(cd $tmpdir && echo "empty" > 1 && fossil add 1 && fossil commit -m "Empty")
(cd $tmpdir && echo "data" >> 1 && fossil commit -m "Add: data")
(cd $tmpdir && echo "more data" >> 1 && fossil commit -m "Add: more data")

fossil clone $oldrepo $newrepo
rm -rf $tmpdir
mkdir $tmpdir
(cd $tmpdir && fossil open $newrepo && fossil settings -R $oldrepo autosync off)
(cd $tmpdir && echo "even more data" >> 1 && fossil commit -m "Clone, add: even more data")
(cd $tmpdir && fossil tag add branchpoint tip)
(cd $tmpdir && echo "new file" > 2 && fossil add 2 && fossil commit -m "New file")
(cd $tmpdir && fossil update branchpoint)
(cd $tmpdir && echo "branched data" >> 1 && fossil commit -b branch -m "Branch, add: branched data")

