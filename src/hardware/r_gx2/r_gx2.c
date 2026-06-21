//
// Created by ash on 21/6/26.
//
#define  _CREATE_DLL_  // necessary for Unix AND Windows
#include "../../doomdef.h"
#include "../hw_drv.h"
#include "../../z_zone.h"
#include "../../r_local.h" // For rendertimefrac, used for the leveltime shader uniform
#include "../hw_shaders.h"
#include "vertex_cache.h"

// duplicated in i_video.c! yuck!
#include "CafeGLSLCompiler.h"

#include <cglm/cglm.h>
#include <gx2/surface.h>
#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/registers.h>
#include <whb/gfx.h>

//#define DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define DEBUG(fmt, ...)

#define      ASPECT_RATIO            (1.0f)  //(320.0f/200.0f)
#define      FAR_CLIPPING_PLANE      32768.0f // Draw further! Tails 01-21-2001
static float NEAR_CLIPPING_PLANE = NZCLIP_PLANE;

// static mat4 gx2_modelview;
// static mat4 gx2_projection;

// TODO YUCK!
static WHBGfxShaderGroup *GLSL_CompileShader(const char *vsSrc, const char *psSrc) {
	char infoLog[1024];
	GX2VertexShader *vs = GLSL_CompileVertexShader(vsSrc, infoLog, sizeof(infoLog), GLSL_COMPILER_FLAG_NONE);
	if (!vs) {
		OSReport("Failed to compile vertex shader. Infolog: %s\n", infoLog);
		return NULL;
	}
	GX2PixelShader *ps = GLSL_CompilePixelShader(psSrc, infoLog, sizeof(infoLog), GLSL_COMPILER_FLAG_NONE);
	if (!ps) {
		OSReport("Failed to compile pixel shader. Infolog: %s\n", infoLog);
		return NULL;
	}
	WHBGfxShaderGroup *shaderGroup = (WHBGfxShaderGroup *) malloc(sizeof(WHBGfxShaderGroup));
	memset(shaderGroup, 0, sizeof(*shaderGroup));
	shaderGroup->vertexShader = vs;
	shaderGroup->pixelShader = ps;
	return shaderGroup;
}

const char fixed_frag[] = {
#embed "fixed.frag"
	, 0
};
const char fixed_vert[] = {
#embed "fixed.vert"
	, 0
};
// TODO abstract this and make it a bit nicer lol
static WHBGfxShaderGroup *fixed_shader;
static int aPosition = 0;
static int aTexCoord = 0;
static GX2UniformBlock *vuUBO = NULL;

struct fixed_UBO {
	mat4 uModelView;
	mat4 uProjection;
};

struct fixed_UBO ubo;

struct vertex_arena vertex_cache;
struct vertex_arena ubo_cache;

EXPORT boolean HWRAPI(Init)(void) {
	DEBUG("Init");
	vcache_init(&vertex_cache, 128 * 1024 * sizeof(FOutVector), GX2_VERTEX_BUFFER_ALIGNMENT,
	            GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, false);
	vcache_init(&ubo_cache, 2 * 1024 * 1024, GX2_UNIFORM_BLOCK_ALIGNMENT, GX2_INVALIDATE_MODE_UNIFORM_BLOCK, true);

	const boolean ok = GLSL_Init();
	if (!ok) {
		CONS_Printf(M_GetText("Couldn't start up shaders!\n"));
		return false;
	}

	fixed_shader = GLSL_CompileShader(fixed_vert, fixed_frag);
	if (!fixed_shader) {
		CONS_Printf(M_GetText("Failed to compile shader...\n"));
		return false;
	}

	// Set up for FOutVectors
	int buffer = 0;
	aPosition = buffer++;
	WHBGfxInitShaderAttribute(fixed_shader, "aPosition", aPosition, offsetof(FOutVector, x),
	                          GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
	aTexCoord = buffer++;
	WHBGfxInitShaderAttribute(fixed_shader, "aTexCoord", aTexCoord, offsetof(FOutVector, s),
	                          GX2_ATTRIB_FORMAT_FLOAT_32_32);
	WHBGfxInitFetchShader(fixed_shader);

	vuUBO = GX2GetVertexUniformBlock(fixed_shader->vertexShader, "UBO");
	if (!vuUBO) {
		CONS_Printf(M_GetText("Failed to get shader UBO...\n"));
		return false;
	}

	SetTransform(NULL);
	return true;
}

EXPORT void HWRAPI(FinishUpdate)(INT32 waitvbl) {
	WHBGfxFinishRenderTV();
	WHBGfxFinishRender();
	WHBGfxBeginRender(); // actually wait for vsync
	WHBGfxBeginRenderTV();

	vcache_reset(&vertex_cache);
	vcache_reset(&ubo_cache);
}

EXPORT void HWRAPI(Draw2DLine)(F2DCoord *v1, F2DCoord *v2, RGBA_t Color) {
	DEBUG("Draw2DLine");
	// TODO
}

EXPORT void HWRAPI(DrawPolygon)(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags) {
	DEBUG("DrawPolygon");
	// TODO Blend PolyFlags, pSurf

	const struct block verts = vcache_add(&vertex_cache, pOutVerts, sizeof(pOutVerts[0]) * iNumPts);
	GX2SetAttribBuffer(aPosition, verts.size, sizeof(FOutVector), verts.data);
	GX2SetAttribBuffer(aTexCoord, verts.size, sizeof(FOutVector), verts.data);

	GX2SetDepthOnlyControl(TRUE, TRUE, GX2_COMPARE_FUNC_LEQUAL);
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

	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
	GX2SetFetchShader(&fixed_shader->fetchShader);
	GX2SetVertexShader(fixed_shader->vertexShader);
	GX2SetPixelShader(fixed_shader->pixelShader);

	const struct block a_ubo = vcache_add(&ubo_cache, &ubo, sizeof(ubo));
	GX2SetVertexUniformBlock(vuUBO->offset, a_ubo.size, a_ubo.data);

	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_FAN, iNumPts, 0, 1);
}

EXPORT void HWRAPI(DrawIndexedTriangles)(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags,
                                         UINT32 *IndexArray) {
	DEBUG("DrawIndexedTriangles");
	// TODO
}

EXPORT void HWRAPI(RenderSkyDome)(gl_sky_t *sky) {
	DEBUG("RenderSkyDome");
	// TODO
}

EXPORT void HWRAPI(SetBlend)(FBITFIELD PolyFlags) {
	DEBUG("SetBlend");
	// TODO
}

EXPORT void HWRAPI(ClearBuffer)(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor) {
	DEBUG("ClearBuffer");
	if (ColorMask) {
		GX2ColorBuffer *tv_cbuf = WHBGfxGetTVColourBuffer();
		GX2ClearColor(tv_cbuf, ClearColor->red, ClearColor->green, ClearColor->blue, ClearColor->alpha);
	}
	if (DepthMask) {
		GX2DepthBuffer *tv_dbuf = WHBGfxGetTVDepthBuffer();
		GX2ClearDepthStencilEx(tv_dbuf, tv_dbuf->depthClear, tv_dbuf->stencilClear, GX2_CLEAR_FLAGS_BOTH);
	}
	WHBGfxBeginRenderTV(); // a bit cheeky
}

EXPORT void HWRAPI(SetTexture)(GLMipmap_t *TexInfo) {
	DEBUG("SetTexture");
	// TODO
}

EXPORT void HWRAPI(UpdateTexture)(GLMipmap_t *TexInfo) {
	DEBUG("UpdateTexture");
	// TODO
}

EXPORT void HWRAPI(DeleteTexture)(GLMipmap_t *TexInfo) {
	DEBUG("DeleteTexture");
	// TODO
}

EXPORT void HWRAPI(ReadScreenTexture)(int tex, UINT8 *dst_data) {
	DEBUG("ReadScreenTexture");
	// TODO !!
}

EXPORT void HWRAPI(GClipRect)(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip) {
	DEBUG("GClipRect");
	// TODO
}

EXPORT void HWRAPI(ClearMipMapCache)(void) {
	DEBUG("ClearMipMapCache");
	// TODO ?
}

EXPORT void HWRAPI(SetSpecialState)(hwdspecialstate_t IdState, INT32 Value) {
	DEBUG("SetSpecialState");
	// TODO ??
}

//Hurdler: added for new development
EXPORT void HWRAPI(DrawModel)(model_t *model, INT32 frameIndex, float duration, float tics, INT32 nextFrameIndex,
                              FTransform *pos, float hscale, float vscale, UINT8 flipped, UINT8 hflipped,
                              FSurfaceInfo *Surface) {
	DEBUG("DrawModel");
	// TODO ??
}

EXPORT void HWRAPI(CreateModelVBOs)(model_t *model) {
	DEBUG("CreateModelVBOs");
	// TODO ??
}

EXPORT void HWRAPI(SetTransform)(FTransform *stransform) {
	DEBUG("SetTransform");
	float fov = 90.0f;

	glm_mat4_identity(ubo.uModelView);
	if (stransform) {
		fov = stransform->fovxangle;

		if (stransform->mirror) {
			glm_scale(ubo.uModelView, (vec3){
				           -stransform->scalex, stransform->scaley, -stransform->scalez
			           });
		} else if (stransform->flip) {
			glm_scale(ubo.uModelView, (vec3){
				           stransform->scalex, -stransform->scaley, -stransform->scalez
			           });
		} else {
			glm_scale(ubo.uModelView, (vec3){stransform->scalex, stransform->scaley, -stransform->scalez});
		}

		if (stransform->roll) {
			glm_rotate(ubo.uModelView, glm_rad(stransform->rollangle), (vec3){0.0f, 0.0f, 1.0f});
		}
		glm_rotate(ubo.uModelView, glm_rad(stransform->anglex), (vec3){1.0f, 0.0f, 0.0f});
		glm_rotate(ubo.uModelView, glm_rad(stransform->angley + 270.0f), (vec3){0.0f, 1.0f, 0.0f});
		glm_translate(ubo.uModelView, (vec3){-stransform->x, -stransform->z, -stransform->y});
	} else {
		glm_scale(ubo.uModelView, (vec3){1.0f, 1.0f, -1.0f});
	}

	glm_mat4_identity(ubo.uProjection);
	if (stransform && stransform->shearing) {
		// Simulate Software's y-shearing
		// https://zdoom.org/wiki/Y-shearing
		float fdy = stransform->viewaiming * 2;
		if (stransform->flip)
			fdy *= -1.0f;
		glm_translate(ubo.uProjection, (vec3){0.0f, -fdy / BASEVIDHEIGHT, 0.0f});
	}

	if (stransform && stransform->splitscreen) {
		const float afov = (float) (atan(tan(fov * M_PI / 360) * 0.8) * 360 / M_PI);
		glm_perspective(glm_rad(afov), 2 * ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE, ubo.uProjection);
	} else {
		glm_perspective(glm_rad(fov), ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE, ubo.uProjection);
	}
}

EXPORT INT32 HWRAPI(GetTextureUsed)(void) {
	DEBUG("GetTextureUsed");
	// TODO
	return 0;
}

EXPORT void HWRAPI(FlushScreenTextures)(void) {
	DEBUG("FlushScreenTextures");
	// TODO
}

EXPORT void HWRAPI(DoScreenWipe)(int wipeStart, int wipeEnd, FSurfaceInfo *surf, FBITFIELD polyFlags) {
	DEBUG("DoScreenWipe");
	// TODO
}

EXPORT void HWRAPI(DrawScreenTexture)(int tex, FSurfaceInfo *surf, FBITFIELD polyflags) {
	DEBUG("DrawScreenTexture");
	// TODO
}

EXPORT void HWRAPI(MakeScreenTexture)(int tex) {
	DEBUG("MakeScreenTexture");
	// TODO
}

EXPORT void HWRAPI(DrawScreenFinalTexture)(int tex, int width, int height) {
	DEBUG("DrawScreenFinalTexture");
	// TODO
}

EXPORT void HWRAPI(PostImgRedraw)(float points[SCREENVERTS][SCREENVERTS][2]) {
	DEBUG("PostImgRedraw");
	// TODO
}

EXPORT boolean HWRAPI(InitShaders)(void) {
	DEBUG("InitShaders");
	// TODO
	return false;
}

EXPORT void HWRAPI(LoadShader)(int slot, char *code, hwdshaderstage_t stage) {
	DEBUG("LoadShader");
	// TODO
}

EXPORT boolean HWRAPI(CompileShader)(int slot) {
	DEBUG("CompileShader");
	// TODO
	return false;
}

EXPORT void HWRAPI(SetShader)(int slot) {
	DEBUG("SetShader");
	// TODO
}

EXPORT void HWRAPI(UnSetShader)(void) {
	DEBUG("UnSetShader");
	// TODO
}

EXPORT void HWRAPI(SetShaderInfo)(hwdshaderinfo_t info, INT32 value) {
	DEBUG("SetShaderInfo");
	// TODO
}

EXPORT void HWRAPI(SetPaletteLookup)(UINT8 *lut) {
	DEBUG("SetPaletteLookup");
	// TODO
}

EXPORT UINT32 HWRAPI(CreateLightTable)(RGBA_t *hw_lighttable) {
	DEBUG("CreateLightTable");
	// TODO
	return 0;
}

EXPORT void HWRAPI(UpdateLightTable)(UINT32 id, RGBA_t *hw_lighttable) {
	DEBUG("UpdateLightTable");
	// TODO
}

EXPORT void HWRAPI(ClearLightTables)(void) {
	DEBUG("ClearLightTables");
	// TODO
}

EXPORT void HWRAPI(SetScreenPalette)(RGBA_t *palette) {
	DEBUG("SetScreenPalette");
	// TODO
}
