
// Emacs style mode select   -*- C++ -*-
// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Portions Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2014-2023 by Sonic Team Junior.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief SRB2 graphics stuff for SDL

#include <stdlib.h>

#include <signal.h>

#ifdef HAVE_SDL
#define _MATH_DEFINES_DEFINED
#include "SDL.h"

#ifdef HAVE_IMAGE
#include "SDL_image.h"
#elif defined (__unix__) || (!defined(__APPLE__) && defined (UNIXCOMMON)) // Windows & Mac don't need this, as SDL will do it for us.
#define LOAD_XPM //I want XPM!
#include "IMG_xpm.c" //Alam: I don't want to add SDL_Image.dll/so
#define HAVE_IMAGE //I have SDL_Image, sortof
#endif

#ifdef HAVE_IMAGE
#include "SDL_icon.xpm"
#endif

#include "../doomdef.h"

#include "../doomstat.h"
#include "../i_system.h"
#include "../v_video.h"
#include "../m_argv.h"
#include "../m_menu.h"
#include "../d_main.h"
#include "../s_sound.h"
#include "../i_joy.h"
#include "../st_stuff.h"
#include "../hu_stuff.h"
#include "../g_game.h"
#include "../i_video.h"
#include "../console.h"
#include "../command.h"
#include "../r_main.h"
#include "../lua_script.h"
#include "../lua_libs.h"
#include "../lua_hook.h"
#include "sdlmain.h"
#ifdef HWRENDER
#include "../hardware/hw_main.h"
#include "../hardware/hw_drv.h"
// For dynamic referencing of HW rendering functions
#include "hwsym_sdl.h"
#endif

#include <whb/gfx.h>
#include <gx2/shaders.h>
#include <gx2/registers.h>
#include <gx2/mem.h>
#include <gx2/enum.h>
#include <gx2/draw.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/sampler.h>
#include <gx2/utils.h>
#include <gx2r/surface.h>
#include "cafe/CafeGLSLCompiler.h"

#include <whb/proc.h>
#include <proc_ui/procui.h>
#include <coreinit/memdefaultheap.h>

// maximum number of windowed modes (see windowedModes[][])
#define MAXWINMODES (21)

/**	\brief
*/
static INT32 numVidModes = -1;

/**	\brief
*/
static char vidModeName[33][32]; // allow 33 different modes

rendermode_t rendermode = render_soft;
rendermode_t chosenrendermode = render_none; // set by command line arguments

static void VidWaitChanged(void) {}

// synchronize page flipping with screen refresh
consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "On", CV_SAVE | CV_CALL, CV_OnOff, VidWaitChanged);
static consvar_t cv_stretch = CVAR_INIT ("stretch", "Off", CV_SAVE|CV_NOSHOWHELP, CV_OnOff, NULL);
static consvar_t cv_alwaysgrabmouse = CVAR_INIT ("alwaysgrabmouse", "Off", CV_SAVE, CV_OnOff, NULL);

UINT8 graphics_started = 0; // Is used in console.c and screen.c

// To disable fullscreen at startup; is set in VID_PrepareModeList
boolean allow_fullscreen = false;
static SDL_bool disable_fullscreen = SDL_FALSE;
#define USE_FULLSCREEN (disable_fullscreen||!allow_fullscreen)?0:cv_fullscreen.value
static SDL_bool disable_mouse = SDL_FALSE;
#define USE_MOUSEINPUT (!disable_mouse && cv_usemouse.value && havefocus)
#define MOUSE_MENU false //(!disable_mouse && cv_usemouse.value && menuactive && !USE_FULLSCREEN)
#define MOUSEBUTTONS_MAX MOUSEBUTTONS

// first entry in the modelist which is not bigger than MAXVIDWIDTHxMAXVIDHEIGHT
static      INT32          firstEntry = 0;

// Total mouse motion X/Y offsets
static      INT32        mousemovex = 0, mousemovey = 0;

// SDL vars
static      SDL_Surface *vidSurface = NULL;
static      SDL_Surface *bufSurface = NULL;
static      SDL_Surface *icoSurface = NULL;
#if 0
static      SDL_Rect   **modeList = NULL;
static       Uint8       BitsPerPixel = 16;
#endif
Uint16      realwidth = BASEVIDWIDTH;
Uint16      realheight = BASEVIDHEIGHT;
static       SDL_bool    mousegrabok = SDL_TRUE;
static       SDL_bool    wrapmouseok = SDL_FALSE;
#define HalfWarpMouse(x,y) if (wrapmouseok) SDL_WarpMouseInWindow(window, (Uint16)(x/2),(Uint16)(y/2))
static       SDL_bool    videoblitok = SDL_FALSE;
static       SDL_bool    exposevideo = SDL_FALSE;
static       SDL_bool    usesdl2soft = SDL_FALSE;
static       SDL_bool    borderlesswindow = SDL_FALSE;

// SDL2 vars
static SDL_bool      havefocus = SDL_TRUE;
static const char *fallback_resolution_name = "Fallback";

// GX2 vars
static WHBGfxShaderGroup *basic_shader;
static int aPosition;
static int aTexCoord;

GX2Texture main_screen = {
	.surface = {
		.aa = GX2_AA_MODE1X,
		.dim = GX2_SURFACE_DIM_TEXTURE_2D,
		.depth = 1,
		.mipLevels = 1,
		.swizzle = 0,
		.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED,
	},
	.viewNumMips = 1,
	.viewNumSlices = 1,
	.compMap = GX2_COMP_MAP(
		GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A
	),
};

// For resolutions that aren't multiples of 256 (aka not 1280x720) we need to bounce the framebuffer
GX2Surface temp_surf = {
	.aa = GX2_AA_MODE1X,
	.dim = GX2_SURFACE_DIM_TEXTURE_2D,
	.depth = 1,
	.mipLevels = 1,
	.swizzle = 0,
	.tileMode = GX2_TILE_MODE_LINEAR_SPECIAL,
};

GX2Texture colour_table = {
	.surface = {
		.aa = GX2_AA_MODE1X,
		.use = GX2_SURFACE_USE_TEXTURE,
		.dim = GX2_SURFACE_DIM_TEXTURE_2D,
		.depth = 1,
		.mipLevels = 1,
		.swizzle = 0,
		.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED,

		.width = 256,
		.height = 1,
		.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
	},
	.viewNumMips = 1,
	.viewNumSlices = 1,
	.compMap = GX2_COMP_MAP(
		GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A
	),
};

GX2Sampler sampler_sharp;

// windowed video modes from which to choose from.
static INT32 windowedModes[MAXWINMODES][2] =
{
	{1920, 1080}, // 1.33,4.00
	{1280, 960}, // 1.33,4.00
	{1280, 800}, // 1.60,4.00
	{1280, 720}, // 1.66
	{ 800, 600}, // 1.33,2.50
	{ 640, 480}, // 1.33,2.00
	{ 640, 400}, // 1.60,2.00
	{ 320, 240}, // 1.33,1.00
	{ 320, 200}, // 1.60,1.00
};

static INT32 Impl_SDL_Scancode_To_Keycode(SDL_Scancode code)
{
	if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z)
	{
		// get lowercase ASCII
		return code - SDL_SCANCODE_A + 'a';
	}
	if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9)
	{
		return code - SDL_SCANCODE_1 + '1';
	}
	else if (code == SDL_SCANCODE_0)
	{
		return '0';
	}
	if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F10)
	{
		return KEY_F1 + (code - SDL_SCANCODE_F1);
	}
	switch (code)
	{
		// F11 and F12 are separated from the rest of the function keys
		case SDL_SCANCODE_F11: return KEY_F11;
		case SDL_SCANCODE_F12: return KEY_F12;

		case SDL_SCANCODE_KP_0: return KEY_KEYPAD0;
		case SDL_SCANCODE_KP_1: return KEY_KEYPAD1;
		case SDL_SCANCODE_KP_2: return KEY_KEYPAD2;
		case SDL_SCANCODE_KP_3: return KEY_KEYPAD3;
		case SDL_SCANCODE_KP_4: return KEY_KEYPAD4;
		case SDL_SCANCODE_KP_5: return KEY_KEYPAD5;
		case SDL_SCANCODE_KP_6: return KEY_KEYPAD6;
		case SDL_SCANCODE_KP_7: return KEY_KEYPAD7;
		case SDL_SCANCODE_KP_8: return KEY_KEYPAD8;
		case SDL_SCANCODE_KP_9: return KEY_KEYPAD9;

		case SDL_SCANCODE_RETURN:         return KEY_ENTER;
		case SDL_SCANCODE_ESCAPE:         return KEY_ESCAPE;
		case SDL_SCANCODE_BACKSPACE:      return KEY_BACKSPACE;
		case SDL_SCANCODE_TAB:            return KEY_TAB;
		case SDL_SCANCODE_SPACE:          return KEY_SPACE;
		case SDL_SCANCODE_MINUS:          return KEY_MINUS;
		case SDL_SCANCODE_EQUALS:         return KEY_EQUALS;
		case SDL_SCANCODE_LEFTBRACKET:    return '[';
		case SDL_SCANCODE_RIGHTBRACKET:   return ']';
		case SDL_SCANCODE_BACKSLASH:      return '\\';
		case SDL_SCANCODE_NONUSHASH:      return '#';
		case SDL_SCANCODE_SEMICOLON:      return ';';
		case SDL_SCANCODE_APOSTROPHE:     return '\'';
		case SDL_SCANCODE_GRAVE:          return '`';
		case SDL_SCANCODE_COMMA:          return ',';
		case SDL_SCANCODE_PERIOD:         return '.';
		case SDL_SCANCODE_SLASH:          return '/';
		case SDL_SCANCODE_CAPSLOCK:       return KEY_CAPSLOCK;
		case SDL_SCANCODE_PRINTSCREEN:    return 0; // undefined?
		case SDL_SCANCODE_SCROLLLOCK:     return KEY_SCROLLLOCK;
		case SDL_SCANCODE_PAUSE:          return KEY_PAUSE;
		case SDL_SCANCODE_INSERT:         return KEY_INS;
		case SDL_SCANCODE_HOME:           return KEY_HOME;
		case SDL_SCANCODE_PAGEUP:         return KEY_PGUP;
		case SDL_SCANCODE_DELETE:         return KEY_DEL;
		case SDL_SCANCODE_END:            return KEY_END;
		case SDL_SCANCODE_PAGEDOWN:       return KEY_PGDN;
		case SDL_SCANCODE_RIGHT:          return KEY_RIGHTARROW;
		case SDL_SCANCODE_LEFT:           return KEY_LEFTARROW;
		case SDL_SCANCODE_DOWN:           return KEY_DOWNARROW;
		case SDL_SCANCODE_UP:             return KEY_UPARROW;
		case SDL_SCANCODE_NUMLOCKCLEAR:   return KEY_NUMLOCK;
		case SDL_SCANCODE_KP_DIVIDE:      return KEY_KPADSLASH;
		case SDL_SCANCODE_KP_MULTIPLY:    return '*'; // undefined?
		case SDL_SCANCODE_KP_MINUS:       return KEY_MINUSPAD;
		case SDL_SCANCODE_KP_PLUS:        return KEY_PLUSPAD;
		case SDL_SCANCODE_KP_ENTER:       return KEY_ENTER;
		case SDL_SCANCODE_KP_PERIOD:      return KEY_KPADDEL;
		case SDL_SCANCODE_NONUSBACKSLASH: return '\\';

		case SDL_SCANCODE_LSHIFT: return KEY_LSHIFT;
		case SDL_SCANCODE_RSHIFT: return KEY_RSHIFT;
		case SDL_SCANCODE_LCTRL:  return KEY_LCTRL;
		case SDL_SCANCODE_RCTRL:  return KEY_RCTRL;
		case SDL_SCANCODE_LALT:   return KEY_LALT;
		case SDL_SCANCODE_RALT:   return KEY_RALT;
		case SDL_SCANCODE_LGUI:   return KEY_LEFTWIN;
		case SDL_SCANCODE_RGUI:   return KEY_RIGHTWIN;
		default:                  break;
	}

	return 0;
}

static boolean ShouldIgnoreMouse(void)
{
	if (cv_alwaysgrabmouse.value)
		return false;
	if (menuactive)
		return !M_MouseNeeded();
	if (paused || con_destlines || chat_on)
		return true;
	if (gamestate != GS_LEVEL && gamestate != GS_INTERMISSION &&
			gamestate != GS_CONTINUING && gamestate != GS_CUTSCENE)
		return true;
	return false;
}

static boolean ShouldGrabMouse(void)
{
	if (cv_alwaysgrabmouse.value)
		return true;
	if (ShouldIgnoreMouse())
		return false;
	if (!mousegrabbedbylua)
		return false;
	return true;
}

static void SDLdoGrabMouse(void)
{

}

static void SDLdoUngrabMouse(void)
{

}

void SDLforceUngrabMouse(void)
{

}

void I_UpdateMouseGrab(void)
{

}

void I_SetMouseGrab(boolean grab)
{
	if (grab)
		SDLdoGrabMouse();
	else
		SDLdoUngrabMouse();
}

static void VID_Command_NumModes_f (void)
{
	CONS_Printf(M_GetText("%d video mode(s) available(s)\n"), VID_NumModes());
}

static void VID_Command_Info_f (void)
{
#if 1
	SDL2STUB();
#else
#if 0
	const SDL_VideoInfo *videoInfo;
	videoInfo = SDL_GetVideoInfo(); //Alam: Double-Check
	if (videoInfo)
	{
		CONS_Printf("%s", M_GetText("Video Interface Capabilities:\n"));
		if (videoInfo->hw_available)
			CONS_Printf("%s", M_GetText(" Hardware surfaces\n"));
		if (videoInfo->wm_available)
			CONS_Printf("%s", M_GetText(" Window manager\n"));
		//UnusedBits1  :6
		//UnusedBits2  :1
		if (videoInfo->blit_hw)
			CONS_Printf("%s", M_GetText(" Accelerated blits HW-2-HW\n"));
		if (videoInfo->blit_hw_CC)
			CONS_Printf("%s", M_GetText(" Accelerated blits HW-2-HW with Colorkey\n"));
		if (videoInfo->wm_available)
			CONS_Printf("%s", M_GetText(" Accelerated blits HW-2-HW with Alpha\n"));
		if (videoInfo->blit_sw)
		{
			CONS_Printf("%s", M_GetText(" Accelerated blits SW-2-HW\n"));
			if (!M_CheckParm("-noblit")) videoblitok = SDL_TRUE;
		}
		if (videoInfo->blit_sw_CC)
			CONS_Printf("%s", M_GetText(" Accelerated blits SW-2-HW with Colorkey\n"));
		if (videoInfo->blit_sw_A)
			CONS_Printf("%s", M_GetText(" Accelerated blits SW-2-HW with Alpha\n"));
		if (videoInfo->blit_fill)
			CONS_Printf("%s", M_GetText(" Accelerated Color filling\n"));
		//UnusedBits3  :16
		if (videoInfo->video_mem)
			CONS_Printf(M_GetText(" There is %i KB of video memory\n"), videoInfo->video_mem);
		else
			CONS_Printf("%s", M_GetText(" There no video memory for SDL\n"));
		//*vfmt
	}
#else
	if (!M_CheckParm("-noblit")) videoblitok = SDL_TRUE;
#endif
	SurfaceInfo(bufSurface, M_GetText("Current Engine Mode"));
	SurfaceInfo(vidSurface, M_GetText("Current Video Mode"));
#endif
}

static void VID_Command_ModeList_f(void)
{
	// List windowed modes
	INT32 i = 0;
	CONS_Printf("NOTE: Under SDL2, all modes are supported on all platforms.\n");
	CONS_Printf("Under opengl, fullscreen only supports native desktop resolution.\n");
	CONS_Printf("Under software, the mode is stretched up to desktop resolution.\n");
	for (i = 0; i < MAXWINMODES; i++)
	{
		CONS_Printf("%2d: %dx%d\n", i, windowedModes[i][0], windowedModes[i][1]);
	}

}

static void VID_Command_Mode_f (void)
{
	INT32 modenum;

	if (COM_Argc()!= 2)
	{
		CONS_Printf(M_GetText("vid_mode <modenum> : set video mode, current video mode %i\n"), vid.modenum);
		return;
	}

	modenum = atoi(COM_Argv(1));

	if (modenum >= VID_NumModes())
		CONS_Printf(M_GetText("Video mode not present\n"));
	else
		setmodeneeded = modenum+1; // request vid mode change
}

static inline void SDLJoyRemap(event_t *event)
{
	(void)event;
}

static INT32 SDLJoyAxis(const Sint16 axis, evtype_t which)
{
	// -32768 to 32767
	INT32 raxis = axis/32;
	if (which == ev_joystick)
	{
		if (Joystick.bGamepadStyle)
		{
			// gamepad control type, on or off, live or die
			if (raxis < -(JOYAXISRANGE/2))
				raxis = -1;
			else if (raxis > (JOYAXISRANGE/2))
				raxis = 1;
			else
				raxis = 0;
		}
		else
		{
			raxis = JoyInfo.scale!=1?((raxis/JoyInfo.scale)*JoyInfo.scale):raxis;

#ifdef SDL_JDEADZONE
			if (-SDL_JDEADZONE <= raxis && raxis <= SDL_JDEADZONE)
				raxis = 0;
#endif
		}
	}
	else if (which == ev_joystick2)
	{
		if (Joystick2.bGamepadStyle)
		{
			// gamepad control type, on or off, live or die
			if (raxis < -(JOYAXISRANGE/2))
				raxis = -1;
			else if (raxis > (JOYAXISRANGE/2))
				raxis = 1;
			else raxis = 0;
		}
		else
		{
			raxis = JoyInfo2.scale!=1?((raxis/JoyInfo2.scale)*JoyInfo2.scale):raxis;

#ifdef SDL_JDEADZONE
			if (-SDL_JDEADZONE <= raxis && raxis <= SDL_JDEADZONE)
				raxis = 0;
#endif
		}
	}
	return raxis;
}

static void Impl_HandleWindowEvent(SDL_WindowEvent evt)
{
	static SDL_bool firsttimeonmouse = SDL_TRUE;
	static SDL_bool mousefocus = SDL_TRUE;
	static SDL_bool kbfocus = SDL_TRUE;

	switch (evt.event)
	{
		case SDL_WINDOWEVENT_ENTER:
			mousefocus = SDL_TRUE;
			break;
		case SDL_WINDOWEVENT_LEAVE:
			mousefocus = SDL_FALSE;
			break;
		case SDL_WINDOWEVENT_FOCUS_GAINED:
			kbfocus = SDL_TRUE;
			mousefocus = SDL_TRUE;
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
			kbfocus = SDL_FALSE;
			mousefocus = SDL_FALSE;
			break;
		case SDL_WINDOWEVENT_MAXIMIZED:
			break;
	}

	if (mousefocus && kbfocus)
	{
		// Tell game we got focus back, resume music if necessary
		window_notinfocus = false;
		if (!paused)
			S_ResumeAudio(); //resume it

		if (!firsttimeonmouse)
		{
			if (cv_usemouse.value) I_StartupMouse();
		}
		//else firsttimeonmouse = SDL_FALSE;

		if (USE_MOUSEINPUT && ShouldGrabMouse())
			SDLdoGrabMouse();
	}
	else if (!mousefocus && !kbfocus)
	{
		// Tell game we lost focus, pause music
		window_notinfocus = true;
		if (! cv_playmusicifunfocused.value)
			S_PauseAudio();
		if (! cv_playsoundsifunfocused.value)
			S_StopSounds();

		if (!disable_mouse)
		{
			SDLforceUngrabMouse();
		}
		memset(gamekeydown, 0, NUMKEYS); // TODO this is a scary memset

		if (MOUSE_MENU)
		{
			SDLdoUngrabMouse();
		}
	}

}

static void Impl_HandleKeyboardEvent(SDL_KeyboardEvent evt, Uint32 type)
{
	event_t event;
	if (type == SDL_KEYUP)
	{
		event.type = ev_keyup;
	}
	else if (type == SDL_KEYDOWN)
	{
		event.type = ev_keydown;
	}
	else
	{
		return;
	}
	event.key = Impl_SDL_Scancode_To_Keycode(evt.keysym.scancode);
	event.repeated = (evt.repeat != 0);
	if (event.key) D_PostEvent(&event);
}

static void Impl_HandleTextEvent(SDL_TextInputEvent evt)
{
	event_t event;
	event.type = ev_text;
	if (evt.text[1] != '\0')
	{
		// limit ourselves to ASCII for now, we can add UTF-8 support later
		return;
	}
	event.key = evt.text[0];
	D_PostEvent(&event);
}

static void Impl_HandleJoystickAxisEvent(SDL_JoyAxisEvent evt)
{
	event_t event;
	SDL_JoystickID joyid[2];

	// Determine the Joystick IDs for each current open joystick
	joyid[0] = SDL_JoystickInstanceID(JoyInfo.dev);
	joyid[1] = SDL_JoystickInstanceID(JoyInfo2.dev);

	evt.axis++;
	event.key = event.x = event.y = INT32_MAX;

	if (evt.which == joyid[0])
	{
		event.type = ev_joystick;
	}
	else if (evt.which == joyid[1])
	{
		event.type = ev_joystick2;
	}
	else return;
	//axis
	if (evt.axis > JOYAXISSET*2)
		return;
	//vaule
	if (evt.axis%2)
	{
		event.key = evt.axis / 2;
		event.x = SDLJoyAxis(evt.value, event.type);
	}
	else
	{
		evt.axis--;
		event.key = evt.axis / 2;
		event.y = SDLJoyAxis(evt.value, event.type);
	}
	D_PostEvent(&event);
}

#if 0
static void Impl_HandleJoystickHatEvent(SDL_JoyHatEvent evt)
{
	event_t event;
	SDL_JoystickID joyid[2];

	// Determine the Joystick IDs for each current open joystick
	joyid[0] = SDL_JoystickInstanceID(JoyInfo.dev);
	joyid[1] = SDL_JoystickInstanceID(JoyInfo2.dev);

	if (evt.hat >= JOYHATS)
		return; // ignore hats with too high an index

	if (evt.which == joyid[0])
	{
		event.key = KEY_HAT1 + (evt.hat*4);
	}
	else if (evt.which == joyid[1])
	{
		event.key = KEY_2HAT1 + (evt.hat*4);
	}
	else return;

	// NOTE: UNFINISHED
}
#endif

static void Impl_HandleJoystickButtonEvent(SDL_JoyButtonEvent evt, Uint32 type)
{
	event_t event;
	SDL_JoystickID joyid[2];

	// Determine the Joystick IDs for each current open joystick
	joyid[0] = SDL_JoystickInstanceID(JoyInfo.dev);
	joyid[1] = SDL_JoystickInstanceID(JoyInfo2.dev);

	if (evt.which == joyid[0])
	{
		event.key = KEY_JOY1;
	}
	else if (evt.which == joyid[1])
	{
		event.key = KEY_2JOY1;
	}
	else return;
	if (type == SDL_JOYBUTTONUP)
	{
		event.type = ev_keyup;
	}
	else if (type == SDL_JOYBUTTONDOWN)
	{
		event.type = ev_keydown;
	}
	else return;
	if (evt.button < JOYBUTTONS)
	{
		event.key += evt.button;
	}
	else return;

	SDLJoyRemap(&event);
	if (event.type != ev_console) D_PostEvent(&event);
}



void I_GetEvent(void)
{
	SDL_Event evt;
	// We only want the first motion event,
	// otherwise we'll end up catching the warp back to center.
	//int mouseMotionOnce = 0;

	if (!graphics_started)
	{
		return;
	}

	mousemovex = mousemovey = 0;

	while (SDL_PollEvent(&evt))
	{
		switch (evt.type)
		{
			case SDL_WINDOWEVENT:
				Impl_HandleWindowEvent(evt.window);
				break;
			case SDL_KEYUP:
			case SDL_KEYDOWN:
				Impl_HandleKeyboardEvent(evt.key, evt.type);
				break;
			case SDL_TEXTINPUT:
				Impl_HandleTextEvent(evt.text);
				break;
			case SDL_JOYAXISMOTION:
				Impl_HandleJoystickAxisEvent(evt.jaxis);
				break;
#if 0
			case SDL_JOYHATMOTION:
				Impl_HandleJoystickHatEvent(evt.jhat)
				break;
#endif
			case SDL_JOYBUTTONUP:
			case SDL_JOYBUTTONDOWN:
				Impl_HandleJoystickButtonEvent(evt.jbutton, evt.type);
				break;
			case SDL_JOYDEVICEADDED:
				{
					SDL_Joystick *newjoy = SDL_JoystickOpen(evt.jdevice.which);

					CONS_Debug(DBG_GAMELOGIC, "Joystick device index %d added\n", evt.jdevice.which + 1);

					// Because SDL's device index is unstable, we're going to cheat here a bit:
					// For the first joystick setting that is NOT active:
					// 1. Set cv_usejoystickX.value to the new device index (this does not change what is written to config.cfg)
					// 2. Set OTHERS' cv_usejoystickX.value to THEIR new device index, because it likely changed
					//    * If device doesn't exist, switch cv_usejoystick back to default value (.string)
					//      * BUT: If that default index is being occupied, use ANOTHER cv_usejoystick's default value!
					if (newjoy && (!JoyInfo.dev || !SDL_JoystickGetAttached(JoyInfo.dev))
						&& JoyInfo2.dev != newjoy) // don't override a currently active device
					{
						cv_usejoystick.value = evt.jdevice.which + 1;

						if (JoyInfo2.dev)
							cv_usejoystick2.value = I_GetJoystickDeviceIndex(JoyInfo2.dev) + 1;
						else if (atoi(cv_usejoystick2.string) != JoyInfo.oldjoy
								&& atoi(cv_usejoystick2.string) != cv_usejoystick.value)
							cv_usejoystick2.value = atoi(cv_usejoystick2.string);
						else if (atoi(cv_usejoystick.string) != JoyInfo.oldjoy
								&& atoi(cv_usejoystick.string) != cv_usejoystick.value)
							cv_usejoystick2.value = atoi(cv_usejoystick.string);
						else // we tried...
							cv_usejoystick2.value = 0;
					}
					else if (newjoy && (!JoyInfo2.dev || !SDL_JoystickGetAttached(JoyInfo2.dev))
						&& JoyInfo.dev != newjoy) // don't override a currently active device
					{
						cv_usejoystick2.value = evt.jdevice.which + 1;

						if (JoyInfo.dev)
							cv_usejoystick.value = I_GetJoystickDeviceIndex(JoyInfo.dev) + 1;
						else if (atoi(cv_usejoystick.string) != JoyInfo2.oldjoy
								&& atoi(cv_usejoystick.string) != cv_usejoystick2.value)
							cv_usejoystick.value = atoi(cv_usejoystick.string);
						else if (atoi(cv_usejoystick2.string) != JoyInfo2.oldjoy
								&& atoi(cv_usejoystick2.string) != cv_usejoystick2.value)
							cv_usejoystick.value = atoi(cv_usejoystick2.string);
						else // we tried...
							cv_usejoystick.value = 0;
					}

					// Was cv_usejoystick disabled in settings?
					if (!strcmp(cv_usejoystick.string, "0") || !cv_usejoystick.value)
						cv_usejoystick.value = 0;
					else if (atoi(cv_usejoystick.string) <= I_NumJoys() // don't mess if we intentionally set higher than NumJoys
						     && cv_usejoystick.value) // update the cvar ONLY if a device exists
						CV_SetValue(&cv_usejoystick, cv_usejoystick.value);

					if (!strcmp(cv_usejoystick2.string, "0") || !cv_usejoystick2.value)
						cv_usejoystick2.value = 0;
					else if (atoi(cv_usejoystick2.string) <= I_NumJoys() // don't mess if we intentionally set higher than NumJoys
					         && cv_usejoystick2.value) // update the cvar ONLY if a device exists
						CV_SetValue(&cv_usejoystick2, cv_usejoystick2.value);

					// Update all joysticks' init states
					// This is a little wasteful since cv_usejoystick already calls this, but
					// we need to do this in case CV_SetValue did nothing because the string was already same.
					// if the device is already active, this should do nothing, effectively.
					I_InitJoystick();
					I_InitJoystick2();

					CONS_Debug(DBG_GAMELOGIC, "Joystick1 device index: %d\n", JoyInfo.oldjoy);
					CONS_Debug(DBG_GAMELOGIC, "Joystick2 device index: %d\n", JoyInfo2.oldjoy);

					// update the menu
					if (currentMenu == &OP_JoystickSetDef)
						M_SetupJoystickMenu(0);

					if (JoyInfo.dev != newjoy && JoyInfo2.dev != newjoy)
						SDL_JoystickClose(newjoy);
				}
				break;
			case SDL_JOYDEVICEREMOVED:
				if (JoyInfo.dev && !SDL_JoystickGetAttached(JoyInfo.dev))
				{
					CONS_Debug(DBG_GAMELOGIC, "Joystick1 removed, device index: %d\n", JoyInfo.oldjoy);
					I_ShutdownJoystick();
				}

				if (JoyInfo2.dev && !SDL_JoystickGetAttached(JoyInfo2.dev))
				{
					CONS_Debug(DBG_GAMELOGIC, "Joystick2 removed, device index: %d\n", JoyInfo2.oldjoy);
					I_ShutdownJoystick2();
				}

				// Update the device indexes, because they likely changed
				// * If device doesn't exist, switch cv_usejoystick back to default value (.string)
				//   * BUT: If that default index is being occupied, use ANOTHER cv_usejoystick's default value!
				if (JoyInfo.dev)
					cv_usejoystick.value = JoyInfo.oldjoy = I_GetJoystickDeviceIndex(JoyInfo.dev) + 1;
				else if (atoi(cv_usejoystick.string) != JoyInfo2.oldjoy)
					cv_usejoystick.value = atoi(cv_usejoystick.string);
				else if (atoi(cv_usejoystick2.string) != JoyInfo2.oldjoy)
					cv_usejoystick.value = atoi(cv_usejoystick2.string);
				else // we tried...
					cv_usejoystick.value = 0;

				if (JoyInfo2.dev)
					cv_usejoystick2.value = JoyInfo2.oldjoy = I_GetJoystickDeviceIndex(JoyInfo2.dev) + 1;
				else if (atoi(cv_usejoystick2.string) != JoyInfo.oldjoy)
					cv_usejoystick2.value = atoi(cv_usejoystick2.string);
				else if (atoi(cv_usejoystick.string) != JoyInfo.oldjoy)
					cv_usejoystick2.value = atoi(cv_usejoystick.string);
				else // we tried...
					cv_usejoystick2.value = 0;

				// Was cv_usejoystick disabled in settings?
				if (!strcmp(cv_usejoystick.string, "0"))
					cv_usejoystick.value = 0;
				else if (atoi(cv_usejoystick.string) <= I_NumJoys() // don't mess if we intentionally set higher than NumJoys
						 && cv_usejoystick.value) // update the cvar ONLY if a device exists
					CV_SetValue(&cv_usejoystick, cv_usejoystick.value);

				if (!strcmp(cv_usejoystick2.string, "0"))
					cv_usejoystick2.value = 0;
				else if (atoi(cv_usejoystick2.string) <= I_NumJoys() // don't mess if we intentionally set higher than NumJoys
						 && cv_usejoystick2.value) // update the cvar ONLY if a device exists
					CV_SetValue(&cv_usejoystick2, cv_usejoystick2.value);

				CONS_Debug(DBG_GAMELOGIC, "Joystick1 device index: %d\n", JoyInfo.oldjoy);
				CONS_Debug(DBG_GAMELOGIC, "Joystick2 device index: %d\n", JoyInfo2.oldjoy);

				// update the menu
				if (currentMenu == &OP_JoystickSetDef)
					M_SetupJoystickMenu(0);
				break;
			case SDL_QUIT:
				LUA_HookBool(true, HOOK(GameQuit));
				I_Quit();
				break;
		}
	}

	// Send all relative mouse movement as one single mouse event.
	if (mousemovex || mousemovey)
	{
		event_t event;
		int wwidth, wheight;
		//SDL_GetWindowSize(window, &wwidth, &wheight);
		//SDL_memset(&event, 0, sizeof(event_t));
		event.type = ev_mouse;
		event.key = 0;
		event.x = (INT32)lround(mousemovex * ((float)wwidth / (float)realwidth));
		event.y = (INT32)lround(mousemovey * ((float)wheight / (float)realheight));
		D_PostEvent(&event);
	}

	// In order to make wheels act like buttons, we have to set their state to Up.
	// This is because wheel messages don't have an up/down state.
	gamekeydown[KEY_MOUSEWHEELDOWN] = gamekeydown[KEY_MOUSEWHEELUP] = 0;
}

void I_StartupMouse(void)
{
	static SDL_bool firsttimeonmouse = SDL_TRUE;

	if (disable_mouse)
		return;

	if (!firsttimeonmouse)
	{
		HalfWarpMouse(realwidth, realheight); // warp to center
	}
	else
		firsttimeonmouse = SDL_FALSE;
	if (cv_usemouse.value && ShouldGrabMouse())
		SDLdoGrabMouse();
	else
		SDLdoUngrabMouse();
}

//
// I_OsPolling
//
void I_OsPolling(void)
{
	SDL_Keymod mod;

	if (consolevent)
		I_GetConsoleEvents();
	if (SDL_WasInit(SDL_INIT_JOYSTICK) == SDL_INIT_JOYSTICK)
	{
		SDL_JoystickUpdate();
		I_GetJoystickEvents();
		I_GetJoystick2Events();
	}

	I_GetMouseEvents();

	I_GetEvent();

	mod = SDL_GetModState();
	/* Handle here so that our state is always synched with the system. */
	shiftdown = ctrldown = altdown = 0;
	capslock = false;
	if (mod & KMOD_LSHIFT) shiftdown |= 1;
	if (mod & KMOD_RSHIFT) shiftdown |= 2;
	if (mod & KMOD_LCTRL)   ctrldown |= 1;
	if (mod & KMOD_RCTRL)   ctrldown |= 2;
	if (mod & KMOD_LALT)     altdown |= 1;
	if (mod & KMOD_RALT)     altdown |= 2;
	if (mod & KMOD_CAPS) capslock = true;

	if (!WHBProcIsRunning()) {
		LUA_HookBool(true, HOOK(GameQuit));
		I_Quit();
	}
}

//
// I_UpdateNoBlit
//
void I_UpdateNoBlit(void)
{
	// TODO
}

// I_SkipFrame
//
// Returns true if it thinks we can afford to skip this frame
// from PrBoom's src/SDL/i_video.c
static inline boolean I_SkipFrame(void)
{
#if 1
	// While I fixed the FPS counter bugging out with this,
	// I actually really like being able to pause and
	// use perfstats to measure rendering performance
	// without game logic changes.
	return false;
#else
	static boolean skip = false;

	skip = !skip;

	switch (gamestate)
	{
		case GS_LEVEL:
			if (!paused)
				return false;
			/* FALLTHRU */
		//case GS_TIMEATTACK: -- sorry optimisation but now we have a cool level platter and that being laggardly looks terrible
		case GS_WAITINGPLAYERS:
			return skip; // Skip odd frames
		default:
			return false;
	}
#endif
}

//
// I_FinishUpdate
//
struct rect_vertexes {
	struct {
		float x;
		float y;
	} v[4];
} __attribute__((aligned (GX2_VERTEX_BUFFER_ALIGNMENT)));

struct rect_vertexes verts = {
	{
		[0] = {.x = -1.0f, .y = 1.0f},
		[1] = {.x = 1.0f, .y = 1.0f},
		[2] = {.x = 1.0f, .y = -1.0f},
		[3] = {.x = -1.0f, .y = -1.0f},
	}
};
struct rect_vertexes uvs = {
	{
		[0] = {.x = 0.0f, .y = 0.0f},
		[1] = {.x = 1.0f, .y = 0.0f},
		[2] = {.x = 1.0f, .y = 1.0f},
		[3] = {.x = 0.0f, .y = 1.0f},
	}
};

void I_FinishUpdate(void)
{
	if (rendermode == render_none)
		return; //Alam: No software or OpenGl surface

	SCR_CalculateFPS();

	if (I_SkipFrame())
		return;

	if (marathonmode)
		SCR_DisplayMarathonInfo();

	// draw captions if enabled
	if (cv_closedcaptioning.value)
		SCR_ClosedCaptions();

	if (cv_ticrate.value)
		SCR_DisplayTicRate();

	if (cv_showping.value && netgame && consoleplayer != serverplayer)
		SCR_DisplayLocalPing();

	if (rendermode == render_soft && screens[0])
	{
		if (!main_screen.surface.image) {
			// ProcUI probably ganked us
			return;
		}
		if (vid.width != main_screen.surface.pitch) {
			// Slow blit path
			GX2CopySurface(&temp_surf, 0, 0, &main_screen.surface, 0, 0);
		}

		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, main_screen.surface.image, main_screen.surface.imageSize);
		WHBGfxBeginRender();
		WHBGfxBeginRenderTV();
		WHBGfxClearColor(0.0f, 1.0f, 0.0f, 1.0f);

		GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
		GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
		GX2SetBlendControl(
			GX2_RENDER_TARGET_0,
			/* RGB = [srcRGB * srcA] + [dstRGB * (1-srcA)] */
			GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA,
			GX2_BLEND_COMBINE_MODE_ADD,
			TRUE,
			/* A = [srcA * 1] + [dstA * (1-srcA)] */
			GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_INV_SRC_ALPHA,
			GX2_BLEND_COMBINE_MODE_ADD
		);

		GX2SetPixelSampler(&sampler_sharp, 0);
		GX2SetPixelTexture(&main_screen, 0);
		GX2SetPixelSampler(&sampler_sharp, 1);
		GX2SetPixelTexture(&colour_table, 1);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, &verts, sizeof(verts));
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, &uvs, sizeof(uvs));

		GX2SetAttribBuffer(aPosition, sizeof(verts), sizeof(verts.v[0]), &verts);
		GX2SetAttribBuffer(aTexCoord, sizeof(uvs), sizeof(uvs.v[0]), &uvs);

		GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
		GX2SetFetchShader(&basic_shader->fetchShader);
		GX2SetVertexShader(basic_shader->vertexShader);
		GX2SetPixelShader(basic_shader->pixelShader);

		GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);

		WHBGfxFinishRenderTV();
		WHBGfxFinishRender();
	}
#ifdef HWRENDER
	else if (rendermode == render_opengl)
	{
		// Final postprocess step of palette rendering, after everything else has been drawn.
		if (HWR_ShouldUsePaletteRendering())
		{
			HWD.pfnMakeScreenTexture(HWD_SCREENTEXTURE_GENERIC2);
			HWD.pfnSetShader(HWR_GetShaderFromTarget(SHADER_PALETTE_POSTPROCESS));
			HWD.pfnDrawScreenTexture(HWD_SCREENTEXTURE_GENERIC2, NULL, 0);
			HWD.pfnUnSetShader();
		}
		HWD.pfnFinishUpdate(1);
	}
#endif

	exposevideo = SDL_FALSE;
}

//
// I_UpdateNoVsync
//
void I_UpdateNoVsync(void)
{
	INT32 real_vidwait = cv_vidwait.value;
	cv_vidwait.value = 0;
	I_FinishUpdate();
	cv_vidwait.value = real_vidwait;
}

//
// I_ReadScreen
//
void I_ReadScreen(UINT8 *scr)
{
	if (rendermode != render_soft)
		I_Error ("I_ReadScreen: called while in non-software mode");
	else
		VID_BlitLinearScreen(screens[0], scr,
			vid.width, vid.height,
			vid.rowbytes, vid.rowbytes);
}

struct gx2_colour {
	uint8_t r, g, b, a;
};
//
// I_SetPalette
//
void I_SetPalette(RGBA_t *palette)
{

	if (!colour_table.surface.image) {
		GX2CalcSurfaceSizeAndAlignment(&colour_table.surface);
		GX2InitTextureRegs(&colour_table);

		colour_table.surface.image = MEMAllocFromDefaultHeapEx(colour_table.surface.imageSize, colour_table.surface.alignment);
	}

	for (size_t i = 0; i < 256; i++) {
		struct gx2_colour* table = colour_table.surface.image;
		table[i].r = palette[i].s.red;
		table[i].g = palette[i].s.green;
		table[i].b = palette[i].s.blue;
		table[i].a = 0xFF;
	}

	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, colour_table.surface.image, colour_table.surface.imageSize);
}

// return number of fullscreen + X11 modes
INT32 VID_NumModes(void)
{
	if (USE_FULLSCREEN && numVidModes != -1)
		return numVidModes - firstEntry;
	else
		return MAXWINMODES;
}

const char *VID_GetModeName(INT32 modeNum)
{
#if 0
	if (USE_FULLSCREEN && numVidModes != -1) // fullscreen modes
	{
		modeNum += firstEntry;
		if (modeNum >= numVidModes)
			return NULL;

		sprintf(&vidModeName[modeNum][0], "%dx%d",
			modeList[modeNum]->w,
			modeList[modeNum]->h);
	}
	else // windowed modes
	{
#endif
	if (modeNum == -1)
	{
		return fallback_resolution_name;
	}
		if (modeNum > MAXWINMODES)
			return NULL;

		sprintf(&vidModeName[modeNum][0], "%dx%d",
			windowedModes[modeNum][0],
			windowedModes[modeNum][1]);
	//}
	return &vidModeName[modeNum][0];
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	int i;
	for (i = 0; i < MAXWINMODES; i++)
	{
		if (windowedModes[i][0] == w && windowedModes[i][1] == h)
		{
			return i;
		}
	}
	return -1;
}

void VID_PrepareModeList(void)
{
	// Under SDL2, we just use the windowed modes list, and scale in windowed fullscreen.
	allow_fullscreen = true;
}

static SDL_bool Impl_CreateContext(void)
{
	// Renderer-specific stuff
#ifdef HWRENDER
	if ((rendermode == render_opengl)
	&& (vid.glstate != VID_GL_LIBRARY_ERROR))
	{
		// TODO
	}
	else
#endif
	if (rendermode == render_soft)
	{
		// TODO
	}
	return SDL_TRUE;
}

void VID_CheckGLLoaded(rendermode_t oldrender)
{
	(void)oldrender;
#ifdef HWRENDER
	if (vid.glstate == VID_GL_LIBRARY_ERROR) // Well, it didn't work the first time anyway.
	{
		CONS_Alert(CONS_ERROR, "OpenGL never loaded\n");
		rendermode = oldrender;
		if (chosenrendermode == render_opengl) // fallback to software
			rendermode = render_soft;
		if (setrenderneeded)
		{
			CV_StealthSetValue(&cv_renderer, oldrender);
			setrenderneeded = 0;
		}
	}
#endif
}

static void VID_DestroyGX2Surfaces(void) {
	if (main_screen.surface.image) {
		GX2RDestroySurfaceEx(&main_screen.surface, 0);
		main_screen.surface.image = NULL;
	}
	if (temp_surf.image) {
		GX2RDestroySurfaceEx(&main_screen.surface, 0);
		temp_surf.image = NULL;
	}
}

static void VID_CreateGX2Surfaces(void) {
	VID_DestroyGX2Surfaces();

	GX2RCreateSurface(&main_screen.surface, GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_USAGE_FORCE_MEM1);
	GX2InitTextureRegs(&main_screen);

	if (vid.width != main_screen.surface.pitch) {
		// bounce texture
		CONS_Printf("Video width %d does not match stride %d! Video updates slow\n", vid.width, main_screen.surface.pitch);

		GX2RCreateSurface(&temp_surf, GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_USAGE_FORCE_MEM1);
		vid.direct = temp_surf.image;
		screens[0] = vid.direct;
	} else {
		// use main screen directly
		vid.direct = main_screen.surface.image;
		screens[0] = vid.direct;
	}
}

static uint32_t VID_ProcUIAcquire(void* arg) { (void)arg; VID_CreateGX2Surfaces(); return 0; }
static uint32_t VID_ProcUIRelease(void* arg) { (void)arg; VID_DestroyGX2Surfaces(); return 0; }

boolean VID_CheckRenderer(void)
{
	boolean rendererchanged = false;
	boolean contextcreated = false;
#ifdef HWRENDER
	rendermode_t oldrenderer = rendermode;
#endif

	if (dedicated)
		return false;

	if (setrenderneeded)
	{
		rendermode = setrenderneeded;
		rendererchanged = true;

#ifdef HWRENDER
		if (rendermode == render_opengl)
		{
			VID_CheckGLLoaded(oldrenderer);
			if (vid.glstate == VID_GL_LIBRARY_NOTLOADED)
			{
				VID_StartupOpenGL();

				// Loaded successfully!
				if (vid.glstate == VID_GL_LIBRARY_LOADED)
				{
					// TODO GX2
				}
			}
			else if (vid.glstate == VID_GL_LIBRARY_ERROR)
				rendererchanged = false;
		}
#endif

		if (!contextcreated)
			Impl_CreateContext();

		setrenderneeded = 0;
	}

	if (rendermode == render_soft)
	{
		// "vram"
		if (vid.buffer) {
			MEMFreeToDefaultHeap(vid.buffer);
			vid.buffer = NULL;
		}
		vid.rowbytes = vid.width; // TODO fix the renderer to not need this
		const uint32_t buffer_size = NUMSCREENS * vid.height * vid.rowbytes * vid.bpp;
		vid.buffer = MEMAllocFromDefaultHeapEx(buffer_size, 0x100);

		// screen texture
		if (vid.bpp == 1) {
			main_screen.surface.format = GX2_SURFACE_FORMAT_UNORM_R8;
			temp_surf.format = GX2_SURFACE_FORMAT_UNORM_R8;
		} else {
			CONS_Printf("Unsupported video depth %d\n", vid.bpp);
		}

		main_screen.surface.width = vid.width;
		main_screen.surface.height = vid.height;
		temp_surf.width = vid.width;
		temp_surf.height = vid.height;

		VID_CreateGX2Surfaces();
	}
#ifdef HWRENDER
	else if (rendermode == render_opengl && rendererchanged)
	{
		HWR_Switch();
		V_SetPalette(0);
	}
#endif

	return rendererchanged;
}

static UINT32 refresh_rate;
static UINT32 VID_GetRefreshRate(void)
{
	return 60;
}

INT32 VID_SetMode(INT32 modeNum)
{
	SDLdoUngrabMouse();

	vid.recalc = 1;
	vid.bpp = 1;

	if (modeNum < 0)
		modeNum = 0;
	if (modeNum >= MAXWINMODES)
		modeNum = MAXWINMODES-1;

	vid.width = windowedModes[modeNum][0];
	vid.height = windowedModes[modeNum][1];
	vid.modenum = modeNum;

	refresh_rate = VID_GetRefreshRate();

	VID_CheckRenderer();
	return SDL_TRUE;
}

static WHBGfxShaderGroup* GLSL_CompileShader(const char* vsSrc, const char* psSrc)
{
	char infoLog[1024];
	GX2VertexShader* vs = GLSL_CompileVertexShader(vsSrc, infoLog, sizeof(infoLog), GLSL_COMPILER_FLAG_NONE);
	if(!vs) {
		OSReport("Failed to compile vertex shader. Infolog: %s\n", infoLog);
		return NULL;
	}
	GX2PixelShader* ps = GLSL_CompilePixelShader(psSrc, infoLog, sizeof(infoLog), GLSL_COMPILER_FLAG_NONE);
	if(!ps) {
		OSReport("Failed to compile pixel shader. Infolog: %s\n", infoLog);
		return NULL;
	}
	WHBGfxShaderGroup* shaderGroup = (WHBGfxShaderGroup*)malloc(sizeof(WHBGfxShaderGroup));
	memset(shaderGroup, 0, sizeof(*shaderGroup));
	shaderGroup->vertexShader = vs;
	shaderGroup->pixelShader = ps;
	return shaderGroup;
}

const char main_frag[] = {
#embed "cafe/main.frag"
	, 0
};
const char main_vert[] = {
#embed "cafe/main.vert"
	, 0
};

void I_StartupGraphics(void)
{
	if (dedicated)
	{
		rendermode = render_none;
		return;
	}
	if (graphics_started)
		return;

	COM_AddCommand ("vid_nummodes", VID_Command_NumModes_f, COM_LUA);
	COM_AddCommand ("vid_info", VID_Command_Info_f, COM_LUA);
	COM_AddCommand ("vid_modelist", VID_Command_ModeList_f, COM_LUA);
	COM_AddCommand ("vid_mode", VID_Command_Mode_f, 0);
	CV_RegisterVar (&cv_vidwait);
	CV_RegisterVar (&cv_stretch);
	CV_RegisterVar (&cv_alwaysgrabmouse);
	disable_mouse = M_CheckParm("-nomouse");
	disable_fullscreen = M_CheckParm("-win") ? 1 : 0;

	keyboard_started = true;

	WHBGfxInit();
	boolean ok = GLSL_Init();
	if (!ok) {
		CONS_Printf(M_GetText("Couldn't start up shaders!\n"));
		return;
	}

	basic_shader = GLSL_CompileShader(main_vert, main_frag);
	if (!basic_shader) {
		CONS_Printf(M_GetText("Failed to compile shader...\n"));
	}

	int buffer = 0;
	aPosition = buffer++;
	WHBGfxInitShaderAttribute(basic_shader, "aPosition", aPosition, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	aTexCoord = buffer++;
	WHBGfxInitShaderAttribute(basic_shader, "aTexCoord", aTexCoord, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	WHBGfxInitFetchShader(basic_shader);
	GX2InitSampler(&sampler_sharp, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);

	ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, VID_ProcUIAcquire, NULL, 150);
	ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, VID_ProcUIRelease, NULL, 150);

	// Renderer choices
	// Takes priority over the config.
	if (M_CheckParm("-renderer"))
	{
		INT32 i = 0;
		CV_PossibleValue_t *renderer_list = cv_renderer_t;
		const char *modeparm = M_GetNextParm();
		while (renderer_list[i].strvalue)
		{
			if (!stricmp(modeparm, renderer_list[i].strvalue))
			{
				chosenrendermode = renderer_list[i].value;
				break;
			}
			i++;
		}
	}

	// Choose Software renderer
	else if (M_CheckParm("-software"))
		chosenrendermode = render_soft;

#ifdef HWRENDER
	// Choose OpenGL renderer
	else if (M_CheckParm("-opengl"))
		chosenrendermode = render_opengl;

	// Don't startup OpenGL
	if (M_CheckParm("-nogl"))
	{
		vid.glstate = VID_GL_LIBRARY_ERROR;
		if (chosenrendermode == render_opengl)
			chosenrendermode = render_none;
	}
#endif

	if (chosenrendermode != render_none)
		rendermode = chosenrendermode;

	usesdl2soft = M_CheckParm("-softblit");
	borderlesswindow = M_CheckParm("-borderless");

	//SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY>>1,SDL_DEFAULT_REPEAT_INTERVAL<<2);
	//VID_Command_ModeList_f();

#ifdef HWRENDER
	if (rendermode == render_opengl)
		VID_StartupOpenGL();
#endif

	// Window icon
#ifdef HAVE_IMAGE
	icoSurface = IMG_ReadXPMFromArray(SDL_icon_xpm);
#endif

	// Fury: we do window initialization after GL setup to allow
	// SDL_GL_LoadLibrary to work well on Windows

	// Create window
	//Impl_CreateWindow(USE_FULLSCREEN);
	//Impl_SetWindowName("SRB2 "VERSIONSTRING);
	VID_SetMode(VID_GetModeForSize(BASEVIDWIDTH, BASEVIDHEIGHT));

	vid.width = BASEVIDWIDTH; // Default size for startup
	vid.height = BASEVIDHEIGHT; // BitsPerPixel is the SDL interface's
	vid.recalc = true; // Set up the console stufff
	vid.direct = NULL; // Maybe direct access?
	vid.bpp = 1; // This is the game engine's Bpp
	vid.WndParent = NULL; //For the window?

#ifdef HAVE_TTF
	I_ShutdownTTF();
#endif

	VID_SetMode(VID_GetModeForSize(BASEVIDWIDTH, BASEVIDHEIGHT));

	if (M_CheckParm("-nomousegrab"))
		mousegrabok = SDL_FALSE;
#if 0 // defined (_DEBUG)
	else
	{
		char videodriver[4] = {'S','D','L',0};
		if (!M_CheckParm("-mousegrab") &&
		    *strncpy(videodriver, SDL_GetCurrentVideoDriver(), 4) != '\0' &&
		    strncasecmp("x11",videodriver,4) == 0)
			mousegrabok = SDL_FALSE; //X11's XGrabPointer not good
	}
#endif
	realwidth = (Uint16)vid.width;
	realheight = (Uint16)vid.height;

	//VID_Command_Info_f();
	SDLdoUngrabMouse();

	if (mousegrabok && !disable_mouse)
		SDLdoGrabMouse();

	graphics_started = true;
}

void VID_StartupOpenGL(void)
{
#ifdef HWRENDER
	static boolean glstartup = false;
	if (!glstartup)
	{
		CONS_Printf("VID_StartupOpenGL()...\n");
		HWD.pfnInit             = hwSym("Init",NULL);
		HWD.pfnFinishUpdate     = hwSym("FinishUpdate",NULL);
		HWD.pfnDraw2DLine       = hwSym("Draw2DLine",NULL);
		HWD.pfnDrawPolygon      = hwSym("DrawPolygon",NULL);
		HWD.pfnDrawIndexedTriangles = hwSym("DrawIndexedTriangles",NULL);
		HWD.pfnRenderSkyDome    = hwSym("RenderSkyDome",NULL);
		HWD.pfnSetBlend         = hwSym("SetBlend",NULL);
		HWD.pfnClearBuffer      = hwSym("ClearBuffer",NULL);
		HWD.pfnSetTexture       = hwSym("SetTexture",NULL);
		HWD.pfnUpdateTexture    = hwSym("UpdateTexture",NULL);
		HWD.pfnDeleteTexture    = hwSym("DeleteTexture",NULL);
		HWD.pfnReadScreenTexture= hwSym("ReadScreenTexture",NULL);
		HWD.pfnGClipRect        = hwSym("GClipRect",NULL);
		HWD.pfnClearMipMapCache = hwSym("ClearMipMapCache",NULL);
		HWD.pfnSetSpecialState  = hwSym("SetSpecialState",NULL);
		HWD.pfnSetTexturePalette= hwSym("SetTexturePalette",NULL);
		HWD.pfnGetTextureUsed   = hwSym("GetTextureUsed",NULL);
		HWD.pfnDrawModel        = hwSym("DrawModel",NULL);
		HWD.pfnCreateModelVBOs  = hwSym("CreateModelVBOs",NULL);
		HWD.pfnSetTransform     = hwSym("SetTransform",NULL);
		HWD.pfnPostImgRedraw    = hwSym("PostImgRedraw",NULL);
		HWD.pfnFlushScreenTextures=hwSym("FlushScreenTextures",NULL);
		HWD.pfnDoScreenWipe     = hwSym("DoScreenWipe",NULL);
		HWD.pfnDrawScreenTexture= hwSym("DrawScreenTexture",NULL);
		HWD.pfnMakeScreenTexture= hwSym("MakeScreenTexture",NULL);
		HWD.pfnDrawScreenFinalTexture=hwSym("DrawScreenFinalTexture",NULL);

		HWD.pfnInitShaders      = hwSym("InitShaders",NULL);
		HWD.pfnLoadShader       = hwSym("LoadShader",NULL);
		HWD.pfnCompileShader    = hwSym("CompileShader",NULL);
		HWD.pfnSetShader        = hwSym("SetShader",NULL);
		HWD.pfnUnSetShader      = hwSym("UnSetShader",NULL);

		HWD.pfnSetShaderInfo    = hwSym("SetShaderInfo",NULL);

		HWD.pfnSetPaletteLookup = hwSym("SetPaletteLookup",NULL);
		HWD.pfnCreateLightTable = hwSym("CreateLightTable",NULL);
		HWD.pfnUpdateLightTable = hwSym("UpdateLightTable",NULL);
		HWD.pfnClearLightTables = hwSym("ClearLightTables",NULL);
		HWD.pfnSetScreenPalette = hwSym("SetScreenPalette",NULL);

		vid.glstate = HWD.pfnInit() ? VID_GL_LIBRARY_LOADED : VID_GL_LIBRARY_ERROR; // let load the OpenGL library

		if (vid.glstate == VID_GL_LIBRARY_ERROR)
		{
			rendermode = render_soft;
			setrenderneeded = 0;
		}
		glstartup = true;
	}
#endif
}

void I_ShutdownGraphics(void)
{
	const rendermode_t oldrendermode = rendermode;

	rendermode = render_none;
	if (icoSurface) SDL_FreeSurface(icoSurface);
	icoSurface = NULL;
	if (oldrendermode == render_soft)
	{
		if (vidSurface) SDL_FreeSurface(vidSurface);
		vidSurface = NULL;
		if (vid.buffer) free(vid.buffer);
		vid.buffer = NULL;
		if (bufSurface) SDL_FreeSurface(bufSurface);
		bufSurface = NULL;
	}

	I_OutputMsg("I_ShutdownGraphics(): ");

	// was graphics initialized anyway?
	if (!graphics_started)
	{
		I_OutputMsg("graphics never started\n");
		return;
	}
	graphics_started = false;
	I_OutputMsg("shut down\n");

#ifdef HWRENDER
	// TODO
#endif
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	framebuffer = SDL_FALSE;
}
#endif

void I_GetCursorPosition(INT32 *x, INT32 *y)
{
	SDL_GetMouseState(x, y);
}

UINT32 I_GetRefreshRate(void)
{
	// Moved to VID_GetRefreshRate.
	// Precalculating it like that won't work as
	// well for windowed mode since you can drag
	// the window around, but very slow PCs might have
	// trouble querying mode over and over again.
	return refresh_rate;
}
