REM Based on /wiki/Release%20Build%20How-To
nmake /f Makefile.msc FOSSIL_ENABLE_SSL=1 OPTIMIZATIONS=0 DEBUG=1 clean fossil.exe
@echo off
echo:
echo ********************************************************
echo:
echo Now run: "set FOSSIL_BREAK=1"
echo Then run: "fossil"
echo Then attach VisualStudio to the process
echo Then press ENTER
