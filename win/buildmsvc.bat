@ECHO OFF

::
:: buildmsvc.bat --
::
:: This batch file attempts to build Fossil using the latest version of
:: Microsoft Visual Studio installed on this machine.
::
:: For VS 2017 and later, it uses the x64 build tools by default;
:: pass "x86" as the first argument to use the x86 tools.
::

SETLOCAL

REM SET __ECHO=ECHO
REM SET __ECHO2=ECHO
IF NOT DEFINED _AECHO (SET _AECHO=REM)
IF NOT DEFINED _CECHO (SET _CECHO=REM)
IF NOT DEFINED _VECHO (SET _VECHO=REM)

REM
REM NOTE: Setup local environment variables that point to the root directory
REM       of the Fossil source checkout and to the directory containing this
REM       build tool.
REM
SET ROOT=%~dp0\..
SET ROOT=%ROOT:\\=\%

%_VECHO% Root = '%ROOT%'

SET TOOLS=%~dp0
SET TOOLS=%TOOLS:~0,-1%

%_VECHO% Tools = '%TOOLS%'

REM
REM Visual C++ ????
REM
IF DEFINED VCINSTALLDIR IF EXIST "%VCINSTALLDIR%" (
  %_AECHO% Build environment appears to be set up.
  GOTO skip_setupVisualStudio
)

REM
REM Visual Studio ????
REM
IF DEFINED VSVARS32 IF EXIST "%VSVARS32%" (
  %_AECHO% Build environment batch file manually overridden to "%VSVARS32%"...
  GOTO skip_detectVisualStudio
)

REM
REM Visual Studio 2017 / 2019 / 2022
REM
CALL :fn_TryUseVsWhereExe
IF NOT DEFINED VSWHEREINSTALLDIR GOTO skip_detectVisualStudio2017
SET VSVARS32=%VSWHEREINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat
IF "%~1" == "x86" (
  SET VSVARS32=%VSWHEREINSTALLDIR%\VC\Auxiliary\Build\vcvars32.bat
)
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2017 / 2019 / 2022...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2017

REM
REM Visual Studio 2015
REM
IF NOT DEFINED VS140COMNTOOLS GOTO skip_detectVisualStudio2015
SET VSVARS32=%VS140COMNTOOLS%\vsvars32.bat
IF EXIST "%VSVARS32%" (
  %_AECHO% Using Visual Studio 2015...
  GOTO skip_detectVisualStudio
)
:skip_detectVisualStudio2015

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
REM NOTE: After this point, the environment should already be setup for
REM       building with MSVC.
REM
:skip_setupVisualStudio

%_VECHO% VcInstallDir = '%VCINSTALLDIR%'

REM
REM NOTE: Attempt to create the build output directory, if necessary.
REM       In order to build using the current directory as the build
REM       output directory, use the following command before executing
REM       this tool:
REM
REM       SET BUILDDIR=%CD%
REM
IF DEFINED BUILDDIR (
  IF DEFINED BUILDSUFFIX (
    CALL :fn_FindVarInVar BUILDSUFFIX BUILDDIR

    IF ERRORLEVEL 1 (
      REM
      REM NOTE: The build suffix is already present, do nothing.
      REM
    ) ELSE (
      REM
      REM NOTE: The build suffix is not present, add it now.
      REM
      SET BUILDDIR=%BUILDDIR%%BUILDSUFFIX%
    )

    CALL :fn_ResetErrorLevel
  )
) ELSE (
  REM
  REM NOTE: By default, when BUILDDIR is unset, build in the "msvcbld"
  REM       sub-directory relative to the root of the source checkout.
  REM       This retains backward compatibility with third-party build
  REM       scripts, etc,
  REM
  SET BUILDDIR=%ROOT%\msvcbld%BUILDSUFFIX%
)

%_VECHO% BuildSuffix = '%BUILDSUFFIX%'
%_VECHO% BuildDir = '%BUILDDIR%'

IF NOT EXIST "%BUILDDIR%" (
  %__ECHO% MKDIR "%BUILDDIR%"

  IF ERRORLEVEL 1 (
    ECHO Could not make directory "%BUILDDIR%".
    GOTO errors
  )
)

REM
REM NOTE: Attempt to change to the created build output directory so that
REM       the generated files will be placed there, if needed.
REM
%__ECHO2% PUSHD "%BUILDDIR%"

IF ERRORLEVEL 1 (
  ECHO Could not change to directory "%BUILDDIR%".
  GOTO errors
)

SET NEED_POPD=1

REM
REM NOTE: If requested, setup the build environment to refer to the Windows
REM       SDK v7.1A, which is required if the binaries are being built with
REM       Visual Studio 201x and need to work on Windows XP.
REM
IF DEFINED USE_V110SDK71A (
  %_AECHO% Forcing use of the Windows SDK v7.1A...
  CALL :fn_UseV110Sdk71A
)

%_VECHO% Path = '%PATH%'
%_VECHO% Include = '%INCLUDE%'
%_VECHO% Lib = '%LIB%'
%_VECHO% Tools = '%TOOLS%'
%_VECHO% Root = '%ROOT%'
%_VECHO% NmakeArgs = '%NMAKE_ARGS%'

REM
REM NOTE: Attempt to execute NMAKE for the Fossil MSVC makefile, passing
REM       anything extra from our command line along (e.g. extra options).
REM       Also, pass the base directory of the Fossil source tree as this
REM       allows an out-of-source-tree build.
REM
%__ECHO% nmake /f "%TOOLS%\Makefile.msc" B="%ROOT%" %NMAKE_ARGS% %*

IF ERRORLEVEL 1 (
  GOTO errors
)

REM
REM NOTE: Attempt to restore the previously saved directory, if needed.
REM
IF DEFINED NEED_POPD (
  %__ECHO2% POPD

  IF ERRORLEVEL 1 (
    ECHO Could not restore directory.
    GOTO errors
  )

  CALL :fn_UnsetVariable NEED_POPD
)

GOTO no_errors

:fn_UseV110Sdk71A
  IF "%PROCESSOR_ARCHITECTURE%" == "x86" GOTO set_v110Sdk71A_x86
  SET PFILES_SDK71A=%ProgramFiles(x86)%
  GOTO set_v110Sdk71A_done
  :set_v110Sdk71A_x86
  SET PFILES_SDK71A=%ProgramFiles%
  :set_v110Sdk71A_done
  SET PATH=%PFILES_SDK71A%\Microsoft SDKs\Windows\7.1A\Bin;%PATH%
  SET INCLUDE=%PFILES_SDK71A%\Microsoft SDKs\Windows\7.1A\Include;%INCLUDE%
  IF "%PLATFORM%" == "x64" GOTO set_v110Sdk71A_lib_x64
  SET LIB=%PFILES_SDK71A%\Microsoft SDKs\Windows\7.1A\Lib;%LIB%
  GOTO set_v110Sdk71A_lib_done
  :set_v110Sdk71A_lib_x64
  SET LIB=%PFILES_SDK71A%\Microsoft SDKs\Windows\7.1A\Lib\x64;%LIB%
  :set_v110Sdk71A_lib_done
  CALL :fn_UnsetVariable PFILES_SDK71A
  SET NMAKE_ARGS=%NMAKE_ARGS% FOSSIL_ENABLE_WINXP=1
  GOTO :EOF

:fn_FindVarInVar
  IF NOT DEFINED %1 GOTO :EOF
  IF NOT DEFINED %2 GOTO :EOF
  SETLOCAL
  CALL :fn_UnsetVariable VALUE
  SET __ECHO_CMD=ECHO %%%2%% ^^^| FIND /I "%%%1%%"
  FOR /F "delims=" %%V IN ('%__ECHO_CMD%') DO (
    SET VALUE=%%V
  )
  IF DEFINED VALUE (
    CALL :fn_SetErrorLevel
  ) ELSE (
    CALL :fn_ResetErrorLevel
  )
  ENDLOCAL
  GOTO :EOF

:fn_UnsetVariable
  SETLOCAL
  SET VALUE=%1
  IF DEFINED VALUE (
    SET VALUE=
    ENDLOCAL
    SET %VALUE%=
  ) ELSE (
    ENDLOCAL
  )
  CALL :fn_ResetErrorLevel
  GOTO :EOF

:fn_ResetErrorLevel
  VERIFY > NUL
  GOTO :EOF

:fn_SetErrorLevel
  VERIFY MAYBE 2> NUL
  GOTO :EOF

:fn_TryUseVsWhereExe
  IF DEFINED VSWHERE_EXE GOTO skip_setVsWhereExe
  SET VSWHERE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
  IF NOT EXIST "%VSWHERE_EXE%" SET VSWHERE_EXE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe
  :skip_setVsWhereExe
  IF NOT EXIST "%VSWHERE_EXE%" (
    %_AECHO% The "VsWhere" tool does not appear to be installed.
    GOTO :EOF
  )
  SET VS_WHEREIS_CMD="%VSWHERE_EXE%" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest
  IF DEFINED __ECHO (
    %__ECHO% %VS_WHEREIS_CMD%
    REM
    REM NOTE: This will not be executed, any reasonable fake path will work.
    REM
    SET VSWHEREINSTALLDIR=C:\Program Files\Microsoft Visual Studio\2017\Community
    GOTO skip_setVsWhereInstallDir
  )
  FOR /F "delims=" %%D IN ('%VS_WHEREIS_CMD%') DO (SET VSWHEREINSTALLDIR=%%D)
  :skip_setVsWhereInstallDir
  %_VECHO% VsWhereInstallDir = '%VSWHEREINSTALLDIR%'
  IF NOT DEFINED VSWHEREINSTALLDIR (
    %_AECHO% Visual Studio 2017 / 2019 / 2022 is not installed.
    GOTO :EOF
  )
  %_AECHO% Visual Studio 2017 / 2019 / 2022 is installed.
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
