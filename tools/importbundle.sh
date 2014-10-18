#!/bin/sh
# Imports a bundle which has been exported with exportbundle.
# Syntax: exportbundle.sh oldrepo.fossil newrepo.fossil data.bundle

set -e

oldrepo="$1"
newrepo="$2"
bundle="$3"

fossil clone $oldrepo $newrepo
fossil import --incremental $newrepo < $bundle

