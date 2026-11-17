#ifndef VF_GFX_H
#define VF_GFX_H

#include <ultra64.h>

#define GE_BUF_COUNT 2
#define GE_BUF_X(i) ((i) * 512)
#define GE_SCREEN_W 320
#define GE_SCREEN_H 240
#define GE_TEX_TOP 376

typedef struct {
	u32 w0;
	u32 w1;
	u32 w2;
	u32 uv;
	u32 col;
} GeVertex;

typedef struct {
	u16 x;
	u16 y;
	u8 log_w;
	u8 log_h;
	u8 argb4444;
	u8 valid;
} GeTexture;

typedef struct {
	const u8 *data;
	u32 stride;
	u32 fmt;
	u32 siz;
	u32 width;
	u32 height;
} TexSource;

u16 vf_f16(s64 value, int frac_bits);
u16 vf_f16_normal_x(u32 intensity);
u16 vf_f16_normal_z(u32 intensity);

void ge_init(void);
void ge_begin_frame(void);
void ge_end_frame(void);
void ge_flip(void);
void ge_set_origin(int x, int y);
void ge_set_scissor(int x0, int y0, int x1, int y1);
void ge_set_near(u16 value);
void ge_set_depth(int enabled);
void ge_set_blend(int enabled, u32 alpha);
void ge_set_light(u16 colour);
void ge_set_texture(const GeTexture *tex);
void ge_fill(int x, int y, int w, int h, u16 colour);
void ge_clear_depth(int x, int y, int w, int h);
void ge_triangle(const GeVertex *v);
const GeTexture *ge_white_texture(void);

u32 vf_gfx_frame(void);
void vf_tex_new_frame(void);
int vf_tex_acquire(const TexSource *src, GeTexture *out);
void ge_tex_store(int x, int y, int log_w, int log_h, const u16 *pixels);

#endif
