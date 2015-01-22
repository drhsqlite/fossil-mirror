Built-in Skins
==============

Each subdirectory under this folder describes a built-in "skin".
There are three files in each subdirectory for the CSS, the header,
and the footer for the skin.

To improve an existing built-in skin, simply edit the appropriate
files and recompile.

To add a new skin:

   1.   Create a new subdirectory under skins/.  (The new directory is
        called "skins/newskin" below but you should use a new original
        name, of course.)

   2.   Add files skins/newskin/css.txt, skins/newskin/header.txt,
        and skins/newskin/footer.txt.  Be sure to "fossil add" these files.

   3.   Go to the src/ directory and rerun "tclsh makemake.tcl".  This
        step rebuilds the various makefiles so that they have dependencies
        on the skin files you just installed.

   4.   Edit the BuiltinSkin[] array near the top of the src/skins.c source
        file so that it describes and references the "newskin" skin.

   5.   Type "make" to rebuild.
