@ECHO OFF

::
:: buildmsvc.bat --
::
:: This batch file attempts to build Fossil using the latest version
:: Microsoft Visual Studio installed on this machine.
::
::

SETLOCAL

REM SET __ECHO=ECHO
REM SET __ECHO2=ECHO
IF NOT DEFINED _AECHO (SET _AECHO=REM)
IF NOT DEFINED _CECHO (SET _CECHO=REM)
IF NOT DEFINED _VECHO (SET _VECHO=REM)

REM
REM Visual Studio ????
REM
IF DEFINED VSVARS32 IF EXIST "%VSVARS32%" (
  %_AECHO% Build environment batch file manually overridden to "%VSVARS32%"...
  GOTO skip_detectVisualStudio
)

REM
REM Visual Studio 2013
REM
IF NOT DEFINED VS120COMNTOOLS GOTO skip_detectVisualStudio2013
SET VSVARS32=%VS120COMNTOOLS%\vsvars32.bat
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2013...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2013

REM
REM Visual Studio 2012
REM
IF NOT DEFINED VS110COMNTOOLS GOTO skip_detectVisualStudio2012
SET VSVARS32=%VS110COMNTOOLS%\vsvars32.bat
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2012...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2012

REM
REM Visual Studio 2010
REM
IF NOT DEFINED VS100COMNTOOLS GOTO skip_detectVisualStudio2010
SET VSVARS32=%VS100COMNTOOLS%\vsvars32.bat
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2010...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2010

REM
REM Visual Studio 2008
REM
IF NOT DEFINED VS90COMNTOOLS GOTO skip_detectVisualStudio2008
SET VSVARS32=%VS90COMNTOOLS%\vsvars32.bat
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2008...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2008

REM
REM Visual Studio 2005
REM
IF NOT DEFINED VS80COMNTOOLS GOTO skip_detectVisualStudio2005
SET VSVARS32=%VS80COMNTOOLS%\vsvars32.bat
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2005...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2005

REM
REM Visual Studio 2003
REM
IF NOT DEFINED VS71COMNTOOLS GOTO skip_detectVisualStudio2003
SET VSVARS32=%VS71COMNTOOLS%\vsvars32.bat
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2003...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2003

REM
REM Visual Studio 2002
REM
IF NOT DEFINED VS70COMNTOOLS GOTO skip_detectVisualStudio2002
SET VSVARS32=%VS70COMNTOOLS%\vsvars32.bat
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2002...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2002

REM
REM NOTE: If we get to this point, no Visual Studio build environment batch
REM       files were found.
REM
ECHO No Visual Studio build environment batch files were found.
GOTO errors

REM
REM NOTE: At this point, the appropriate Visual Studio version should be
REM       selected.
REM
:skip_detectVisualStudio

REM
REM NOTE: Remove any double-backslash sequences that may be present in the
REM       selected Visual Studio common tools path.  This is not strictly
REM       necessary; however, it makes reading the output easier.
REM
SET VSVARS32=%VSVARS32:\\=\%

%_VECHO% VsVars32 = '%VSVARS32%'

REM
REM NOTE: Setup local environment variables that point to the root directory
REM       of the Fossil source checkout and to the directory containing this
REM       build tool.
REM
SET ROOT=%~dp0\..
SET ROOT=%ROOT:\\=\%

SET TOOLS=%~dp0
SET TOOLS=%TOOLS:~0,-1%

%_VECHO% Root = '%ROOT%'
%_VECHO% Tools = '%TOOLS%'

REM
REM NOTE: After this point, a clean ERRORLEVEL is required; therefore, make
REM       sure it is reset now.
REM
CALL :fn_ResetErrorLevel

REM
REM NOTE: Attempt to call the selected batch file to setup the environment
REM       variables for building with MSVC.
REM
%__ECHO3% CALL "%VSVARS32%"

IF ERRORLEVEL 1 (
  ECHO Visual Studio build environment batch file "%VSVARS32%" failed.
  GOTO errors
)

REM
REM NOTE: Attempt to create the build output directory, if necessary.
REM
IF NOT EXIST "%ROOT%\msvcbld" (
  %__ECHO% MKDIR "%ROOT%\msvcbld"

  IF ERRORLEVEL 1 (
    ECHO Could not make directory "%ROOT%\msvcbld".
    GOTO errors
  )
)

REM
REM NOTE: Attempt to change to the created build output directory so that
REM       the generated files will be placed there.
REM
%__ECHO2% PUSHD "%ROOT%\msvcbld"

IF ERRORLEVEL 1 (
  ECHO Could not change to directory "%ROOT%\msvcbld".
  GOTO errors
)

REM
REM NOTE: Attempt to execute NMAKE for the Fossil MSVC makefile, passing
REM       anything extra from our command line along (e.g. extra options).
REM
%__ECHO% nmake /f "%TOOLS%\Makefile.msc" %*

IF ERRORLEVEL 1 (
  GOTO errors
)

REM
REM NOTE: Attempt to restore the previously saved directory.
REM
%__ECHO2% POPD

IF ERRORLEVEL 1 (
  ECHO Could not restore directory.
  GOTO errors
)

GOTO no_errors

:fn_ResetErrorLevel
  VERIFY > NUL
  GOTO :EOF

:fn_SetErrorLevel
  VERIFY MAYBE 2> NUL
  GOTO :EOF

:usage
  ECHO.
  ECHO Usage: %~nx0 [...]
  ECHO.
  GOTO errors

:errors
  CALL :fn_SetErrorLevel
  ENDLOCAL
  ECHO.
  ECHO Build failure, errors were encountered.
  GOTO end_of_file

:no_errors
  CALL :fn_ResetErrorLevel
  ENDLOCAL
  ECHO.
  ECHO Build success, no errors were encountered.
  GOTO end_of_file

:end_of_file
%__ECHO% EXIT /B %ERRORLEVEL%
