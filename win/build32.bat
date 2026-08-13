REM Based on /wiki/Release%20Build%20How-To
nmake /f Makefile.msc FOSSIL_ENABLE_SSL=1 FOSSIL_ENABLE_WINXP=1 OPTIMIZATIONS=4 clean fossil.exe
dumpbin /dependents fossil.exe
