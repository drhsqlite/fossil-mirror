# For this project, disable the pager for --help
set useropts(nopager) 1

# Searches for a usable Tcl (prefer 8.6, 8.5, 8.4) in the given paths
# Returns a dictionary of the contents of the tclConfig.sh file, or
# empty if not found
proc parse-tclconfig-sh {args} {
	foreach p $args {
		# Allow pointing directly to the path containing tclConfig.sh
		if {[file exists $p/tclConfig.sh]} {
			return [parse-tclconfig-sh-file $p/tclConfig.sh]
		}
		# Some systems allow for multiple versions
		foreach libpath {lib/tcl8.6 lib/tcl8.5 lib/tcl8.4 lib/tcl tcl lib}  {
			if {[file exists $p/$libpath/tclConfig.sh]} {
				return [parse-tclconfig-sh-file $p/$libpath/tclConfig.sh]
			}
		}
	}
}

proc parse-tclconfig-sh-file {filename} {
	foreach line [split [readfile $filename] \n] {
		if {[regexp {^(TCL_[^=]*)=(.*)$} $line -> name value]} {
			set value [regsub -all {\$\{.*\}} $value ""]
			set tclconfig($name) [string trim $value ']
		}
	}
	return [array get tclconfig]
}

# The complex extension checking is done here.

global withinfo
global extdb

# Final determination of module status
dict set extdb status {}

# Returns 1 if the extension has the attribute
proc ext-has {ext attr} {
    expr {$attr in [dict get $::extdb attrs $ext]}
}

# Returns an entry from the extension 'info' table, or $default otherwise
proc ext-get {ext key {default {}}} {
    if {[dict exists $::extdb info $ext $key]} {
        return [dict get $::extdb info $ext $key]
    } else {
        return $default
    }
}

# Set the status of the extension to the given value, and returns the value
proc ext-set-status {ext value} {
    dict set ::extdb status $ext $value
    return $value
}

# Returns the status of the extension, or ? if unknown
proc ext-get-status {ext} {
    if {[dict exists $::extdb status $ext]} {
        return [dict get $::extdb status $ext]
    }
    return ?
}

proc check-extension-status {ext required} {
    global withinfo

    set status [ext-get-status $ext]

    if {$ext in $withinfo(without)} {
        # Disabled without further ado
        msg-result "Extension $ext...disabled"
        return [ext-set-status $ext n]
    }

    if {$status in {m y n}} {
        return $status
    }

    # required is "required" if this extension *must* be enabled
    # required is "wanted" if it is not fatal for this extension
    # not to be enabled

    array set depinfo {m 0 y 0 n 0}

    # Check direct dependencies
    if [ext-get $ext check 1] {
        # "check" conditions are met
    } else {
        # not met
        incr depinfo(n)
    }

    if {$depinfo(n) == 0} {
        # Now extension dependencies
        foreach i [ext-get $ext dep] {
            set status [check-extension-status $i $required]
            #puts "$ext: dep $i $required => $status"
            incr depinfo($status)
            if {$depinfo(n)} {
                break
            }
        }
    }

    #parray depinfo

    if {$depinfo(n)} {
        msg-checking "Extension $ext..."
        if {$required eq "required"} {
            user-error "dependencies not met"
        }
        msg-result "disabled (dependencies)"
        return [ext-set-status $ext n]
    }

    # Selected as a module?
    if {$ext in $withinfo(mod)} {
        if {[ext-has $ext tcl]} {
            # Easy, a Tcl module
            msg-result "Extension $ext...tcl"
        } elseif {[ext-has $ext static]} {
            user-error "Extension $ext can't be a module"
        } else {
            msg-result "Extension $ext...module"
            foreach i [ext-get $ext libdep] {
                define-append LDLIBS_$ext [get-define $i ""]
            }
        }
        return [ext-set-status $ext m]
    }

    # Selected as a static extension?
    if {[ext-has $ext shared]} {
        user-error "Extension $ext can only be selected as a module"
    } elseif {$ext in $withinfo(ext) || $required eq "$required"} {
        msg-result "Extension $ext...enabled"
    } elseif {$ext in $withinfo(maybe)} {
        msg-result "Extension $ext...enabled (default)"
    } else {
        # Could be selected, but isn't (yet)
        return [ext-set-status $ext x]
    }
    foreach i [ext-get $ext libdep] {
        define-append LDLIBS [get-define $i ""]
    }
    return [ext-set-status $ext y]
}

# Examines the user options (the $withinfo array)
# and the extension database ($extdb) to determine
# what is selected, and in what way.
#
# The results are available via ext-get-status
# And a dictionary is returned containing four keys:
#   static-c     extensions which are static C
#   static-tcl   extensions which are static Tcl
#   module-c     extensions which are C modules
#   module-tcl   extensions which are Tcl modules
proc check-extensions {} {
    global extdb withinfo

    # Check valid extension names
    foreach i [concat $withinfo(ext) $withinfo(mod)] {
        if {![dict exists $extdb attrs $i]} {
            user-error "Unknown extension: $i"
        }
    }

    set extlist [lsort [dict keys [dict get $extdb attrs]]]

    set withinfo(maybe) {}

    # Now work out the default status. We have.
    # normal case, include !optional if possible
    # --without=default, don't include optional
    if {$withinfo(nodefault)} {
        lappend withinfo(maybe) stdlib
    } else {
        foreach i $extlist {
            if {![ext-has $i optional]} {
                lappend withinfo(maybe) $i
            }
        }
    }

    foreach i $extlist {
        define LDLIBS_$i ""
    }

    foreach i [concat $withinfo(ext) $withinfo(mod)] {
        check-extension-status $i required
    }
    foreach i $withinfo(maybe) {
        check-extension-status $i wanted
    }

    array set extinfo {static-c {} static-tcl {} module-c {} module-tcl {}}

    foreach i $extlist {
        set status [ext-get-status $i]
        set tcl [ext-has $i tcl]
        switch $status,$tcl {
            y,1 {
                define jim_ext_$i
                lappend extinfo(static-tcl) $i
            }
            y,0 {
                define jim_ext_$i
                lappend extinfo(static-c) $i
                # If there are any static C++ extensions, jimsh must be linked using
                # the C++ compiler
                if {[ext-has $i cpp]} {
                    define HAVE_CXX_EXTENSIONS
                }
            }
            m,1 { lappend extinfo(module-tcl) $i }
            m,0 { lappend extinfo(module-c) $i }
        }
    }
    return [array get extinfo]
}
