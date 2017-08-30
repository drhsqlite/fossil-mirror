# For this project, disable the pager for --help and --ref
# The user can still enable by using --nopager=0 or --disable-nopager
dict set autosetup(optdefault) nopager 1

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
