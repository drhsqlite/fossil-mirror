Using IDE for Fossil Development
================================

Overview
--------

IDE is not required to build Fossil. However, IDE may be useful in development
of the Fossil code. Staple IDE features that come to mind are ease of editing,
static code analysis, source indexing, autocomplete, and integrated
debugging.

Who may benefit from using an IDE? In general sense, of course, anyone who is
used to a particular IDE. Specifically to Fossil development this could be:

 *  Developers who are __new to Fossil code__; IDE helps navigate definitions
    and implementations across the source files

 *  Developers who may be familiar with the code, yet would want to diagnose
    an issue that calls for __in-depth debugging__; IDE makes debugging
    somewhat more accessible

[Current Fossil build process](./makefile.wiki) is Makefile-based, which
extensively uses code-generation. It's flexible, yet as such it's not natively
fitting any specific IDE. This often represents a challenge for an IDE workflow
as it's trying to figure out the correct dependencies.

To assist with Fossil IDE workflow, there're currently two alternatives to
consider depending on IDE capabilities to support it:

 *  [Use CMake-based workflow](#cmake-workflow); in this case the Fossil's
    [CMakeLists.txt](/file/CMakeLists.txt) file is directly used as the IDE
    project file

 *  [Import the existing Makefile](#import-makefile) and then tweak the
    resulting project settings to properly reference the dependencies

The following sections describe both approaches as tested with a selection of
IDEs (not exhaustive).


> __USAGE NOTES__: To get most utility out of IDE in Fossil development, it's
> recommended to do a general development in _Release_ build configuration.
> In this case, compile errors would refer to original source files instead
> of the generated ones which actually get compiled. Clicking on such error
> message in the IDE output pane will bring up the source file and the
> offending line.
>
> This somewhat reduces chances of making edits in the generated files just
> to see the precious code lost as the files get re-generated during build.
>
> Switch to _Debug_ build configuration once in a need to chase that bug
> that is playing hard to get. The debugger is stepping through the actually
> compiled code, that is the editor will load the generated files from the
> build directory. While fixing the bugs, just be aware of where to make the
> edits.
>
> A simple rule of thumb is to keep only `<module>.c` source files open,
> closing the files `<module>_.c` (underscored) and `<module>.h` (header)
> which get generated in the build directory.


> __WARNING__: Edits to _generated_ files in the build directory will __NOT__
> survive a _Rebuild_, and the changes cannot be committed directly to the
> project repository.
>
> Make sure you edit files in the Fossil __source__ directories.


<a name="cmake-workflow"></a>
CMake-based Fossil Workflow
---------------------------

Most major C/C++ IDEs handle CMake projects either natively or via native
project files generated from the given [CMakeLists.txt](/file/CMakeLists.txt).

So far the Fossil CMake-based workflow has been tried with the following IDEs:

 *  [Qt Creator](#cmake-qtc) (Linux, Windows)
 *  [VisualStudio 2017](#cmake-vs) (Windows)
 *  [Eclipse CDT](#cmake-eclipse) (Linux)
 *  [VSCode](#cmake-vscode) (Linux, Windows)
 *  [Code::Blocks](#cmake-cb) (Linux)


<a name="cmake-cmd"></a>
__Using CMake on the Command-line__

In general, the CMake workflow is very much the same on all of the supported
platforms. CMake generator takes care of the platform/compiler specifics.

CMake can be used directly on the command-line to build the Fossil project in
either Release or Debug configuration:

    cd fossil
    mkdir build-debug
    cd build-debug
    cmake .. -G"Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
    make

OR on Windows with MSVC:

    cmake .. -G"NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug
    nmake

An IDE executes similar commands behind the scenes to generate and build a
CMake-based project. In general, CMake is capable of generating native project
files for a wide selection of IDEs. However for the Fossil CMake project, it's
preferable to generate a __Makefiles__ build system, because that's what the
underlying Fossil build process is.

Below are the steps used to setup Fossil CMake project in the IDEs tested.


> __NOTE__: To refresh a _Project Tree View_ so that it shows the generated
> files, the easiest way is to re-save the `CMakeLists.txt` file. This
> triggers the IDE tools to regenerate the build system and rescan the project
> source files.


<a name="cmake-qtc"></a>
__Qt Creator__

CMake workflow in Qt Creator has been supported for quite some time, so it
evolved to become more streamlined. Below are the steps to set up CMake
project in Qt Creator version 4.5 and above.

1.  From Weclome panel _Open Project_, browse to the Fossil project directory
    and open the `CMakeLists.txt` file.
1.  Qt Creator will prompt to select a _Kit_ to configure the project.
    For example, `Desktop Qt 5.9.4 GCC 64bit`

At this point the project is already usable. Optionally, for convenience it
makes sense to adjust some of the default settings.

1.  Open the _Projects_ panel
1.  Remove the extra build configurations, keep Debug and Release
1.  For each of the build configurations

     *  Select a _Build directory_. For example: `build-fossil-debug/`
        directory or `build-debug/` subdirectory.
     *  Set any additional CMake variables

Now it's ready for build and use.

1.  Select the build configuration; for example `Debug`
1.  Do _Build Project "fossil"_; it runs the CMake and then starts the build
    showing the progress in the _Compile Output_ pane
1.  Refresh the _Projects Explorer View_ so that it shows the generated files:

     *  Open the `CMakeLists.txt` file and trivially "touch" it then re-save
        it.
     *  This triggers CMake which then re-populates the _Project Explorer View_
        adding the generated header files from the build directory.
1.  Navigate definitions/implementations. For example, in `src/main.c`
    function `version_cmd()` for the function `fossil_print()` select context
    menu _Follow Symbol Under Cursor_ -- the Fossil source file `src/printf.c`
    automatically opens in editor.
1.  Set the run arguments -- panel _Projects: Run_; by default the `app`
    target is already selected (`bin/fossil` executable):

      * Set the _Command line arguments:_ for example, `version`
      * Optionally, check _Run in terminal_
1.  Do _Run_ OR
1.  To start debugging -- do menu _Debug> Step Into_; the editor opens file
    `main_.c` (generated!!), execution stops in `main()`. Fossil executable
    output is shown in a separate terminal window.

> __WARNING__: Edits to _generated_ files in the build directory will __NOT__
> survive a _Rebuild_, and the changes cannot be committed directly to the
> project repository.
>
> Make sure you edit files in the Fossil __source__ directories.


<a name="cmake-vs"></a>
__Visual Studio 2017__

Starting with Visual Studio 2017 CMake-based projects (`CMakeLists.txt` files)
are directly supported without the need to generate an intermediate MSBuild
project files. This feature is supported via _Visual C++ Tools for CMake_
component which is installed by default as part of the
__Desktop development with C++__ workload.


> __NOTE__: By default, Visual Studio 2017 CMake Tools uses Ninja build tool.
> In general it has a better performance over the usual NMake, as it allows
> parallel build tasks. However in the scope of Fossil CMake project there's
> no real speedup, since the underlying Fossil Makefile is still built with
> NMake.
>
> As Ninja tool has some subtle differences in handling of the `clean` target,
> it's recommended to select the `"NMake Makefiles"` generator for the Fossil
> CMake project (which is a valid choice despite a possible warning.)


1.  Menu _File> Open:Folder_ to open Fossil source project directory that
    contains the `CMakeLists.txt` file
1.  Change the generator -- menu _CMake> Change CMake Settings> fossil_ will
    open `CMakeSettings.json` file; in all of the defined configurations set:

        "generator": "NMake Makefiles",
        "buildCommandArgs": "",
    Save the settings file and the project is reconfigured automatically.
1.  Select configuration -- toolbar _Select Project Settings_, for example:
    `x64-Debug`
1.  Do menu _CMake> Build All_; build progress is shown in _Output:Build_ pane
1.  Navigate definitions/implementations. For example, in `src/main.c`
    function `version_cmd()` for the function `fossil_print()` select context
    menu _Peek Definition_ -- the Fossil source file `src/printf.c`
    automatically opens the peek preview pane.
1.  Select a start target `bin/fossil.exe` -- toolbar _Select Startup Item_
1.  Set the run arguments -- menu _CMake> Debug and Launch Settings_ in the
    file `.vs/launch.vs.json`. For example, for _"default"_ configuration
    append:

        "args": ["version"]
    Save the settings file.
1.  To start debugging -- do menu _Debug> Step Into_; the editor opens file
    `main_.c` (generated!!), execution stops in `main()`. Fossil executable
    output is shown in a separate terminal window.


> __WARNING__: Edits to _generated_ files in the build directory will __NOT__
> survive a _Rebuild_, and the changes cannot be committed directly to the
> project repository.
>
> Make sure you edit files in the Fossil __source__ directories.


> __NOTE__: If doing a cross-platform build, it's necessary to clean the
> builds of the external components that are used with Fossil (in `compat/`
> subdirectory). For example, `zlib`, `openssl`. Failure to do that may
> manifest with the fireworks of compile and link errors.


<a name="cmake-eclipse"></a>
__Eclipse CDT__

To configure CMake-based project with Eclipse it first has to be generated as
a native project, then imported into Eclipse as an existing project.

The CMake build directory should __not__ be a subdirectory of the Fossil
project source directory, but rather a sibling or other directory. For example:

    workspace/
    |-- fossil-debug/              <== CMake build directory
    |-- fossil/                    <== Fossil project directory
    |   `-- CMakeLists.txt
    `-- fossil-release/

1.  Generate Eclipse CDT4 makefiles, optionally specify the Eclipse's version:

        cd workspace
        cd debug
        cmake ../fossil -G"Eclipse CDT4 - Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
             -DCMAKE_ECLIPSE_VERSION=3.8
        ls .cproject

1.  From Eclipse do _Import_ as a _General>Existing Projects into Workspace_
    from within the CMake build directory. This pulls both the Fossil
    CMake-based project and the original Makefiles-based project which appears
    under _[Subprojects]/fossil_
1.  Do _Build_ and then menu _Project> C/C++ Index: Rebuild_ to pick up the
    generated sources needed for the code browsing and autocomplete
1.  Navigate definitions/implementations. For example, in `src/main.c`
    function `version_cmd()` hover mouse over the call to `fossil_print()` --
    this allows to peek at its implementation. Or select _Open Declaration_
    from the context menu.
1.  For running and debugging, create a new _Run:Run Configuration_ for
    _C/C++ Application_; name it `fossil`, pick a project executable
    `bin/fossil` and specify run arguments. For example: `version`
1.  Do _Debug fossil_, which would switch to the Debug perspective and the
    editor opens file `main_.c` (generated!!), execution stops in `main()`


> __WARNING__: Edits to _generated_ files in the build directory will __NOT__
> survive a _Rebuild_, and the changes cannot be committed directly to the
> project repository.
>
> Make sure you edit files in the Fossil __source__ directories.


> __NOTE__: Eclipse creates a few local files and folders to keep its settings,
> (namely `.project`) which may already be part of Fossil, so make sure not to
> push them to the main Fossil repository.


<a name="cmake-vscode"></a>
__VSCode__

If you're a regular VSCode user, you probably have all the needed C/C++ and
CMake extensions already installed and set up. Otherwise, C/C++ and CMake
extensions need to be installed from the marketplace to enable CMake support.
For example:

1.  _C/C++_ ("C/C++", ms-vscode.cpptools)
1.  _CMake Tools_ ("CMake Tools")
1.  _CMake_ ("CMake For VisualStudio Code")

Then you may follow the CMake Tools "Getting Started" guide skipping to
"Configuring Your Project" section. It's also helpful to review "C/C++"
extension documentation for "Configuring launch.json for C/C++ debugging".

Briefly:

1.  Open the Fossil source project folder in VSCode:

        cd fossil
        code .

1.  _CMake Tools_ extension notification prompts to configure your project
1.  Select a _Kit_ from the shown list. For example, `GCC`
1.  _CMake Tools_ then executes CMake commands and by default configures the
    project to build in `build/` subdirectory with Debug configuration.
1.  In status bar set active target `app` (it's set to `[all]` by default)
1.  Build the project -- status-bar command _Build_
1.  Navigate definitions/implementations. For example, in `src/main.c` function
    `version_cmd()` for the function `fossil_print()` select context menu
    _Peek Definition_ -- the Fossil source file `src/printf.c` automatically
    opens the peek preview pane.
1.  Set the run arguments -- menu _Debug:Open Configurations_ for C/C++ in the
    file `.vscode/launch.json`. For example, for _"(gdb) Launch"_ configuration
    add:

        "program": "${workspaceFolder}/build/bin/fossil",
        "args": ["version"],
        "stopAtEntry": true,
        "cwd": "${workspaceFolder}/build",
    Save the file.
1.  Switch to the Debug view and _Start Debugging_; the editor opens file
    `main_.c` (generated!!), execution stops in `main()`


> __WARNING__: Edits to _generated_ files in the build directory will __NOT__
> survive a _Rebuild_, and the changes cannot be committed directly to the
> project repository.
>
> Make sure you edit files in the Fossil __source__ directories.


<a name="cmake-cb"></a>
__Code::Blocks__

1.  Create a build directory
1.  Generate the Code::Blocks project from the Fossil `CMakeLists.txt` file:

        mkdir build-debug
        cd build-debug
        cmake .. -G"CodeBlocks - Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug
        ls fossil.cbp

1.  Open the generated Code::Blocks project `.cbp` file
1.  Select and build `app` target (_Logs_ view shows _Build log_ output)
1.  Re-run the CMake command to refresh the project tree so it shows the
    generated source/include files:

        cmake ..

1.  Navigate source for definitions/implementations. For example, in `src/main.c`
    function `version_cmd()` find the implementation of `fossil_print()` -- the
    Fossil source file `src/printf.c` automatically opens in editor to show the
    function body.
1.  Set the run arguments -- menu _Project:Set program's arguments_ for target
    `app`. For example: `version`
1.  _Run_; OR
1.  For debugging select Menu _Debug:Step-in_; the editor opens file `main_.c`
    (generated!!), execution stops in `main()`


> __WARNING__: Edits to _generated_ files in the build directory will __NOT__
> survive a _Rebuild_, and the changes cannot be committed directly to the
> project repository.
>
> Make sure you edit files in the Fossil __source__ directories.


<a name="import-makefile"></a>
Import the Existing Fossil Makefile
-----------------------------------

Many IDEs allow importing an existing Makefile based project. However, not
all Makefiles are equally straightforward to import. Fossil's build process
involves quite extensive use of code-generation, which complicates dependency
resolution. The resulting IDE project file may turn up not very practical.
Additionally, such an imported project will need to be maintained manually or
re-imported to account for upstream updates in the Fossil project (additions,
renames etc.)

On the plus side, once the resulting Fossil IDE project has been configured,
it's as close to the original Makefile as it could be. Chances are the bulk of
dependencies would remain fairly static, as the Fossil project evolution shows.

So far adopting of the Fossil Makefile project has been tried with the
following IDEs:

 *  [Eclipse CDT](#import-eclipse) (Linux)
 *  [Visual Studio 2017](#import-vs) (Windows)

The general approach for adopting the existing Fossil Makefile into an IDE is
as follows:

1.  Configure the Fossil project to generate the main Makefile (Linux and
similar)

        ./configure --fossil-debug

1.  Import the existing Makefile project into via IDE's wizard or other
    facility
1.  Try to build the resulting project (to check the dependencies)
1.  Adjust the IDE project settings to resolve missing dependencies
1.  Test the code indexing whether definitions/implementations are resolved
    into correct sources
1.  Create and test the Debug configuration (defined FOSSIL_DEBUG preprocessor
    macro)

Below are the steps used to adopt Fossil Makefile project into the IDEs tested.


<a name="import-eclipse"></a>
__Eclipse CDT__

A configured Fossil Makefile-based project can be imported __as is__ into
Eclipse CDT. With a few tweaks, it allows code-browsing, autocomplete, and
debugging.

1.  Configure the Fossil project to generate the main Makefile (Linux and
    similar)

        ./configure --fossil-debug

1.  Do menu _File> Import> C/C++: Existing Code as Makefile Project_
1.  Browse to the Fossil project directory
1.  Pick the configured Toolchain, e.g. `Linux GCC`
1.  Once the import completed, the source tree is populated with source
    directories. At this point the project is already _buildable_, yet source
    browsing is not fully functional yet. Add the following settings:
1.  Do Menu _Project> Properties> C/C++ General: Paths and Symbols_
1.  Add _Includes_ in workspace folders: `fossil/bld`, `fossil/src`
1.  Add _Output Location_: `fossil/bld`
1.  Do _Build_ and then menu _Project> C/C++ Index: Rebuild_ to pick up the
    generated sources needed for the code browsing and autocomplete
1.  Navigate definitions/implementations. For example, in `src/main.c` function
    `version_cmd()` hover mouse over the call to `fossil_print()` -- this allows
    to peek at its implementation. Or select _Open Declaration_ from the context
    menu.
1.  For running and debugging, create a new _Run:Run Configuration_ for
    _C/C++ Application_; name it `fossil`, pick a project executable `bin/fossil`
    and specify run arguments. For example: `version`
1.  Do _Debug fossil_, which would switch to the Debug perspective and the editor
    opens file `main_.c` (generated!!), execution stops in `main()`


> __WARNING__: Edits to _generated_ files in the build directory will __NOT__
> survive a _Rebuild_, and the changes cannot be committed directly to the
> project repository.
>
> Make sure you edit files in the Fossil __source__ directories.


> __NOTE__: Eclipse creates a few local files and folders to keep its settings,
> (namely `.project`) which may already be part of Fossil, so make sure not to
> push them to the main Fossil repository.


<a name="import-vs"></a>
__Visual Studio 2017__

There're several ways of how to layout an imported project, here we show a
straightforward way that makes use of Visual Studio's import wizard.


> __NOTE__: In such a layout, the build directory `msvcbld` is shared between
> the build configurations. This requires a clean re-build when switching the
> build configuration or target platform in case of cross-compiling.


1.  Menu _File> New> Project From Existing Code_;  a wizard starts

        Project Type: Visual C++
        Project file location: <Fossil project source directory>
        Project name: fossil
        Folders:
            src/ (Add subfolders: Yes)
            win/ (Add subfolders: Yes)

        Project Settings: Use external build system

        Build command line (Debug): win\buildmsvc.bat FOSSIL_DEBUG=1
        Rebuild All command line (Debug):
        Clean command line (Debug): win\buildmsvc.bat FOSSIL_DEBUG=1 clean clean-zlib
        Output (Debug): msvcbld\fossil.exe
        Preprocessor definitions (Debug):
        Include Search Path (Debug): msvcbld;src;.;compat/zlib

        Build command line (Debug): win\buildmsvc.bat
        Rebuild All command line (Debug):
        Clean command line (Debug): win\buildmsvc.bat clean clean-zlib
        Output (Debug): msvcbld\fossil.exe
        Preprocessor definitions (Debug):
        Include Search Path (Debug): msvcbld;src;.;compat/zlib
    Apply the settings.
1.  New solution file `fossil.sln` and project file `fossil.vcxproj` are
    created in the Fossil project source directory
1.  Change the project build directory to `msvcbld` for All Configurations and
    All Platforms -- menu _Project> fossil Properties> General__

        Output Directory:  $(SolutionDir)mscvcbld\
        Intermediate Directory:  mscvcbld\
        Build Log File:  $(IntDir)$(MSBuildProjectName)-$(Configuration).log

1.  Select a target build configuration -- toolbar _Solution Configurations_,
    and _Solution Platforms_ (For example, `Debug`, `x64`)
1.  Do menu _Build> Build Solution_
1.  Navigate definitions/implementations. For example, in `src/main.c`
    function `version_cmd()` for the function `fossil_print()` select
    context menu _Peek Definition_ -- the Fossil source file `src/printf.c`
    automatically opens the peek preview pane.
1.  Set the run arguments -- menu _Project> fossil Properties: Debugging:
    Command arguments_. For example: `version`
1.  To start debugging -- do menu _Debug> Step Into_; the editor opens file
    `main_.c` (generated!!), execution stops in `main()`.
1.  Before switching to another build configuration, do menu
    _Build> Clean Solution_; then switch and build.


> __WARNING__: Edits to _generated_ files in the build directory will __NOT__
> survive a _Rebuild_, and the changes cannot be committed directly to the
> project repository.
>
> Make sure you edit files in the Fossil __source__ directories.


<a name="see-also"></a>
See Also
--------

 *  [Adding Features to Fossil](./adding_code.wiki)
 *  [The Fossil Build Process](./makefile.wiki)
