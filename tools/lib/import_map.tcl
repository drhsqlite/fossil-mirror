# -----------------------------------------------------------------------------
# Management of the mapping between cvs changesets and fossil uuids.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::tools::log  ; # User feedback

namespace eval ::vc::fossil::import::map {
    vc::tools::log::system map
    namespace import ::vc::tools::log::write
}

# -----------------------------------------------------------------------------
# API

#     vc::fossil::import::map
#         set cset uuid    - Associate changeset with uuid
#         get cset -> uuid - Retrieve uuid for changeset.

# -----------------------------------------------------------------------------
# API Implementation - Functionality

proc ::vc::fossil::import::map::set {cset uuid} {
    variable map
    ::set map($cset) $uuid
    write 2 map "== $uuid"
    return
}

proc ::vc::fossil::import::map::get {cset} {
    variable map
    return $map($cset)
}

# -----------------------------------------------------------------------------

namespace eval ::vc::fossil::import::map {
    variable  map    ; # Map from csets to uuids
    array set map {} ; #

    namespace export get set
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::fossil::import::map 1.0
return
