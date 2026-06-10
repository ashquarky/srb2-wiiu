target_sources(SRB2SDL2 PRIVATE
	hw_bsp.c
	hw_draw.c
	hw_light.c
	hw_main.c
	hw_clip.c
	hw_md2.c
	hw_cache.c
	hw_md2load.c
	hw_md3load.c
	hw_model.c
	hw_batching.c
)

if(WIIU)
	target_sources(SRB2SDL2 PRIVATE r_gx2/r_gx2.c r_gx2/r_gx2mm.c)
else()
	target_sources(SRB2SDL2 PRIVATE r_opengl/r_opengl.c)
endif()
