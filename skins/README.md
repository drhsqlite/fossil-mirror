Built-in Skins
==============

Each subdirectory under this folder describes a built-in "skin".
There are five key files in each subdirectory:

  * `css.txt`	&rarr; The CSS for the skin
  * `details.txt` &rarr; Skin-specific settings
  * `footer.txt` &rarr; Text of the Content Footer for each page
  * `header.txt` &rarr; Text of the Content Header for each page
  * `js.txt` &rarr; Javascript included in the Content Footer

To improve an existing built-in skin, simply edit the appropriate
files and recompile.

To add a new skin:

   1.   Create a new subdirectory under skins/.  (The new directory is
        called "skins/newskin" below but you should use a new original
        name, of course.)

   2.   Add files skins/newskin/css.txt, skins/newskin/details.txt,
        skins/newskin/footer.txt, skins/newskin/header.txt, and
        skins/newskin/js.txt. Be sure to "fossil add" these files.

   3.   Go to the tools/ directory and rerun "tclsh makemake.tcl".  This
        step rebuilds the various makefiles so that they have dependencies
        on the skin files you just installed.

   4.   Edit the BuiltinSkin[] array near the top of the src/skins.c source
        file so that it describes and references the "newskin" skin.

   5.   Type "make" to rebuild.

See the [custom skin documentation](/doc/$CURRENT/www/customskin.md) for
more information.

Development Hints
-----------------

One way to develop a new skin is to copy the baseline files (css.txt,
details.txt, footer.txt, header.txt, and js.txt) into a working 
directory $WORKDIR then launch Fossil with a command-line option 
"--skin $WORKDIR".  Example:

        cp -r skins/default newskin
        fossil ui --skin ./newskin

When the argument to --skin contains one or more '/' characters, the
appropriate skin files are read from disk from the directory specified.
So after launching fossil as shown above, you can edit the newskin/*.txt
files using your favorite text editor, then press Reload on your browser
to see immediate results.
