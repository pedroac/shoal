cmake_minimum_required(VERSION 3.24)

# VERSION holds "vMAJOR.MINOR.PATCH", like the other Sprat projects.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SHOAL_VERSION_TAG)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
string(STRIP "${SHOAL_VERSION_TAG}" SHOAL_VERSION_TAG)
string(REGEX REPLACE "^v" "" SHOAL_VERSION "${SHOAL_VERSION_TAG}")

# C is for sprat-lib, which is C99.
project(shoal VERSION ${SHOAL_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SHOAL_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)

if(SHOAL_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

# sprat-lib: an installed package when there is one, otherwise a download.
# To build against a local checkout, pass -DFETCHCONTENT_SOURCE_DIR_SPRAT=../sprat-lib
include(FetchContent)
if(NOT TARGET sprat::sprat)
    FetchContent_Declare(sprat
        GIT_REPOSITORY https://github.com/pedroac/sprat-lib.git
        GIT_TAG 97d87dba91f20dcbe9c2c45b2bdb749ccaa9a849   # sprat-lib main, v0.7.0
        FIND_PACKAGE_ARGS 0.7
    )
    FetchContent_MakeAvailable(sprat)
endif()

# Warnings for Shoal's own targets; dependencies keep their own flags.
function(shoal_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
endfunction()

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()
