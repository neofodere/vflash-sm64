#include <ultra64.h>
#include "gfx.h"
#include "vf_hw.h"
#include "vf_port.h"

#define LIST_WORDS 0x18000
#define LIST_RESERVE 64
#define GE_TIMEOUT 40000000

#define F16_ONE 0x3E00
#define DEPTH_FAR 0x1600

#define BUF_ADDR(i) (VF_SURF_BASE + (u32) GE_BUF_X(i) / 32 * 512)

static u32 sList[LIST_WORDS] __attribute__((aligned(64)));
static u32 *sCur;
static u32 *sEnd;

static u16 sNormalX[256];
static u16 sNormalZ[256];

static int sBack;
static int sPresent;
static int sOriginX;
static int sOriginY;
static int sScissor[4];
static u32 sFrame;

static u32 sShadowOrigin;
static u32 sShadowClip0;
static u32 sShadowClip1;
static u32 sShadowTexFmt;
static u32 sShadowTexPos;
static u32 sShadowLight;
static u32 sShadowBlend;
static u32 sShadowBlendK;
static u32 sShadowDepth;
static u32 sShadowNear;

static GeTexture sWhite;

static u32 isqrt64(u64 v)
{
	u64 bit = (u64) 1 << 62;
	u64 res = 0;

	while (bit > v)
		bit >>= 2;
	while (bit) {
		if (v >= res + bit) {
			v -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}
	return (u32) res;
}

static int msb64(u64 v)
{
	u32 hi = (u32) (v >> 32);

	return hi ? 63 - __builtin_clz(hi) : 31 - __builtin_clz((u32) v);
}

u16 vf_f16(s64 value, int frac_bits)
{
	u32 sign = 0;
	u64 mag;
	u32 mant;
	int msb;
	int exp;

	if (value < 0) {
		sign = 0x8000;
		mag = (u64) (-value);
	} else {
		mag = (u64) value;
	}
	if (mag == 0)
		return 0;

	msb = msb64(mag);
	if (msb > 9) {
		mag += (u64) 1 << (msb - 10);
		msb = msb64(mag);
		mant = (u32) (mag >> (msb - 9)) & 511;
	} else {
		mant = (u32) (mag << (9 - msb)) & 511;
	}
	exp = msb - frac_bits + 31;
	if (exp < 1)
		return 0;
	if (exp > 63)
		return (u16) (sign | 0x7FFF);
	return (u16) (sign | ((u32) exp << 9) | mant);
}

u16 vf_f16_normal_x(u32 intensity)
{
	return sNormalX[intensity & 255];
}

u16 vf_f16_normal_z(u32 intensity)
{
	return sNormalZ[intensity & 255];
}

static void build_normal_tables(void)
{
	u32 i;

	for (i = 0; i < 256; i++) {
		u32 nz = (i * 65536 + 127) / 255;
		u32 sq = ((65025 - i * i) << 16) / 65025;
		u32 nx = isqrt64((u64) sq << 16);

		sNormalZ[i] = vf_f16(nz, 16);
		sNormalX[i] = vf_f16(nx, 16);
	}
}

static inline void emit(u32 word)
{
	if (sCur < sEnd)
		*sCur++ = word;
}

static void emit_regs(u32 first, u32 count, const u16 *values)
{
	u32 i;

	emit(0x20000000 | (first << 16) | count);
	for (i = 0; i < count; i += 2) {
		u32 w = values[i];

		if (i + 1 < count)
			w |= (u32) values[i + 1] << 16;
		emit(w);
	}
}

static u16 *surface_pixel(int x, int y)
{
	u32 off = (u32) ((y - VF_SURF_TOP) >> 3) * 16384 + (u32) (x >> 5) * 512 + (u32) (y & 7) * 64 + (u32) (x & 31) * 2;

	return (u16 *) VF_VIRT(VF_SURF_BASE + off);
}

void ge_tex_store(int x, int y, int log_w, int log_h, const u16 *pixels)
{
	int w = 8 << log_w;
	int h = 8 << log_h;
	int row;
	int col;

	for (row = 0; row < h; row++)
		for (col = 0; col < w; col++)
			*surface_pixel(x + col, y + row) = pixels[row * w + col];
}

const GeTexture *ge_white_texture(void)
{
	return &sWhite;
}

void ge_init(void)
{
	u16 white[64];
	u32 i;

	build_normal_tables();

	VF_REG(VF_GE_SURF_CLIP) = (u32) VF_SURF_TOP << 16 | 512;
	VF_REG(VF_GE_SURF_DRAW) = (u32) VF_SURF_TOP << 16 | 512;
	VF_REG(VF_GE_SURF_BASE) = VF_SURF_BASE;

	for (i = 0; i < 64; i++)
		white[i] = 0x7FFF;
	sWhite.x = 0;
	sWhite.y = GE_TEX_TOP;
	sWhite.log_w = 0;
	sWhite.log_h = 0;
	sWhite.valid = 1;
	ge_tex_store(sWhite.x, sWhite.y, 0, 0, white);

	for (i = 0; i < GE_SCREEN_H; i++) {
		int x;

		for (x = 0; x < GE_SCREEN_W; x++) {
			*surface_pixel(GE_BUF_X(0) + x, VF_SURF_TOP + (int) i) = 0;
			*surface_pixel(GE_BUF_X(1) + x, VF_SURF_TOP + (int) i) = 0;
		}
	}
	vf_drain_writes();

	VF_REG(VF_VE_LAYER_MODE) |= 4;
	VF_REG(VF_VE_LAYER0_ADDR) = BUF_ADDR(0);
	VF_REG(VF_VE_WINDOW_START) = 0;
	VF_REG(VF_VE_WINDOW_END) = (GE_SCREEN_W - 1) | (GE_SCREEN_H - 1) << 16;
	VF_REG(VF_VE_LAYER_ENABLE) |= 0x10;

	sBack = 1;
}

static void invalidate_shadow(void)
{
	sShadowOrigin = 0xFFFFFFFF;
	sShadowClip0 = 0xFFFFFFFF;
	sShadowClip1 = 0xFFFFFFFF;
	sShadowTexFmt = 0xFFFFFFFF;
	sShadowTexPos = 0xFFFFFFFF;
	sShadowLight = 0xFFFFFFFF;
	sShadowBlend = 0xFFFFFFFF;
	sShadowBlendK = 0xFFFFFFFF;
	sShadowDepth = 0xFFFFFFFF;
	sShadowNear = 0xFFFFFFFF;
}

void ge_begin_frame(void)
{
	static const u16 scale[3] = { F16_ONE, F16_ONE, F16_ONE };
	static const u16 ident[9] = { F16_ONE, 0, 0, 0, F16_ONE, 0, 0, 0, F16_ONE };
	static const u16 zero3[3] = { 0, 0, 0 };
	static const u16 light_dir[3] = { 0, 0, F16_ONE };
	static const u16 lights_rest[7] = { 0, 0, 0, 0, 0, 0, 0 };
	static const u16 focal[2] = { F16_ONE, F16_ONE };
	static const u16 ambient[1] = { 0 };

	sCur = sList;
	sEnd = sList + LIST_WORDS - LIST_RESERVE;
	sFrame++;
	invalidate_shadow();

	emit_regs(0x3C, 3, scale);
	emit_regs(0x40, 9, ident);
	emit_regs(0x4C, 3, zero3);
	emit_regs(0x50, 9, ident);
	emit_regs(0x5C, 3, zero3);
	emit_regs(0x60, 3, light_dir);
	emit_regs(0x63, 7, lights_rest);
	emit_regs(0x70, 1, ambient);
	emit_regs(0x78, 2, focal);
	emit(0x84000017);

	sOriginX = 0;
	sOriginY = 0;
	sScissor[0] = 0;
	sScissor[1] = 0;
	sScissor[2] = GE_SCREEN_W;
	sScissor[3] = GE_SCREEN_H;
}

static void sync_origin(void)
{
	u32 x = (u32) (GE_BUF_X(sBack) + sOriginX);
	u32 y = (u32) (VF_SURF_TOP + sOriginY);
	u32 v = y << 12 | x;

	if (v != sShadowOrigin) {
		emit(0x81000000 | v);
		sShadowOrigin = v;
	}
}

static void sync_clip(void)
{
	int x0 = sScissor[0] < 0 ? 0 : sScissor[0];
	int y0 = sScissor[1] < 0 ? 0 : sScissor[1];
	int x1 = sScissor[2] > GE_SCREEN_W ? GE_SCREEN_W : sScissor[2];
	int y1 = sScissor[3] > GE_SCREEN_H ? GE_SCREEN_H : sScissor[3];
	u32 c0;
	u32 c1;

	if (x1 <= x0 || y1 <= y0) {
		x1 = x0 + 1;
		y1 = y0 + 1;
	}
	c0 = (u32) (VF_SURF_TOP + y0) << 12 | (u32) (GE_BUF_X(sBack) + x0);
	c1 = (u32) (VF_SURF_TOP + y1 - 1) << 12 | (u32) (GE_BUF_X(sBack) + x1 - 1);
	if (c0 != sShadowClip0) {
		emit(0x82000000 | c0);
		sShadowClip0 = c0;
	}
	if (c1 != sShadowClip1) {
		emit(0x83000000 | c1);
		sShadowClip1 = c1;
	}
}

void ge_set_origin(int x, int y)
{
	sOriginX = x;
	sOriginY = y;
}

void ge_set_scissor(int x0, int y0, int x1, int y1)
{
	sScissor[0] = x0;
	sScissor[1] = y0;
	sScissor[2] = x1;
	sScissor[3] = y1;
}

void ge_set_near(u16 value)
{
	if (value != sShadowNear) {
		u16 v[1];

		v[0] = value;
		emit_regs(0x74, 1, v);
		sShadowNear = value;
	}
}

void ge_set_depth(int enabled)
{
	u32 v = enabled ? 0xE230 : 0x4230;

	if (v != sShadowDepth) {
		emit(0x1E000000 | v);
		sShadowDepth = v;
	}
}

void ge_set_blend(int enabled, u32 alpha)
{
	u32 mode = enabled ? 2 : 0;
	u32 k = (alpha & 255) * 0x010101;

	if (mode != sShadowBlend) {
		emit(0x88000000 | mode);
		sShadowBlend = mode;
	}
	if (enabled && k != sShadowBlendK) {
		emit(0x8D000000 | k);
		sShadowBlendK = k;
	}
}

void ge_set_light(u16 colour)
{
	if (colour != sShadowLight) {
		u16 v[1];

		v[0] = colour;
		emit_regs(0x6C, 1, v);
		sShadowLight = colour;
	}
}

void ge_set_texture(const GeTexture *tex)
{
	u32 fmt = (u32) tex->log_w | (u32) tex->log_h << 3 | (tex->argb4444 ? 0x100 : 0);
	u32 pos = (u32) tex->y << 12 | tex->x;

	if (fmt != sShadowTexFmt) {
		emit(0x86000000 | fmt);
		sShadowTexFmt = fmt;
	}
	if (pos != sShadowTexPos) {
		emit(0x87000000 | pos);
		sShadowTexPos = pos;
	}
}

void ge_fill(int x, int y, int w, int h, u16 colour)
{
	if (w <= 0 || h <= 0)
		return;
	sync_clip();
	emit(0xD3000003);
	emit((u32) (VF_SURF_TOP + y) << 16 | (u32) (GE_BUF_X(sBack) + x));
	emit((u32) h << 16 | (u32) w);
	emit(colour);
}

void ge_clear_depth(int x, int y, int w, int h)
{
	int saved[4];

	(void) x;
	(void) y;
	(void) w;
	(void) h;
	saved[0] = sScissor[0];
	saved[1] = sScissor[1];
	saved[2] = sScissor[2];
	saved[3] = sScissor[3];
	ge_set_scissor(0, 0, GE_SCREEN_W, GE_SCREEN_H);
	sync_clip();
	emit(0x85078100);
	emit(0xD3000003);
	emit(0);
	emit((u32) GE_SCREEN_H << 16 | GE_SCREEN_W);
	emit(DEPTH_FAR);
	emit(0x96000000);
	ge_set_scissor(saved[0], saved[1], saved[2], saved[3]);
}

void ge_triangle(const GeVertex *v)
{
	u32 i;

	if (sEnd - sCur < 17)
		return;
	sync_origin();
	sync_clip();
	emit(0x40380003);
	for (i = 0; i < 3; i++) {
		emit(v[i].w0);
		emit(v[i].w1);
		emit(v[i].w2);
		emit(v[i].uv);
		emit(v[i].col);
	}
}

static void run_list(void)
{
	u32 spin = 0;

	vf_drain_writes();
	VF_REG(VF_GE_LIST) = VF_PHYS(sList);
	VF_REG(VF_GE_KICK) = 1;
	while (!(VF_REG(VF_GE_STATUS) & 8)) {
		if (++spin > GE_TIMEOUT) {
			VF_LOG("ge timeout");
			break;
		}
	}
	VF_REG(VF_GE_STATUS) = 0x3F;
}

void ge_end_frame(void)
{
	sEnd = sList + LIST_WORDS;
	emit(0x1400FD01);
	run_list();
	sPresent = 1;
}

void ge_flip(void)
{
	if (!sPresent)
		return;
	sPresent = 0;
	VF_REG(VF_VE_LAYER0_ADDR) = BUF_ADDR(sBack);
	sBack ^= 1;
}

u32 vf_gfx_frame(void)
{
	return sFrame;
}
