# This is a wrapper, permitting overriding of the MAKE parameters

# Are we doing a cross-compile for Windows?  Set to '.w32' if we are:
cross=

# Are we doing a clean?  Set to 'clean' if so:
clean=

# Are we doing all platforms?
doall=0

postbuild()
{
	echo "Finished build"
}

die()
{
	echo $1
	exit 1
}

domake()
{
	optsfile="make.opts${cross}"
	(
		if [ -f $optsfile ]
		then
			. $optsfile
		fi

		make -f Makefile${cross} ${clean} || die "Could not build!"
	)

	if [ "$clean" != "clean" ]
	then
		postbuild
	fi
}

syntax()
{
cat <<EOF
OPTIONS:

  make.sh [help] [cross] [all] [clean]

The options are position-insensitive and additive:

  'help'  displays this text
  'cross' does a cross-compile for Windows
  'all'   does a regular build as well as a cross-compile
  'clean' cleans rather than builds

For example:

   make.sh cross clean

Will clean the Windows cross-compiled build.  Giving no options will make the
script do a 'regular' build.

FILES:

In order to override the defaults, you may create a file "make.opts" and/or
"make.opts.w32".  These are normal "bash" scripts, in which you override whatever
behavior you want.  For example, you might override the "postbuild" function so
that you create a ZIP file after compilation.  Or you can export a new TCC
variable to override the standard default values.  See "Makefile" for values you
might be interested in overriding.
EOF
exit 1
}


# process command-line options:

while [ "$1" != "" ]
do
	case $1 in
		cross)
			cross='.w32'
		;;

		all)
			doall=1
		;;

		clean)
			clean='clean'
		;;
		help|*) 
			syntax
		;;
	esac
	shift
done

# Do what needs to be done!
if [ "$doall" == "1" ]
then
	cross=
	domake
	cross='.w32'
	domake
else
	domake
fi
