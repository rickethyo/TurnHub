# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.31.6-msvc6)

# Reject any attempt to use a toolchain file. We must not use one because
# we could be downloading it here. If the CMAKE_TOOLCHAIN_FILE environment
# variable is set, the cache variable will have been initialized from it.
unset(CMAKE_TOOLCHAIN_FILE CACHE)
unset(ENV{CMAKE_TOOLCHAIN_FILE})

# We name the project and the target for the ExternalProject_Add() call
# to something that will highlight to the user what we are working on if
# something goes wrong and an error message is produced.

project(lvgl-populate NONE)



include(ExternalProject)
ExternalProject_Add(lvgl-populate
                     "UPDATE_DISCONNECTED" "False" "DOWNLOAD_EXTRACT_TIMESTAMP" "TRUE" "EXTERNALPROJECT_INTERNAL_ARGUMENT_SEPARATOR" "URL" "https://codeload.github.com/lvgl/lvgl/tar.gz/c033a98afddd65aaafeebea625382a94020fe4a7" "URL_HASH" "SHA256=9c6f8230d0b0e11141aad8f047af699322dbd3d37b2c41170b1526c12d5dfbb3"
                    SOURCE_DIR          "D:/TurnHub/Include/shared/simulator/build-win/_deps/lvgl-src"
                    BINARY_DIR          "D:/TurnHub/Include/shared/simulator/build-win/_deps/lvgl-build"
                    CONFIGURE_COMMAND   ""
                    BUILD_COMMAND       ""
                    INSTALL_COMMAND     ""
                    TEST_COMMAND        ""
                    USES_TERMINAL_DOWNLOAD  YES
                    USES_TERMINAL_UPDATE    YES
                    USES_TERMINAL_PATCH     YES
)


