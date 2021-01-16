# Sonic Robo Blast 2

## WiiU info
I was doing some light experimentation with a Wii U port of SRB2, since it's kinda a great lil' game. At the time of writing it crashes when entering a level due to a buffer overflow Somewhere in the code. reset data is disabled for a similar reason. Works about the same in Decaf and on console via classic HBL (no ProcUI).

### Compiling
With the usual wut/devkitPPC setup (last tested on r38):

```shell
# make a build folder or whatever you like
# this will save you much pain if cmake doesn't work and you need to start over
mkdir build && cd build
# run cmake - this is the ideal command:
# cmake .. -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/wut/share/wut.toolchain.cmake -DSRB2_CONFIG_HAVE_GME=NO -DSRB2_CONFIG_HAVE_OPENMPT=NO -DSRB2_CONFIG_HAVE_CURL=NO
# this is what I actually needed to get it working:
cmake .. -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/wut/share/wut.toolchain.cmake -DSDL2_INCLUDE_DIR=$DEVKITPRO/portlibs/wiiu/include/SDL2 -DSRB2_CONFIG_HAVE_GME=NO -DSRB2_CONFIG_HAVE_OPENMPT=NO -DSRB2_CONFIG_HAVE_CURL=NO
# I think this is SRB2's fault but I haven't got around to fixing it
# then compile:
make -j$(nproc)
# your output file is ./bin/SRB2SDL2.rpx
```

To run on console, you also the following on the root of your SD card:
- `music.dta`
- `patch_music.pk3`
- `patch.pk3`
- `player.dta`
- `srb2.pk3`
- `zones.pk3`

I pulled all these files out of the Windows installer zip on srb2.org.

I'm not convinced the port can actually create files on-console right now (works on emulator) so you may also need a valid `config.cfg` and `srb2sav1.ssg` to get it running on hardware. Despite my best efforts, reading in these large files on-console is *slow*, so break out `udplogserver`, get some tea, and settle in.

## Original readme

[![Build status](https://ci.appveyor.com/api/projects/status/399d4hcw9yy7hg2y?svg=true)](https://ci.appveyor.com/project/STJr/srb2)
[![Build status](https://travis-ci.org/STJr/SRB2.svg?branch=master)](https://travis-ci.org/STJr/SRB2)
[![CircleCI](https://circleci.com/gh/STJr/SRB2/tree/master.svg?style=svg)](https://circleci.com/gh/STJr/SRB2/tree/master)

[Sonic Robo Blast 2](https://srb2.org/) is a 3D Sonic the Hedgehog fangame based on a modified version of [Doom Legacy](http://doomlegacy.sourceforge.net/).

## Dependencies
- NASM (x86 builds only)
- SDL2 (Linux/OS X only)
- SDL2-Mixer (Linux/OS X only)
- libupnp (Linux/OS X only)
- libgme (Linux/OS X only)
- libopenmpt (Linux/OS X only)

## Compiling

See [SRB2 Wiki/Source code compiling](http://wiki.srb2.org/wiki/Source_code_compiling)

## Disclaimer
Sonic Team Junior is in no way affiliated with SEGA or Sonic Team. We do not claim ownership of any of SEGA's intellectual property used in SRB2.
