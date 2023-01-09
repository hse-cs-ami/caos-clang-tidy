cmake_minimum_required(VERSION 3.4.3)
enable_testing()

set (CMAKE_CXX_COMPILER clang++-15)
set (CMAKE_CXX_STANDARD 17)

SET(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)
SET(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)

if( CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR )
    project(CaosChecker)


  # find clang + llvm
  find_package(Clang REQUIRED)

  if( LLVM_FOUND )
    list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
    include(AddLLVM)

    # set the compiler flags to match llvm
    include(HandleLLVMOptions)
  endif()

  # Make sure that our source directory is on the current cmake module path so that
  # we can include cmake files from this directory.
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

  # include Clang macros (unfortunately they are not part of the cmake installation)
  include(AddClang)

  # add include directories
  include_directories(${LLVM_INCLUDE_DIRS})

set(LLVM_LINK_COMPONENTS
  Support
  )

add_subdirectory(caos)

endif()
