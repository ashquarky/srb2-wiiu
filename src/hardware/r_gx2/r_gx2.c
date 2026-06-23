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
#include <gx2/utils.h>
#include <gx2r/surface.h>
#include <whb/gfx.h>

// #define DEBUG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define DEBUG(fmt, ...)

#define      ASPECT_RATIO            (1.0f)  //(320.0f/200.0f)
#define      FAR_CLIPPING_PLANE      32768.0f // Draw further! Tails 01-21-2001
static float NEAR_CLIPPING_PLANE = NZCLIP_PLANE;

static RGBA_t rgba_palette[256];

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

static struct fixed_UBO ubo;

static struct vertex_arena vertex_cache;
static struct vertex_arena ubo_cache;
static struct vertex_arena index_cache;

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define HANDLE_NDX(h) (h & 0xffff)
#define HANDLE_GEN(h) (h >> 16)
#define NEW_HANDLE(ndx, gen) ((gen << 16) | (ndx & 0xffff))

struct gx2_texture {
	GX2Texture t;
	uint16_t generation;
};

static struct gx2_texture gx2_textures[2048] = {};
static uint16_t gx2_texture_free = 1;

static struct gx2_texture *get_gx2_texture(const uint32_t handle) {
	if (!handle) return NULL;
	const uint16_t ndx = HANDLE_NDX(handle), gen = HANDLE_GEN(handle);

	struct gx2_texture *tex = &gx2_textures[ndx];
	if (tex->generation != gen) return NULL;

	return tex;
}

static GX2Sampler sampler_sharp;

EXPORT boolean HWRAPI(Init)(void) {
	DEBUG("Init");
	vcache_init(&vertex_cache, 128 * 1024 * sizeof(FOutVector), GX2_VERTEX_BUFFER_ALIGNMENT,
	            GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, false);
	vcache_init(&ubo_cache, 2 * 1024 * 1024, GX2_UNIFORM_BLOCK_ALIGNMENT,
	            GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_UNIFORM_BLOCK, true);
	vcache_init(&index_cache, 128 * 1024, GX2_INDEX_BUFFER_ALIGNMENT, GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, false);

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

	GX2InitSampler(&sampler_sharp, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);

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
	vcache_reset(&index_cache);
}

EXPORT void HWRAPI(Draw2DLine)(F2DCoord *v1, F2DCoord *v2, RGBA_t Color) {
	DEBUG("Draw2DLine");
	// TODO
}

static void PrepDraw(FSurfaceInfo *pSurf, const FOutVector *pOutVerts, const FUINT iNumPts, FBITFIELD PolyFlags) {
	// TODO Blend PolyFlags, pSurf

	const struct block verts = vcache_add(&vertex_cache, pOutVerts, sizeof(pOutVerts[0]) * iNumPts);
	GX2SetAttribBuffer(aPosition, verts.size, sizeof(FOutVector), verts.data);
	GX2SetAttribBuffer(aTexCoord, verts.size, sizeof(FOutVector), verts.data);

	GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_NOT_EQUAL, 0.0f);
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
}

EXPORT void HWRAPI(DrawPolygon)(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags) {
	DEBUG("DrawPolygon");

	PrepDraw(pSurf, pOutVerts, iNumPts, PolyFlags);
	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_FAN, iNumPts, 0, 1);
}

EXPORT void HWRAPI(DrawIndexedTriangles)(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags,
                                         UINT32 *IndexArray) {
	DEBUG("DrawIndexedTriangles");

	PrepDraw(pSurf, pOutVerts, iNumPts, PolyFlags);
        const struct block indexes = vcache_add(&index_cache, IndexArray, sizeof(IndexArray[0]) * iNumPts);
	GX2DrawIndexedEx(GX2_PRIMITIVE_MODE_TRIANGLES, iNumPts, GX2_INDEX_TYPE_U32, indexes.data, 0, 1);
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
	if (!TexInfo) {
		DEBUG("null texture");
		return;
	}
	const struct gx2_texture *tex = get_gx2_texture(TexInfo->downloaded);
	if (!tex) {
		DEBUG("fixing texture");
		UpdateTexture(TexInfo);
		tex = get_gx2_texture(TexInfo->downloaded);
	}

	GX2SetPixelSampler(&sampler_sharp, 0);
	GX2SetPixelTexture(&tex->t, 0);
}

EXPORT void HWRAPI(UpdateTexture)(GLMipmap_t *TexInfo) {
	DEBUG("UpdateTexture");
	struct gx2_texture *gtex = get_gx2_texture(TexInfo->downloaded);
	if (!gtex) {
		// Find a free slot
		unsigned int index;
		for (index = 0; index < ARRAY_SIZE(gx2_textures); index++) {
			if (gx2_textures[index].t.surface.image == NULL) {
				gx2_texture_free = index;
				break;
			}
		}
		if (index >= ARRAY_SIZE(gx2_textures)) {
			printf("Texture cache full!");
			return;
		}
		TexInfo->downloaded = NEW_HANDLE(index, ++gx2_textures[index].generation);
		gtex = get_gx2_texture(TexInfo->downloaded);
	}
	GX2Texture *tex = &gtex->t;

	tex->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	tex->surface.width = TexInfo->width;
	tex->surface.height = TexInfo->height;
	tex->surface.depth = 1;
	tex->surface.mipLevels = 1;
	tex->surface.aa = GX2_AA_MODE1X;
	tex->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	tex->surface.swizzle = 0;
	tex->viewNumMips = 1;
	tex->viewNumSlices = 1;

	int src_bpp = 4;
	switch (TexInfo->format) {
		// Convert indexed-colour formats to RGBA (for now)
		case GL_TEXFMT_P_8:
			src_bpp = 1;
			tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
			tex->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
			break;
		case GL_TEXFMT_AP_88:
			src_bpp = 2;
			tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
			tex->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
			break;
		case GL_TEXFMT_RGBA:
			src_bpp = 4;
			tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
			tex->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
			break;
		case GL_TEXFMT_ALPHA_8:
			src_bpp = 1;
			tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8;
			tex->compMap = GX2_COMP_MAP(GX2_SQ_SEL_1, GX2_SQ_SEL_1, GX2_SQ_SEL_1, GX2_SQ_SEL_R);
			break;
		case GL_TEXFMT_INTENSITY_8:
			src_bpp = 1;
			tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8;
			tex->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_1);
			break;
		case GL_TEXFMT_ALPHA_INTENSITY_88:
			src_bpp = 2;
			tex->surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8;
			tex->compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_G);
			break;
	}

	GX2RCreateSurface(&tex->surface, GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_USAGE_CPU_WRITE |
				      GX2R_RESOURCE_USAGE_GPU_READ);
	GX2InitTextureRegs(tex);

	void *pixel_data = GX2RLockSurfaceEx(&tex->surface, 0, GX2R_RESOURCE_USAGE_CPU_WRITE);
	const uint8_t *src = TexInfo->data;

	if (TexInfo->format == GL_TEXFMT_P_8 || TexInfo->format == GL_TEXFMT_AP_88) {
		// Palette conversion
		for (int y = 0; y < TexInfo->height; y++) {
			RGBA_t *line = pixel_data + tex->surface.pitch * sizeof(RGBA_t) * y;
			for (int x = 0; x < TexInfo->width; x++) {
				const uint8_t a = TexInfo->format == GL_TEXFMT_AP_88 ? *src++ : 0;
				const uint8_t p = *src++;

				if (p == HWR_PATCHES_CHROMAKEY_COLORINDEX && TexInfo->flags & TF_CHROMAKEYED) {
					line[x] = (RGBA_t){.s = {0, 0, 0, 0}};
					TexInfo->flags |= TF_TRANSPARENT;
				} else {
					line[x] = rgba_palette[p];
				}

				if (TexInfo->format == GL_TEXFMT_AP_88 && !(TexInfo->flags & TF_CHROMAKEYED)) {
					line[x].s.alpha = a;
				}
			}
		}
	} else {
		// Just copy and fix stride
		for (int y = 0; y < TexInfo->height; y++) {
			void *line = pixel_data + tex->surface.pitch * src_bpp * y;
			const void *src_line = src + TexInfo->width * src_bpp * y;
			memcpy(line, src_line, TexInfo->width * src_bpp);
		}
	}

	GX2RUnlockSurfaceEx(&tex->surface, 0, 0);

	// TODO sampler params
}

EXPORT void HWRAPI(DeleteTexture)(GLMipmap_t *TexInfo) {
	DEBUG("DeleteTexture");
	if (!TexInfo) return;
	struct gx2_texture *tex = get_gx2_texture(TexInfo->downloaded);
	if (!tex) return;

	GX2RDestroySurfaceEx(&tex->t.surface, 0);
	tex->t.surface.image = NULL;

	const uint16_t index = HANDLE_NDX(TexInfo->downloaded);
	if (gx2_texture_free > index)
		gx2_texture_free = index;
}

EXPORT void HWRAPI(SetTexturePalette)(RGBA_t *ppal) {
	const size_t pal_size = (sizeof(RGBA_t) * 256);
	// on a palette change, you have to reload all the textures
	if (memcmp(&rgba_palette, ppal, pal_size) != 0) {
		memcpy(&rgba_palette, ppal, pal_size);
		ClearMipMapCache();
	}
}

EXPORT void HWRAPI(ReadScreenTexture)(int tex, UINT8 *dst_data) {
	DEBUG("ReadScreenTexture");
	// TODO !!
}

EXPORT void HWRAPI(GClipRect)(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip) {
	DEBUG("GClipRect");
	GX2SetViewport(minx, miny, maxx - minx, maxy - miny, 0.0f, 1.0f);
	GX2SetScissor(minx, miny, maxx - minx, maxy - miny);
}

EXPORT void HWRAPI(ClearMipMapCache)(void) {
	DEBUG("ClearMipMapCache");
	for (int i = 0; i < ARRAY_SIZE(gx2_textures); i++) {
		struct gx2_texture *tex = &gx2_textures[i];
		if (!tex->t.surface.image) continue;

		GX2RDestroySurfaceEx(&tex->t.surface, 0);
		tex->t.surface.image = NULL;
		tex->generation++;
	}

	gx2_texture_free = 1;
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
		glm_perspective(glm_rad(afov), 2 * ASPECT_RATIO, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE,
		                ubo.uProjection);
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
