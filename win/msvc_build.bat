@echo off

rem getting 32-bit program files directory
SET pf_32bit=%programfiles(x86)%
IF "%pf_32bit%"=="" SET pf_32bit=%programfiles%

rem getting vcvarsall.bat path for the latest version of visual studio that is available on the system
SET msvc2013=Microsoft Visual Studio 12.0
SET msvc2012=Microsoft Visual Studio 11.0
SET msvc2010=Microsoft Visual Studio 10.0
SET msvc2008=Microsoft Visual Studio 9.0
SET msvc2005=Microsoft Visual Studio 8

rem Microsoft Visual Studio .NET 2003 does not have vcvarsall.bat
                         SET vcvarsall="%pf_32bit%\%msvc2013%\VC\vcvarsall.bat"
IF NOT EXIST %vcvarsall% SET vcvarsall="%pf_32bit%\%msvc2012%\VC\vcvarsall.bat"
IF NOT EXIST %vcvarsall% SET vcvarsall="%pf_32bit%\%msvc2010%\VC\vcvarsall.bat"
IF NOT EXIST %vcvarsall% SET vcvarsall="%pf_32bit%\%msvc2008%\VC\vcvarsall.bat"
IF NOT EXIST %vcvarsall% SET vcvarsall="%pf_32bit%\%msvc2005%\VC\vcvarsall.bat"

rem check everything is correct
IF NOT EXIST %vcvarsall% goto:bad_environment

rem making build directory
pushd "%~dp0"
cd ..
mkdir msvc_build
cd msvc_build

rem setting environment variables for building with Microsoft Visual C++
call %vcvarsall%

rem building
nmake /f "%~dp0\Makefile.msc"
popd
pause
goto:eof

:bad_environment
echo "vcvarsall.bat could not be found on this system."
pause
goto:eof
