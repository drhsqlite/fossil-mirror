cmake_minimum_required(VERSION 2.8 FATAL_ERROR)

project(fossil C)
include(ExternalProject)

set(CMAKE_VERBOSE_MAKEFILE ON)


set(default_build_type "RelWithDebInfo")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
        STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
else()
    message(STATUS "'${CMAKE_BUILD_TYPE}' build type")
endif()

set(isDebugBuild FALSE)
if(CMAKE_BUILD_TYPE MATCHES "[Dd][Ee][Bb][Uu][Gg]" )
    set(isDebugBuild TRUE)
endif()

## VARIABLES ---------------------------------------------------------------
## Additional parameters can be passed in the following variables which can
## be defined on cmake command line (or in cache).
##
## The parameters correspond to the ones used in the external fossil build
## process (as specified at configure, make, and make test)
##
if(FOSSIL_CONFIGURE)
    message(STATUS "Fossil build configuration '${FOSSIL_CONFIGURE}'")
    set(configure_FLAGS ${FOSSIL_CONFIGURE})
endif()

if(FOSSIL_BUILD)
    message(STATUS "Fossil build variables '${FOSSIL_BUILD}'")
    set(build_FLAGS ${FOSSIL_BUILD})
endif()

if(FOSSIL_TEST)
    message(STATUS "Fossil test configuration '${FOSSIL_TEST}'")
    set(test_FLAGS ${FOSSIL_TEST})
endif()


add_executable(fossil-exe IMPORTED)

set(fossil_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(fossil_EXE_DIR "${fossil_INSTALL_DIR}/bin")
set(fossil_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(fossil_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/fossil)
if(MSVC)
    set(fossil_BUILD_DIR ${fossil_BINARY_DIR}/msvcbld)
else(MSVC)
    set(fossil_BUILD_DIR ${fossil_BINARY_DIR}/bld)
endif(MSVC)
set(fossil_TEST_DIR "${fossil_BINARY_DIR}/test")

set(EXE ${CMAKE_EXECUTABLE_SUFFIX})
set(fossil_EXE "${fossil_EXE_DIR}/fossil${EXE}")
file(TO_NATIVE_PATH "${fossil_EXE}" fossil_EXE)
set_target_properties(fossil-exe PROPERTIES
    IMPORTED_LOCATION "${fossil_EXE}"
)

## SETUP IDE ---------------------------------------------------------------
## Setup Project Sources View
## Add more source files for convenience; otherwise only actual dependencies
## would appear in the Projects tree pane.
##
file(GLOB_RECURSE project_sources_DIRS
    #"autosetup/*"   #may be useful when debuging configure
    "skins/*"
    "src/*"
    "test/*.test" "test/tester.tcl"
    "win/*"
    "www/*.wiki" "www/*.md" "www/*.html"
)

file(GLOB project_sources_FILES
    "auto.def"
    "configure"

    "fossil.1"
    "manifest.uuid"
    "VERSION"

    "${fossil_BINARY_DIR}/autoconfig.h"
    "${fossil_BINARY_DIR}/Makefile*"

    "${fossil_BUILD_DIR}/*.[h]"
    #"${fossil_BUILD_DIR}/main_.c"
)

include_directories(
    "${fossil_BUILD_DIR}"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}"
)

add_custom_target(project-sources ALL
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    SOURCES ${project_sources_DIRS} ${project_sources_FILES}
)

## CONFIGURE ---------------------------------------------------------------
##
set(fossil_configure_FLAGS "")
if(configure_FLAGS)
    set(fossil_configure_FLAGS ${configure_FLAGS})
endif()
if(isDebugBuild)
    if(MSVC)
        if(NOT "${fossil_configure_FLAGS}" MATCHES "FOSSIL_DEBUG")
            set(fossil_configure_FLAGS "FOSSIL_DEBUG=1" ${fossil_configure_FLAGS})
        endif()
    else(MSVC)
        if(NOT "${fossil_configure_FLAGS}" MATCHES "--fossil-debug")
            set(fossil_configure_FLAGS "--fossil-debug" ${fossil_configure_FLAGS})
        endif()
    endif(MSVC)
endif()

## BUILD ---------------------------------------------------------------
##
# Build fossil as an external project; just pass the options for configure and
# build stages. This way we rely upon the existing build scripts.
#
# Install the resulting binary into the local binary dir, such that the fossil
# binary can be located at the fixed location on target platform.
#
# Then we define CMake install targets to copy the binaries from the local
# binary dir.
#

set(fossil_build_FLAGS "")
if(build_FLAGS)
    set(fossil_build_FLAGS ${build_FLAGS})
endif()

separate_arguments(fossil_configure_FLAGS)
separate_arguments(fossil_build_FLAGS)

if(MSVC)
    file(RELATIVE_PATH fossil_DESTDIR "${fossil_BINARY_DIR}" "${fossil_EXE_DIR}")
    file(TO_NATIVE_PATH "${fossil_DESTDIR}" fossil_DESTDIR)

    ExternalProject_Add(fossil
        PREFIX fossil
        SOURCE_DIR "${fossil_SOURCE_DIR}"
        CONFIGURE_COMMAND ""
        BINARY_DIR "${fossil_BINARY_DIR}"
        BUILD_COMMAND "${fossil_SOURCE_DIR}/win/buildmsvc.bat"
                      ${fossil_configure_FLAGS}
                      ${fossil_build_FLAGS}
                      all
                      install DESTDIR=${fossil_DESTDIR}
                      VERBATIM
        INSTALL_COMMAND ""
        STEP_TARGETS configure build install
    )

else(MSVC)

    file(RELATIVE_PATH fossil_DESTDIR "${fossil_BINARY_DIR}" "${fossil_INSTALL_DIR}")

    ExternalProject_Add(fossil
        PREFIX fossil
        SOURCE_DIR "${fossil_SOURCE_DIR}"
        CONFIGURE_COMMAND "${fossil_SOURCE_DIR}/configure"
                           --prefix=/ ${fossil_configure_FLAGS}
                          VERBATIM
        BINARY_DIR "${fossil_BINARY_DIR}"
        BUILD_COMMAND "${CMAKE_MAKE_PROGRAM}"
                      ${fossil_build_FLAGS}
                      VERBATIM
        INSTALL_COMMAND "${CMAKE_MAKE_PROGRAM}" install
                        DESTDIR=${fossil_DESTDIR}
        STEP_TARGETS configure build install
    )

endif(MSVC)

set_property(DIRECTORY APPEND PROPERTY
    ADDITIONAL_MAKE_CLEAN_FILES "${fossil_BUILD_DIR}"
                                "${fossil_EXE_DIR}"
)

ExternalProject_Add_Step(fossil forcebuild
    COMMAND "${CMAKE_COMMAND}" -E echo "Build fossil"
    DEPENDEES configure
    DEPENDERS build
    ALWAYS 1
)

add_dependencies(fossil-exe fossil)


## We need an executable target, so that IDEs can infer what to run/debug.
## Define a substitute executable target to stand in for the actual fossil binary
## that we just built via ExternalProject_Add.
## Keep the actual fossil binary, clean up the built substitute artifacts.

get_filename_component(app_NAME "${fossil_EXE}" NAME_WE)
get_filename_component(app_DIR "${fossil_EXE}" DIRECTORY)
file(TO_NATIVE_PATH "${app_DIR}" app_DIR)

file(WRITE "${CMAKE_BINARY_DIR}/app.c" "int main(){ return 0; }\n")
add_executable(app app.c)

set_target_properties(app PROPERTIES
    OUTPUT_NAME "${app_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${app_DIR}"
)
set_target_properties(app PROPERTIES
    LINK_FLAGS "/INCREMENTAL:NO /MANIFEST:NO"
)
if(MSVC AND isDebugBuild)
    set_target_properties(app PROPERTIES
        PDB_NAME "${app_NAME}"
    )
endif()
add_custom_command(TARGET app PRE_LINK
    COMMAND "${CMAKE_COMMAND}" -E chdir  "$<TARGET_FILE_DIR:app>"
            "${CMAKE_COMMAND}" -E rename "$<TARGET_FILE_NAME:app>"
                                         "$<TARGET_FILE_NAME:app>-imported"
)
if(MSVC AND isDebugBuild)
    add_custom_command(TARGET app PRE_LINK
        COMMAND "${CMAKE_COMMAND}" -E chdir  "$<TARGET_FILE_DIR:app>"
                "${CMAKE_COMMAND}" -E rename "$<TARGET_PROPERTY:app,PDB_NAME>.pdb"
                                             "$<TARGET_PROPERTY:app,PDB_NAME>.pdb-imported"
    )
endif()
add_custom_command(TARGET app POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E chdir  "$<TARGET_FILE_DIR:app>"
            "${CMAKE_COMMAND}" -E remove "$<TARGET_FILE_NAME:app>"
    COMMAND "${CMAKE_COMMAND}" -E chdir  "$<TARGET_FILE_DIR:app>"
            "${CMAKE_COMMAND}" -E rename "$<TARGET_FILE_NAME:app>-imported"
                                         "$<TARGET_FILE_NAME:app>"
)
if(MSVC AND isDebugBuild)
    add_custom_command(TARGET app POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E chdir  "$<TARGET_FILE_DIR:app>"
                "${CMAKE_COMMAND}" -E rename "$<TARGET_PROPERTY:app,PDB_NAME>.pdb-imported"
                                             "$<TARGET_PROPERTY:app,PDB_NAME>.pdb"
    )
endif()

add_dependencies(app fossil-exe)


## TEST ---------------------------------------------------------------
##
add_custom_target(test-fossil-setup
    COMMENT "Setting up test-fossil"
    DEPENDS fossil-install
)
add_custom_command(TARGET test-fossil-setup PRE_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${fossil_SOURCE_DIR}/test" "${fossil_TEST_DIR}"
    WORKING_DIRECTORY "${fossil_BINARY_DIR}"
    COMMENT "Copying data for test-fossil"
    VERBATIM
)
add_custom_command(TARGET test-fossil-setup POST_BUILD
    COMMAND "${fossil_EXE}" new "${fossil_BINARY_DIR}/test-fossil.fsl"
            --admin-user testuser
    COMMAND "${fossil_EXE}" open "${fossil_BINARY_DIR}/test-fossil.fsl"
    COMMAND "${fossil_EXE}" user default testuser --user testuser
    WORKING_DIRECTORY "${fossil_TEST_DIR}"
    COMMENT "Setting up repository for test-fossil"
    VERBATIM
)


# Run the test suite.
# Other flags that can be included in TESTFLAGS are:
#
#  -halt     Stop testing after the first failed test
#  -keep     Keep the temporary workspace for debugging
#  -prot     Write a detailed log of the tests to the file ./prot
#  -verbose  Include even more details in the output
#  -quiet    Hide most output from the terminal
#  -strict   Treat known bugs as failures
#
# TESTFLAGS can also include names of specific test files to limit
# the run to just those test cases.

set(TCL "tclsh")
if( test_FLAGS )
    set(fossil_test_FLAGS ${test_FLAGS})
endif()

separate_arguments(fossil_test_FLAGS)
add_custom_target(test-fossil
    COMMAND "${TCL}" tester.tcl "${fossil_EXE}" -quiet ${fossil_test_FLAGS}
    VERBATIM
    WORKING_DIRECTORY "${fossil_TEST_DIR}"
    COMMENT "Running test-fossil"
    DEPENDS fossil-install
)

add_custom_target(test-fossil-clean
    COMMENT "Tearing down test-fossil"
    DEPENDS fossil-install
)
add_custom_command(TARGET test-fossil-clean PRE_BUILD
    COMMAND "${CMAKE_COMMAND}" -E remove "${fossil_BINARY_DIR}/test-fossil.fsl"
    WORKING_DIRECTORY "${fossil_TEST_DIR}"
    COMMENT "Removing repository for test-fossil"
    VERBATIM
)
add_custom_command(TARGET test-fossil-clean POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${fossil_TEST_DIR}"
    WORKING_DIRECTORY "${fossil_BINARY_DIR}"
    COMMENT "Removing data for test-fossil"
    VERBATIM
)

add_custom_target(clean-fossil
    COMMAND "${CMAKE_COMMAND}" -E remove_directory "${fossil_EXE_DIR}"
            VERBATIM
    COMMAND "${CMAKE_MAKE_PROGRAM}" clean
            VERBATIM
    WORKING_DIRECTORY "${fossil_BINARY_DIR}"
    COMMENT "Cleaning fossil build artifacts"
    DEPENDS fossil-configure
)


enable_testing()
add_test(NAME 01-setup
         COMMAND "${CMAKE_COMMAND}" --build . --target test-fossil-setup
)
add_test(NAME 02-test
         COMMAND "${CMAKE_COMMAND}" --build . --target test-fossil
)
add_test(NAME 03-clean
         COMMAND "${CMAKE_COMMAND}" --build . --target test-fossil-clean
)
set_property(TEST 02-test APPEND PROPERTY
             DEPENDS 01-setup)
set_property(TEST 03-clean APPEND PROPERTY
             DEPENDS 01-setup)

add_custom_target(check
    COMMAND "${CMAKE_CTEST_COMMAND}" -V
    DEPENDS test-fossil
)

## INSTALL ---------------------------------------------------------------
##
install(DIRECTORY ${fossil_EXE_DIR}/
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin/
    USE_SOURCE_PERMISSIONS
)
