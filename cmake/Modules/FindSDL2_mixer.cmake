# Find SDL2
# Once done, this will define
#
#  SDL2_MIXER_FOUND - system has SDL2
#  SDL2_MIXER_INCLUDE_DIRS - SDL2 include directories
#  SDL2_MIXER_LIBRARIES - link libraries

find_package(PkgConfig QUIET)
pkg_check_modules(SDL2_MIXER IMPORTED_TARGET SDL2_mixer)

if(SDL2_MIXER_FOUND AND NOT TARGET SDL2_mixer::SDL2_mixer)
	add_library(SDL2_mixer::SDL2_mixer ALIAS PkgConfig::SDL2_MIXER)
endif()
