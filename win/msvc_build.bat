@echo off

rem    this batch file tries to compile fossil using the latest available version
rem    of Microsoft Visual Studio that can be found on the machine.

rem visual studio 2013
SET vsvars32="%VS120COMNTOOLS%\vsvars32.bat"
rem visual studio 2012
IF NOT EXIST %vsvars32% SET vsvars32="%VS110COMNTOOLS%\vsvars32.bat"
rem visual studio 2010
IF NOT EXIST %vsvars32% SET vsvars32="%VS100COMNTOOLS%\vsvars32.bat"
rem visual studio 2008
IF NOT EXIST %vsvars32% SET vsvars32="%VS90COMNTOOLS%\vsvars32.bat"
rem visual studio 2005
IF NOT EXIST %vsvars32% SET vsvars32="%VS80COMNTOOLS%\vsvars32.bat"
rem visual studio 2003 .NET
IF NOT EXIST %vsvars32% SET vsvars32="%VS71COMNTOOLS%\vsvars32.bat"

rem check everything is correct
IF NOT EXIST %vsvars32% goto:bad_environment

rem setting environment variables for building with Microsoft Visual C++
call %vsvars32%

rem making build directory
pushd "%~dp0"
cd ..
mkdir msvc_build
cd msvc_build

rem building
nmake /f "%~dp0\Makefile.msc"

rem leaving
popd
pause
goto:eof

:bad_environment
echo "vsvars32.bat could not be found on this system."
pause
goto:eof
