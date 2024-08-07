# TRACCC library, part of the ACTS project (R&D line)
#
# (c) 2022 CERN for the benefit of the ACTS project
#
# Mozilla Public License Version 2.0

# CMake include(s).
cmake_minimum_required( VERSION 3.16 )
include( FetchContent )

# Silence FetchContent warnings with CMake >=3.24.
if( POLICY CMP0135 )
   cmake_policy( SET CMP0135 NEW )
endif()

# Tell the user what's happening.
message( STATUS "Building Alpaka as part of the TRACCC project" )

# Declare where to get Alpaka from.
set( TRACCC_ALPAKA_SOURCE
   "URL;https://github.com/alpaka-group/alpaka/archive/refs/tags/1.1.0.tar.gz;URL_MD5;e788ec42c8c4ebd87d12647940a01458"
   CACHE STRING "Source for Alpaka, when built as part of this project" )
mark_as_advanced( TRACCC_ALPAKA_SOURCE )
FetchContent_Declare( Alpaka ${TRACCC_ALPAKA_SOURCE} )

#Turn off build testing
if( DEFINED CACHE{BUILD_TESTING} )
   set( _buildTestingValue ${BUILD_TESTING} )
endif()
set( BUILD_TESTING FALSE CACHE INTERNAL "Forceful setting of BUILD_TESTING" )

# Get it into the current directory.
FetchContent_MakeAvailable( Alpaka )

# Reset the BUILD_TESTING variable.
if( DEFINED _buildTestingValue )
   set( BUILD_TESTING ${_buildTestingValue} CACHE BOOL "Turn tests on/off"
      FORCE )
   unset( _buildTestingValue )
else()
   unset( BUILD_TESTING CACHE )
endif()
