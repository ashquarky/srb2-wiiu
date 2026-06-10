# Copyright 2017 Igalia S.L. All Rights Reserved.
#
# Distributed under MIT license.
# See file LICENSE for detail or copy at https://opensource.org/licenses/MIT

# Try to find BrotliDec. Once done, this will define
#
#  BROTLIDEC_FOUND - system has BrotliDec.
#  BROTLIDEC_INCLUDE_DIRS - the BrotliDec include directories
#  BROTLIDEC_LINK_LIBRARIES - link these to use BrotliDec.

find_package(PkgConfig)

pkg_check_modules(BROTLIDEC libbrotlidec)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BrotliDec
        REQUIRED_VARS BROTLIDEC_INCLUDE_DIRS BROTLIDEC_LINK_LIBRARIES
        FOUND_VAR BROTLIDEC_FOUND
        VERSION_VAR BROTLIDEC_VERSION)

mark_as_advanced(
        BROTLIDEC_INCLUDE_DIRS
        BROTLIDEC_LINK_LIBRARIES
)