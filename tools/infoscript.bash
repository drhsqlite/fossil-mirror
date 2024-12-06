#!/usr/bin/env bash
# v0.1 Quick and dirty script to grab fossil's help and stuff it into a file.
# v0.2 Now I have working sed expressions, I can grab the help keywords
# v0.3 This gets me the keywords and only the keywords.
# v0.4 Glue all relevant things together and produce an info file
# v0.5 Working out sed/head/tail differences between GNU and BSD
# v0.6 Replaced "head -n -1" with "sed '$d'" which does the same thing.
# v0.7 Adding help in separately for normal/auxil commands
# v0.8 Added common options and Features. Got rid of some obvious bugs.
#        Builds (finally) on OpenBSD.
#
# Requires fossil, tail, GNU sed and makeinfo
#
# TODO: figure out how to replace one line with two portably

##### Header #####
# put header, then common node, finish menu, then uncommon node then finish menu
echo "Create Header"
printf "\\input texinfo
@settitle Fossil
@setfilename fossil.info
@c @author brickviking

" > fossil.texi
printf "@c Initial rendition to convert fossil help -a -v into texinfo for further
@c massaging by makeinfo. Scripts to do this automatically may come
@c later. Don't expect this to conform to GNU guidelines.

" >> fossil.texi

# Check fossil version. Only thing wrong with this is which fossil binary is picked up first.
fossil version | sed 's/This is fossil version /@set VERSION /' | cut -c1-17  >> fossil.texi

printf "@node Top
@top Fossil - a distributed version control system

Fossil is a distributed version control system (DVCS) with built-in
forum, wiki, ticket tracker, CGI/HTTP interface, and HTTP server.
This file documents version @value{VERSION} of Fossil.

@c     @node menu
@menu
* Features::             Features listed from the website front page.
* Common commands::      These are the commands that are most likely to be used.
* Uncommon commands::    These aren't used as often, but they're still there when needed.
* Common arguments::     -o arguments common to all commands
* License::              The license agreement of the fossil project
@end menu

You can find the help to all of the available fossil commands by using 

@example
fossil help some-command
@end example

This will show you help on some-command. I've attempted to put all the available
help nodes from fossil into here, but it's vaguely possible I've missed some.

" >> fossil.texi

# Add in the Features from the front webpage
printf "@node Features
@chapter Features

Features as described on the fossil home page.

1.  Integrated Bug Tracking, Wiki, Forum, and Technotes - In addition to doing distributed version control like Git and Mercurial, Fossil also supports bug tracking, wiki, forum, and tech-notes.

2.  Built-in Web Interface - Fossil has a built-in and intuitive web interface that promotes project situational awareness. Type \"fossil ui\" and Fossil automatically opens a web browser to a page that shows detailed graphical history and status information on that project.

3.   Self-Contained  -  Fossil is a single self-contained stand-alone executable. To install, simply download a precompiled binary for Linux, Mac, OpenBSD, or Windows and put it on your  \$PATH. Easy-to-compile source code is available for users on other platforms.

4.  Simple Networking - No custom protocols or TCP ports. Fossil uses plain old HTTP (or HTTPS or SSH) for all network communications, so it works fine from behind restrictive firewalls, including proxies. The protocol is bandwidth efficient to the point that Fossil can be used comfortably over dial-up or over the exceedingly slow Wifi on airliners.

5.  CGI/SCGI Enabled - No server is required, but if you want to set one up, Fossil supports four easy server configurations.

6.   Autosync  -  Fossil supports \"autosync\" mode which helps to keep projects moving forward by reducing the amount of needless forking and merging often associated with distributed projects.

7.  Robust & Reliable - Fossil stores content using an enduring file format in an SQLite database so that transactions are atomic even if interrupted by a power loss or system crash. Automatic self-checks verify that all aspects of the repository are consistent prior to each commit.

8.  Free and Open-Source - Uses the 2-clause BSD license.

" >> fossil.texi

###### Common commands
printf "@node Common commands
@chapter Common commands

These are the commands you're most likely to use as a fossil user. They
include most of the common commands you'd be used to in other VCS
programs such as subversion, git or mercurial.

" >> fossil.texi

# begin menu for common keywords
echo "@menu" >> fossil.texi

# Slurp in Common keywords from fossil help
# WARNING: tail count is brittle
echo "Grab common keywords"
for u in $(for t in $(fossil help | tail -n +4| sed '$d'); do echo "$t"; done | sort);  do echo "* ${u}::"; done >> fossil.texi

# Add end menu, add some space too
echo -ne "@end menu

" >> fossil.texi

# Add in the actual help for common commands
# WARNING: tail count is brittle
# tail command pops off the first six lines, sed commands remove the last two lines.
echo "Fossil output common help to workfile"
fossil help -v | tail -n +7 | sed '$d' | sed '$d' >workfile

# swap out @ with @@ so texinfo doesn't barf
echo "Doubling up the @'s"
sed -i -e 's/@/@@/g' workfile

echo "Swapping out # for @node ... \n@unnumbered ..."
# This swaps out "# keyword" with 
# @node keyword
# @unnumbered keyword
# breaks on *BSD's seds, needs gsed there

# Check the OS so we can use gsed if needed
MYOS="$(uname )"
if [[ ${MYOS} == "Linux" ]]; then
    sed -i -e 's/^##* \([a-z0-9-]\{1,\}\)/@node \1\n@section \1\n/' workfile
else
    # We'll assume we're on a BSD here, even though this won't always be true
    gsed -i -e 's/^##* \([a-z0-9-]\{1,\}\)/@node \1\n@section \1\n/' workfile
fi

# turns --switches into @option{--switches}
sed -i -e 's/--\([[:alnum:]-]\{1,\}\)/@option\{--\1\}/g' workfile
# now do the same for places where there's -f|--force
sed -i -e 's/|--\([[:alnum:]-]\{1,\}\)/|@option\{--\1\}/g' workfile
# turns -switches into @option{-switches}. Usually starts with space
sed -i -e 's/ -\([[:alnum:]-]\{1,\}\)/ @option\{-\1\}/g' workfile
# ... and adds it to the output file with some space
cat workfile >> fossil.texi
echo "" >> fossil.texi

##### Uncommon commands ####
echo "@node Uncommon commands
@chapter Uncommon commands

These are the commands that aren't used quite as often, and are normally
used in specific circumstances. You'll use these if you create fossils
suitable for hosting, among other things.

@menu
" >> fossil.texi

# Slurp in auxiliary/uncommon keywords - no need to remove last line here
echo "Grab uncommon keywords"
for u in $(for t in $(fossil help -x); do echo "$t"; done | sort); do echo "* ${u}::"; done >> fossil.texi

echo "@end menu" >> fossil.texi
echo "" >> fossil.texi

# Now add all the help from "fossil help -x -v"
# WARNING: tail count is brittle
# sed comands remove the last two lines.
echo "Fossil output auxiliary help to workfile"
fossil help -x -v | tail -n +4 | sed '$d' | sed '$d' >workfile

# swap out @ with @@ so texinfo doesn't barf
echo "Doubling up the @'s"
sed -i -e 's/@/@@/g' workfile

# This swaps out "# keyword" with 
# @node keyword
# @unnumbered keyword
# breaks on *BSD's seds, needs gsed there
echo "Swapping out # for @node ... \n@unnumbered ..."

if [[ ${MYOS} == "Linux" ]]; then
    sed -i -e 's/^##* \([a-z0-9-]\{1,\}\)/@node \1\n@section \1\n/' workfile
else
    # We'll assume we're on a BSD here, even though this won't always be true
    gsed -i -e 's/^##* \([a-z0-9-]\{1,\}\)/@node \1\n@section \1\n/' workfile
fi

# turns --switches into @option{--switches}
sed -i -e 's/--\([[:alnum:]-]\{1,\}\)/@option\{--\1\}/g' workfile

# ... and adds it to the output file with a spacer line
cat workfile >> fossil.texi
echo "" >> fossil.texi

# Add in common args
echo "Grab common args"
echo "@node Common arguments
@chapter Common arguments

These are the arguments that are common to all fossil commands.

" >> fossil.texi

# Slurp in auxiliary/uncommon keywords
echo "Grab common args text"
# At the moment, this doesn't do lines, and also requires GNU sed
if [[ ${MYOS} == "Linux" ]]; then
    fossil help -o | sed -e '2,$s/^  /\n/' -e '2,$s/--\([[:alnum:]-]\{1,\}\)/@option\{--\1\}/g' >> fossil.texi
else
    fossil help -o | gsed -e '2,$s/^  /\n/' -e '2,$s/--\([[:alnum:]-]\{1,\}\)/@option\{--\1\}/g' >> fossil.texi
fi
# stray stuff that didn't work
# sed -e '/--/s/--\(.*\s+\)/@item --\1 /' >> fossil.texi
echo "" >> fossil.texi

# Add in licence. Look for it in two places, just in case we're in tools/ when
# we call this program.
if [[ -f COPYRIGHT-BSD2.txt ]]; then
  HERE="COPYRIGHT-BSD2.txt"
elif [[ -f ../COPYRIGHT-BSD2.txt ]]; then
  HERE="../COPYRIGHT-BSD2.txt"
else
	echo "Where's COPYRIGHT-BSD2.txt?"
fi
# TODO: This should fail if we couldn't find COPYRIGHT-BSD2.txt
echo "
@node License
@chapter License agreement

@include ${HERE}

" >> fossil.texi

# Every good thing has to end
echo "@bye" >> fossil.texi

# and now we make the final info file - commented out for now
# makeinfo fossil.texi

echo "Done ... for now. Please check fossil.texi file over for inconsistencies, and fill in descriptions."

