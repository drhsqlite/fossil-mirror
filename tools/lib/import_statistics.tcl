# -----------------------------------------------------------------------------
# Management of statistics for an import run.

# -----------------------------------------------------------------------------
# Requirements

package require Tcl 8.4
package require vc::tools::log  ; # User feedback

namespace eval ::vc::fossil::import::stats {
    vc::tools::log::system stats
    namespace import ::vc::tools::log::write
}

# -----------------------------------------------------------------------------
# API

#     vc::fossil::import::stats
#         setup n m  - Initialize module, expect n changesets, of m.
#         done       - Write final statistics.
#         csbegin id - Import of identified changeset begins.
#         csend x    - It took x seconds to import the changeset.
#         

# -----------------------------------------------------------------------------
# API Implementation - Functionality

proc ::vc::fossil::import::stats::init {n m} {
    variable total_csets   $n
    variable total_running 0
    variable total_seconds 0.0

    variable ntfmt %[string length $n]s
    variable nmfmt %[string length $m]s
    return
}

proc ::vc::fossil::import::stats::done {} {
    variable total_csets
    variable total_seconds

    write 0 stats "========= [string repeat = 61]"
    write 0 stats "Imported $total_csets [expr {($total_csets == 1) ? "changeset" : "changesets"}]"
    write 0 stats "Within [F $tot] seconds (avg [F [Avg]] seconds/changeset)"
    return
}

proc ::vc::fossil::import::stats::csbegin {cset} {
    variable nmfmt
    variable ntfmt
    write 0 stats "ChangeSet [format $nmfmt $cset] @ [format $ntfmt $total_running]/$total_csets ([F6 [expr {$total_running*100.0/$total_csets}]]%)"
    return
}

proc ::vc::fossil::import::stats::csend {seconds} {
    variable total_csets
    variable total_seconds
    variable total_running

    incr total_running
    set  total_seconds [expr {$total_seconds + $sec}]

    set avg [Avg]
    set end [expr {$total_csets * $avg}]
    set rem [expr {$end - $total_seconds}]

    write 2 stats "Imported in        [F7 $seconds] seconds"
    write 3 stats "Average Time/Cset  [F7 $avg] seconds"
    write 3 stats "Current Runtime    [FTime $total_seconds]"
    write 3 stats "Total Runtime  (E) [FTime $end]"
    write 3 stats "Remaining Time (E) [FTime $rem]"
    # (E) for Estimated.

    return
}

# -----------------------------------------------------------------------------
# Internal helper commands.

proc ::vc::fossil::import::stats::FTime {s} {
    set m [expr {$s / 60}]
    set h [expr {$s / 3600}]
    return "[F7 $s] sec [F6 $m] min [F5 $h] hr"
}

proc ::vc::fossil::import::stats::F  {x} { format %.2f  $x }
proc ::vc::fossil::import::stats::F5 {x} { format %5.2f $x }
proc ::vc::fossil::import::stats::F6 {x} { format %6.2f $x }
proc ::vc::fossil::import::stats::F7 {x} { format %7.2f $x }

proc ::vc::fossil::import::stats::Avg {} {
    variable total_seconds
    variable total_running
    return [expr {$total_seconds/$total_running}]
}

# -----------------------------------------------------------------------------

namespace eval ::vc::fossil::import::stats {
    namespace export setup done begin add
}

# -----------------------------------------------------------------------------
# Ready

package provide vc::fossil::import::stats 1.0
return
