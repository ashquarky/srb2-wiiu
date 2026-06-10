//
// Created by ash on 9/09/24.
//

#include "r_gx2.h"
#include <stdarg.h>
#include <math.h>
#include <gx2/clear.h>
#include <malloc.h>
#include "../../r_local.h" // For rendertimefrac, used for the leveltime shader uniform
#include "SDL.h"
#include "../../sdl/ogl_sdl.h"
#include "r_gx2mm.h"

#include "CafeGLSLCompiler.h"

#include <gx2/context.h>
#include <gx2/surface.h>
#include <gx2/registers.h>
#include <gx2/enum.h>
#include <gx2/context.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/shaders.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <gx2r/surface.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <gx2/draw.h>

// lots of stub functions during development
#pragma GCC diagnostic ignored "-Wunused-parameter"

static SDL_Texture *main_texture;
static GX2ColorBuffer main_cbuf;
static GX2DepthBuffer main_dbuf;
static GX2ContextState *ctx;
static GX2Sampler nearest_sampler;

EXPORT boolean HWRAPI(Init)(void) {
	gx2mm_init();

	ctx = memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
	memset(ctx, 0, sizeof(*ctx));
	GX2SetupContextStateEx(ctx, TRUE);
	GX2SetContextState(ctx);

	GX2SetAlphaTest(TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
	GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_NEVER);
	GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);

	GLSL_Init();
	CompileShaders();

	GX2InitSampler(&nearest_sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);

	return true;
}

#define GLSL_DEFAULT_VERTEX_SHADER \
        "#version 450\n"                           \
        "layout(location = 0) in vec3 in_position;\n" \
        "layout(location = 1) in vec2 in_texcoord;\n"                           \
        "layout(binding = 0) uniform Projection { mat4 projection; };\n" \
        "layout(binding = 1) uniform ModelView { mat4 modelview; };\n"         \
        "layout(location = 0) out vec2 out_texcoord;\n"                           \
        "void main()\n" \
        "{\n" \
                "gl_Position = vec4(in_position, 1.0); //projection * modelview * vec4(in_position, 1.0);\n" \
                "out_texcoord = in_texcoord;\n" \
        "}\0"

#define GLSL_DEFAULT_FRAGMENT_SHADER \
        "#version 450\n"                                                        \
        "layout(location = 0) in vec2 in_texcoord;\n"                             \
        "layout(binding = 0) uniform sampler2D tex;\n" \
        "layout(binding = 1) uniform PolyColor { vec4 poly_color; };\n"                          \
        "out vec4 out_color;\n"                            \
        "void main(void) {\n" \
                "out_color = poly_color;//texture(tex, in_texcoord) * poly_color;\n" \
        "}\0"

static struct {
    WHBGfxShaderGroup s;
    int in_position;
    int in_texcoord;
    int vu_projection;
    int vu_modelview;
    int pu_tex;
    int pu_poly_color;
} default_shader;

EXPORT boolean HWRAPI(CompileShaders)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
	char info_log[1024];

	default_shader.s.vertexShader = GLSL_CompileVertexShader(GLSL_DEFAULT_VERTEX_SHADER, info_log, sizeof(info_log),
								 GLSL_COMPILER_FLAG_NONE);
	if (!default_shader.s.vertexShader) {
		WHBLogWritef("HW(%s): Vertex shader failed. %s\n", __FUNCTION__, info_log);
		return false;
	}

	default_shader.s.pixelShader = GLSL_CompilePixelShader(GLSL_DEFAULT_FRAGMENT_SHADER, info_log, sizeof(info_log),
							       GLSL_COMPILER_FLAG_NONE);
	if (!default_shader.s.pixelShader) {
		WHBLogWritef("HW(%s): Pixel shader failed. %s\n", __FUNCTION__, info_log);
		return false;
	}

	int buffer = 0;
	default_shader.in_position = buffer++;
	WHBGfxInitShaderAttribute(&default_shader.s, "in_position", default_shader.in_position, 0,
				  GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
	default_shader.in_texcoord = buffer++;
	WHBGfxInitShaderAttribute(&default_shader.s, "in_texcoord", default_shader.in_texcoord, 0,
				  GX2_ATTRIB_FORMAT_FLOAT_32_32);

	default_shader.vu_projection = 0;
	default_shader.vu_modelview = 1;
	default_shader.pu_tex = 0;
	default_shader.pu_poly_color = 1;

	WHBGfxInitFetchShader(&default_shader.s);

	return true;
}

EXPORT void HWRAPI(CleanShaders)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(SetShader)(int type) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(UnSetShader)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(SetShaderInfo)(hwdshaderinfo_t info, INT32 value) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(LoadCustomShader)(int number, char *code, size_t size, boolean isfragment) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(SetPalette)(RGBA_t *ppal) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(FinishUpdate)(INT32 waitvbl) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

[[gnu::aligned(GX2_UNIFORM_BLOCK_ALIGNMENT)]]
static float u_projection[4][4];

[[gnu::aligned(GX2_UNIFORM_BLOCK_ALIGNMENT)]]
static float u_identity[4][4] = {
	{1.0f, 0.0f, 0.0f, 0.0f},
	{0.0f, 1.0f, 0.0f, 0.0f},
	{0.0f, 0.0f, 1.0f, 0.0f},
	{0.0f, 0.0f, 0.0f, 1.0f},
};

static void perspective(float fovy, float aspect) {
	float m[4][4] =
		{
			{1.0f, 0.0f, 0.0f, 0.0f},
			{0.0f, 1.0f, 0.0f, 0.0f},
			{0.0f, 0.0f, 1.0f, -1.0f},
			{0.0f, 0.0f, 0.0f, 0.0f},
		};
	// TODO fixup near/far clip planes
	const float zNear = 0.1f;//NEAR_CLIPPING_PLANE;
	const float zFar = 2000.0f;//FAR_CLIPPING_PLANE;
	const float radians = (fovy / 2.0f * (float) M_PI / 180.0f);
	const float sine = sinf(radians);
	const float deltaZ = zFar - zNear;
	float cotangent;

	if ((fabsf(deltaZ) < 1.0E-36f) || fpclassify(sine) == FP_ZERO || fpclassify(aspect) == FP_ZERO) {
		return;
	}
	cotangent = cosf(radians) / sine;

	m[0][0] = cotangent / aspect;
	m[1][1] = cotangent;
	m[2][2] = -(zFar + zNear) / deltaZ;
	m[3][2] = -2.0f * zNear * zFar / deltaZ;

	memcpy(u_projection, m, sizeof(u_projection));
	GX2Invalidate(GX2_INVALIDATE_MODE_UNIFORM_BLOCK, u_projection, sizeof(u_projection));
}

EXPORT void HWRAPI(Draw2DLine)(F2DCoord *v1, F2DCoord *v2, RGBA_t Color) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(DrawPolygon)(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags) {
	//WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
	size_t bufsize = sizeof(FOutVector) * iNumPts;
	unsigned char *buffer = gx2mm_alloc(bufsize, GX2_VERTEX_BUFFER_ALIGNMENT);
	if (!buffer) return;

	memcpy(buffer, pOutVerts, bufsize);
	for (int i = 0; i < (int)iNumPts; i++) {
		FOutVector ivec = pOutVerts[i];
		WHBLogWritef("HW(%s): Vec %d %0.2f %0.2f %0.2f %0.2f %0.2f %0.2f\n", __FUNCTION__, i,
			     ivec.x, ivec.y, ivec.z, ivec.s, ivec.t);
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, buffer, bufsize);

	// he he he he he
	GX2SetAttribBuffer(default_shader.in_position, bufsize, sizeof(FOutVector), buffer + offsetof(FOutVector, x));
	GX2SetAttribBuffer(default_shader.in_texcoord, bufsize, sizeof(FOutVector), buffer + offsetof(FOutVector, s));

	GX2SetVertexShader(default_shader.s.vertexShader);
	GX2SetPixelShader(default_shader.s.pixelShader);
	GX2SetFetchShader(&default_shader.s.fetchShader);

	//GX2SetVertexUniformBlock(default_shader.vu_projection, sizeof(u_projection), u_projection);
	GX2SetVertexUniformBlock(default_shader.vu_projection, sizeof(u_identity), u_identity);
	GX2SetVertexUniformBlock(default_shader.vu_modelview, sizeof(u_identity), u_identity);

	//yikes
	float *poly_color = gx2mm_alloc(sizeof(float) * 4, GX2_UNIFORM_BLOCK_ALIGNMENT);
	if (!poly_color) return;
	poly_color[0] = 1.0f;//(float)pSurf->PolyColor.s.red / 255.0f;
	poly_color[1] = 1.0f;//(float)pSurf->PolyColor.s.green / 255.0f;
	poly_color[2] = 1.0f;//(float)pSurf->PolyColor.s.blue / 255.0f;
	poly_color[3] = 1.0f;//(float)pSurf->PolyColor.s.alpha / 255.0f;
	GX2Invalidate(GX2_INVALIDATE_MODE_UNIFORM_BLOCK, poly_color, sizeof(float) * 4);

	GX2SetPixelUniformBlock(default_shader.pu_poly_color, sizeof(float) * 4, poly_color);
	GX2SetPixelSampler(&nearest_sampler, default_shader.pu_tex);

	GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLES, iNumPts, 0, 1);
}

EXPORT void HWRAPI(DrawIndexedTriangles)(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags,
					 UINT32 *IndexArray) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(RenderSkyDome)(gl_sky_t *sky) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(SetBlend)(FBITFIELD PolyFlags) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(ClearBuffer)(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor) {
//	WHBLogWritef("HW(%s): %0.2f %0.2f %0.2f %0.2f\n", __FUNCTION__, ClearColor->red, ClearColor->green,
//		    ClearColor->blue, ClearColor->alpha);
	if (ColorMask) {
		GX2ClearColor(&main_cbuf, ClearColor->red, ClearColor->green, ClearColor->blue, ClearColor->alpha);
	}
	if (DepthMask) {
		GX2ClearDepthStencilEx(&main_dbuf, main_dbuf.depthClear, 0, GX2_CLEAR_FLAGS_DEPTH);
	}

	GX2SetContextState(ctx);
}

EXPORT void HWRAPI(SetTexture)(GLMipmap_t *TexInfo) {
	//WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(UpdateTexture)(GLMipmap_t *TexInfo) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(DeleteTexture)(GLMipmap_t *TexInfo) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(ReadRect)(INT32 x, INT32 y, INT32 width, INT32 height, INT32 dst_stride, UINT16 *dst_data) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(GClipRect)(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(ClearMipMapCache)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(SetSpecialState)(hwdspecialstate_t IdState, INT32 Value) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void
HWRAPI(DrawModel)(model_t *model, INT32 frameIndex, float duration, float tics, INT32 nextFrameIndex, FTransform *pos,
		  float hscale, float vscale, UINT8 flipped, UINT8 hflipped, FSurfaceInfo *Surface) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(CreateModelVBOs)(model_t *model) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(SetTransform)(FTransform *ptransform) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT INT32 HWRAPI(GetTextureUsed)(void) { return 0; }

EXPORT void HWRAPI(FlushScreenTextures)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(StartScreenWipe)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(EndScreenWipe)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(DoScreenWipe)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
	ClearBuffer(TRUE, TRUE, &(FRGBAFloat) {1.0f, 0.0f, 0.0f, 1.0f});
}

EXPORT void HWRAPI(DrawIntermissionBG)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(MakeScreenTexture)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(MakeScreenFinalTexture)(void) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(DrawScreenFinalTexture)(int width, int height) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

#define SCREENVERTS 10
EXPORT void HWRAPI(PostImgRedraw)(float points[SCREENVERTS][SCREENVERTS][2]) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

EXPORT void HWRAPI(OglSdlSetPalette)(RGBA_t *palette) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");
}

void OglSdlFinishUpdate(boolean waitvbl) {
	WHBLogWritef("HW(%s): %s\n", __FUNCTION__, "here");

	// hard sync and clean buffers
	GX2DrawDone();
	//gx2mm_reset();

	// copy our work into SDL-land
	uint8_t *src_px = GX2RLockSurfaceEx(&main_cbuf.surface, 0, 0);
	uint8_t *dst_px;
	int pitch;
	int res = SDL_LockTexture(main_texture, NULL, (void **) &dst_px, &pitch);
	if (res < 0) {
		WHBLogWritef("HW(%s): %s\n", __FUNCTION__, SDL_GetError());
	}

	for (int y = 0; y < (int) main_cbuf.surface.height; y++) {
		unsigned int src = y * main_cbuf.surface.pitch * 4; // gx2's pitch is in pixels
		unsigned int dst = y * pitch;

		memcpy(&dst_px[dst], &src_px[src], min(pitch, (int) main_cbuf.surface.pitch * 4));
	}

	SDL_UnlockTexture(main_texture);
	GX2RUnlockSurfaceEx(&main_cbuf.surface, 0, 0);

	// present
	SDL_SetRenderTarget(renderer, NULL);
	SDL_RenderCopy(renderer, main_texture, NULL, NULL);
	SDL_RenderPresent(renderer);

	// setup for next frame
	GX2SetContextState(ctx);
	GX2SetColorBuffer(&main_cbuf, GX2_RENDER_TARGET_0);
}

boolean OglSdlSurface(INT32 w, INT32 h) {
	WHBLogWritef("HW(%s): creating %dx%d\n", __FUNCTION__, w, h);

	// render target cbuf
	main_cbuf.surface = (GX2Surface) {
		.dim = GX2_SURFACE_DIM_TEXTURE_2D,
		.width = w,
		.height = h,
		.depth = 1,
		.mipLevels = 1,
		.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8,
		.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED,
	};
	GX2CalcSurfaceSizeAndAlignment(&main_cbuf.surface);

	main_cbuf.viewNumSlices = 1;
	GX2InitColorBufferRegs(&main_cbuf);

	boolean res = GX2RCreateSurface(
		&main_cbuf.surface,
		GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_BIND_COLOR_BUFFER |
		GX2R_RESOURCE_USAGE_GPU_READ | GX2R_RESOURCE_USAGE_GPU_WRITE |
		GX2R_RESOURCE_USAGE_CPU_READ
	);
	if (!res) return false;

	// render target dbuf
	main_dbuf.surface = (GX2Surface) {
		.dim = GX2_SURFACE_DIM_TEXTURE_2D,
		.width = w,
		.height = h,
		.depth = 1,
		.mipLevels = 1,
		.format = GX2_SURFACE_FORMAT_UNORM_R24_X8,
		.tileMode = GX2_TILE_MODE_DEFAULT,
	};
	GX2CalcSurfaceSizeAndAlignment(&main_dbuf.surface);

	main_dbuf.viewNumSlices = 1;
	main_dbuf.depthClear = 1.0f;
	GX2InitDepthBufferRegs(&main_dbuf);

	res = GX2RCreateSurface(
		&main_dbuf.surface,
		GX2R_RESOURCE_BIND_DEPTH_BUFFER  |
		GX2R_RESOURCE_USAGE_GPU_READ | GX2R_RESOURCE_USAGE_GPU_WRITE
	);
	if (!res) return false;

	// output sdl texture
	main_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!main_texture) return false;

	// context state setup
	GX2SetContextState(ctx);
	GX2SetColorBuffer(&main_cbuf, GX2_RENDER_TARGET_0);
	GX2SetDepthBuffer(&main_dbuf);
	GX2SetViewport(0, 0, (float) main_cbuf.surface.width, (float) main_cbuf.surface.height, 0.0f, 1000.0f);
	GX2SetScissor(0, 0, main_cbuf.surface.width, main_cbuf.surface.height);

	perspective(70.0f, (float) w / (float) h);

	return true;
}