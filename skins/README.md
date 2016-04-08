Built-in Skins
==============

Each subdirectory under this folder describes a built-in "skin".
There are four files in each subdirectory for the CSS, the "details"
file, the footer, and the header for that skin.

To improve an existing built-in skin, simply edit the appropriate
files and recompile.

To add a new skin:

   1.   Create a new subdirectory under skins/.  (The new directory is
        called "skins/newskin" below but you should use a new original
        name, of course.)

   2.   Add files skins/newskin/css.txt, skins/newskin/details.txt,
        skins/newskin/footer.txt and skins/newskin/header.txt.
        Be sure to "fossil add" these files.

   3.   Go to the src/ directory and rerun "tclsh makemake.tcl".  This
        step rebuilds the various makefiles so that they have dependencies
        on the skin files you just installed.

   4.   Edit the BuiltinSkin[] array near the top of the src/skins.c source
        file so that it describes and references the "newskin" skin.

   5.   Type "make" to rebuild.

Development Hints
-----------------

One way to develop a new skin is to copy the baseline files (css.txt,
details.txt, footer.txt, and header.txt) into a working directory $WORKDIR
then launch Fossil with a command-line option "--skin $WORKDIR".  Example:

        cp -r skins/default newskin
        fossil ui --skin ./newskin

When the argument to --skin contains one or more '/' characters, the
appropriate skin files are read from disk from the directory specified.
So after launching fossil as shown above, you can edit the newskin/css.txt,
newskin/details.txt, newskin/footer.txt, and newskin/header.txt files using
your favorite text editor, then press Reload on your browser to see
immediate results.
