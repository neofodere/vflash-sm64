#include <ultra64.h>
#include "gfx.h"
#include "vf_hw.h"
#include "vf_port.h"

#define TEXGEN_HALF 0x4000
#define OP_MTX 0x01
#define OP_MOVEMEM 0x03
#define OP_VTX 0x04
#define OP_DL 0x06
#define OP_RDPHALF_2 0xB3
#define OP_RDPHALF_1 0xB4
#define OP_CLEARGEOMETRYMODE 0xB6
#define OP_SETGEOMETRYMODE 0xB7
#define OP_ENDDL 0xB8
#define OP_SETOTHERMODE_L 0xB9
#define OP_SETOTHERMODE_H 0xBA
#define OP_TEXTURE 0xBB
#define OP_MOVEWORD 0xBC
#define OP_POPMTX 0xBD
#define OP_TRI1 0xBF
#define OP_TEXRECT 0xE4
#define OP_TEXRECTFLIP 0xE5
#define OP_SETSCISSOR 0xED
#define OP_LOADTLUT 0xF0
#define OP_SETTILESIZE 0xF2
#define OP_LOADBLOCK 0xF3
#define OP_LOADTILE 0xF4
#define OP_SETTILE 0xF5
#define OP_FILLRECT 0xF6
#define OP_SETFILLCOLOR 0xF7
#define OP_SETFOGCOLOR 0xF8
#define OP_SETBLENDCOLOR 0xF9
#define OP_SETPRIMCOLOR 0xFA
#define OP_SETENVCOLOR 0xFB
#define OP_SETCOMBINE 0xFC
#define OP_SETTIMG 0xFD
#define OP_SETZIMG 0xFE
#define OP_SETCIMG 0xFF

#define MW_NUMLIGHT 0x02
#define MW_SEGMENT 0x06
#define MV_VIEWPORT 0x80
#define MV_LIGHT0 0x86

#define MTX_PROJECTION 0x01
#define MTX_LOAD 0x02
#define MTX_PUSH 0x04

#define OM_L_Z_CMP 0x0010
#define OM_L_FORCE_BL 0x4000
#define CYCLE_FILL 3

#define MV_STACK 10
#define VERT_CACHE 32
#define DL_STACK 16
#define MAX_LIGHTS 8

#define CLIP_NX 0x01
#define CLIP_PX 0x02
#define CLIP_NY 0x04
#define CLIP_PY 0x08
#define CLIP_NEAR 0x10
#define CLIP_FAR 0x20

#define RECT_NEAR_F16 vf_f16(1, 4)
#define Y_SIGN (-1)
#define CULL_FRONT_SIGN 1

typedef struct {
	s32 m[4][4];
} Mat;

typedef struct {
	s64 m[4][4];
} Mat64;

typedef struct {
	s64 x;
	s64 y;
	s64 z;
	s64 w;
	u8 clip;
	u8 shade[4];
	s16 s;
	s16 t;
} Vert;

typedef struct {
	u8 colour[3];
	s8 dir[3];
} LightState;

typedef struct {
	u32 fmt;
	u32 siz;
	u32 line;
	u32 tmem;
	u32 uls;
	u32 ult;
	u32 lrs;
	u32 lrt;
	u32 size_set;
	u32 mask_s;
	u32 mask_t;
	u32 mirror_s;
	u32 mirror_t;
} Tile;

typedef struct {
	u32 tmem;
	const u8 *data;
	u32 stride;
	u32 valid;
} Load;

typedef struct {
	u8 use_tex;
	u8 use_shade;
	u8 use_prim;
	u8 use_env;
	u8 alpha_prim;
	u8 alpha_shade;
} Combine;

static u32 sSeg[16];
static Mat sMv[MV_STACK];
static int sMvTop;
static Mat sProj;
static Mat64 sMp;
static int sMpDirty;

static s16 sVpScale[2];
static s16 sVpTrans[2];

static u32 sGeom;
static u32 sOmL;
static u32 sOmH;

static LightState sLights[MAX_LIGHTS + 1];
static int sNumLights;

static Vert sVerts[VERT_CACHE];

static int sTexOn;
static int sTexTile;
static u32 sTexScaleS;
static u32 sTexScaleT;
static Tile sTiles[8];
static Load sLoads[8];
static int sLoadNext;
static const u8 *sTimgData;
static u32 sTimgSiz;
static u32 sTimgWidth;

static Combine sComb;
static u8 sPrim[4];
static u8 sEnv[4];
static u32 sFill;
static u32 sCimg;
static u32 sZimg;

static u32 sStat[8];

enum { STAT_TRI, STAT_CULL, STAT_RECT, STAT_FILL, STAT_TEX_OK, STAT_TEX_FAIL, STAT_DEPTH };

static GeTexture sTexCur;
static int sTexCurValid;
static int sTexDirty;
static u16 sNearF16;

static void *seg_ptr(u32 addr)
{
	u32 base = sSeg[(addr >> 24) & 15];

	return (void *) (((base + (addr & 0x00FFFFFF)) & 0x00FFFFFF) | VF_RAM_VIRT);
}

static void mat_from_mtx(Mat *out, const u32 *w)
{
	int i;
	int j;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			u32 shift = (j & 1) ? 0 : 16;
			s32 ip = (s16) (w[i * 2 + (j >> 1)] >> shift);
			u32 fp = (w[8 + i * 2 + (j >> 1)] >> shift) & 0xFFFF;

			out->m[i][j] = (s32) ((u32) ip << 16 | fp);
		}
	}
}

static void mat_mul(Mat *out, const Mat *a, const Mat *b)
{
	Mat r;
	int i;
	int j;
	int k;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			s64 acc = 0;

			for (k = 0; k < 4; k++)
				acc += (s64) a->m[i][k] * b->m[k][j];
			r.m[i][j] = (s32) (acc >> 16);
		}
	}
	*out = r;
}

static void update_mp(void)
{
	const Mat *mv = &sMv[sMvTop];
	int i;
	int j;
	int k;

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			s64 acc = 0;

			for (k = 0; k < 4; k++)
				acc += (s64) mv->m[i][k] * sProj.m[k][j];
			sMp.m[i][j] = acc >> 16;
		}
	}
	sMpDirty = 0;
}

static void projection_changed(void)
{
	s64 p22 = sProj.m[2][2];
	s64 p32 = sProj.m[3][2];
	u16 near_f16 = RECT_NEAR_F16;

	if (sProj.m[2][3] == -65536 && p22 != 65536) {
		s64 near_fx = (p32 << 16) / (p22 - 65536);

		if (near_fx > 0)
			near_f16 = vf_f16(near_fx, 16);
	}
	sNearF16 = near_f16;
	ge_set_near(near_f16);
}

static void cmd_matrix(u32 w0, u32 w1)
{
	u32 params = (w0 >> 16) & 0xFF;
	Mat m;

	mat_from_mtx(&m, (const u32 *) seg_ptr(w1));
	if (params & MTX_PROJECTION) {
		if (params & MTX_LOAD)
			sProj = m;
		else
			mat_mul(&sProj, &m, &sProj);
		projection_changed();
	} else {
		if ((params & MTX_PUSH) && sMvTop < MV_STACK - 1) {
			sMv[sMvTop + 1] = sMv[sMvTop];
			sMvTop++;
		}
		if (params & MTX_LOAD)
			sMv[sMvTop] = m;
		else
			mat_mul(&sMv[sMvTop], &m, &sMv[sMvTop]);
	}
	sMpDirty = 1;
}

static u32 isqrt_u64(u64 v)
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

static u32 clamp255(u32 v)
{
	return v > 255 ? 255 : v;
}

static void view_normal(const s8 *nrm, s32 n[3])
{
	const Mat *mv = &sMv[sMvTop];
	s64 nx = 0;
	s64 ny = 0;
	s64 nz = 0;
	u64 len2;
	u32 len;
	int i;

	for (i = 0; i < 3; i++) {
		nx += (s64) nrm[i] * mv->m[i][0];
		ny += (s64) nrm[i] * mv->m[i][1];
		nz += (s64) nrm[i] * mv->m[i][2];
	}
	len2 = (u64) (nx * nx) + (u64) (ny * ny) + (u64) (nz * nz);
	len = isqrt_u64(len2);
	if (len == 0) {
		n[0] = n[1] = n[2] = 0;
	} else {
		u32 shift = 0;
		u32 sl = len;

		while ((sl >> 20) != 0) {
			sl >>= 1;
			shift++;
		}
		n[0] = (s32) ((nx >> shift) * 127 / (s32) sl);
		n[1] = (s32) ((ny >> shift) * 127 / (s32) sl);
		n[2] = (s32) ((nz >> shift) * 127 / (s32) sl);
	}
}

static void light_vertex(Vert *o, const s8 *nrm)
{
	s32 n[3];
	u32 rgb[3];
	int i;
	int k;

	view_normal(nrm, n);

	for (k = 0; k < 3; k++)
		rgb[k] = sLights[sNumLights].colour[k];
	for (i = 0; i < sNumLights; i++) {
		s32 dot = n[0] * sLights[i].dir[0] + n[1] * sLights[i].dir[1] + n[2] * sLights[i].dir[2];

		if (dot <= 0)
			continue;
		for (k = 0; k < 3; k++)
			rgb[k] += (u32) (((u64) sLights[i].colour[k] * (u32) dot * 4161) >> 26);
	}
	for (k = 0; k < 3; k++)
		o->shade[k] = (u8) clamp255(rgb[k]);
}

static void cmd_vertex(u32 w0, u32 w1)
{
	u32 count = ((w0 >> 20) & 0xF) + 1;
	u32 first = (w0 >> 16) & 0xF;
	const Vtx *src = (const Vtx *) seg_ptr(w1);
	u32 i;

	if (sMpDirty)
		update_mp();
	for (i = 0; i < count && first + i < VERT_CACHE; i++, src++) {
		Vert *o = &sVerts[first + i];
		s64 x = src->v.ob[0];
		s64 y = src->v.ob[1];
		s64 z = src->v.ob[2];
		s64 w;
		u8 clip = 0;

		o->x = sMp.m[0][0] * x + sMp.m[1][0] * y + sMp.m[2][0] * z + sMp.m[3][0];
		o->y = sMp.m[0][1] * x + sMp.m[1][1] * y + sMp.m[2][1] * z + sMp.m[3][1];
		o->z = sMp.m[0][2] * x + sMp.m[1][2] * y + sMp.m[2][2] * z + sMp.m[3][2];
		o->w = sMp.m[0][3] * x + sMp.m[1][3] * y + sMp.m[2][3] * z + sMp.m[3][3];
		w = o->w;
		if (o->x < -w)
			clip |= CLIP_NX;
		if (o->x > w)
			clip |= CLIP_PX;
		if (o->y < -w)
			clip |= CLIP_NY;
		if (o->y > w)
			clip |= CLIP_PY;
		if (o->z < -w || w <= 0)
			clip |= CLIP_NEAR;
		if (o->z > w)
			clip |= CLIP_FAR;
		o->clip = clip;

		if (sGeom & G_LIGHTING) {
			light_vertex(o, (const s8 *) src->v.cn);
			o->shade[3] = src->v.cn[3];
		} else {
			o->shade[0] = src->v.cn[0];
			o->shade[1] = src->v.cn[1];
			o->shade[2] = src->v.cn[2];
			o->shade[3] = src->v.cn[3];
		}
		if (sGeom & G_TEXTURE_GEN) {
			s32 n[3];

			view_normal((const s8 *) src->v.cn, n);
			o->s = (s16) (TEXGEN_HALF + n[0] * TEXGEN_HALF / 127);
			o->t = (s16) (TEXGEN_HALF + n[1] * TEXGEN_HALF / 127);
		} else {
			o->s = src->v.tc[0];
			o->t = src->v.tc[1];
		}
	}
}

static void parse_combine(u32 w0, u32 w1)
{
	u32 rgb[8];
	u32 alpha[8];
	u32 two_cycle = ((sOmH >> 20) & 3) == 1;
	int n = two_cycle ? 8 : 4;
	int i;

	rgb[0] = (w0 >> 20) & 15;
	rgb[1] = (w1 >> 28) & 15;
	rgb[2] = (w0 >> 15) & 31;
	rgb[3] = (w1 >> 15) & 7;
	rgb[4] = (w0 >> 5) & 15;
	rgb[5] = (w1 >> 24) & 15;
	rgb[6] = w0 & 31;
	rgb[7] = (w1 >> 6) & 7;
	alpha[0] = (w0 >> 12) & 7;
	alpha[1] = (w1 >> 12) & 7;
	alpha[2] = (w0 >> 9) & 7;
	alpha[3] = (w1 >> 9) & 7;
	alpha[4] = (w1 >> 21) & 7;
	alpha[5] = (w1 >> 3) & 7;
	alpha[6] = (w1 >> 18) & 7;
	alpha[7] = w1 & 7;

	sComb.use_tex = sComb.use_shade = sComb.use_prim = sComb.use_env = 0;
	sComb.alpha_prim = sComb.alpha_shade = 0;
	for (i = 0; i < n; i++) {
		u32 c = rgb[i];
		int is_c = (i & 3) == 2;

		if (c == 1 || (is_c && c == 8))
			sComb.use_tex = 1;
		if (c == 4 || (is_c && c == 11))
			sComb.use_shade = 1;
		if (c == 3 || (is_c && c == 10))
			sComb.use_prim = 1;
		if (c == 5 || (is_c && c == 12))
			sComb.use_env = 1;
		if (alpha[i] == 3)
			sComb.alpha_prim = 1;
		if (alpha[i] == 4)
			sComb.alpha_shade = 1;
	}
	if (!two_cycle && rgb[0] == 3 && rgb[1] == 4 && rgb[2] == 1 && rgb[3] == 4) {
		sComb.use_tex = 0;
		sComb.use_prim = 0;
	}
	sTexDirty = 1;
}

static u32 bits_per_texel(u32 siz)
{
	return 4u << siz;
}

static int resolve_texture(void)
{
	const Tile *t = &sTiles[sTexTile];
	TexSource src;
	u32 width;
	u32 height;
	int i;
	const Load *ld = NULL;

	sTexCurValid = 0;
	for (i = 0; i < 8; i++)
		if (sLoads[i].valid && sLoads[i].tmem == t->tmem)
			ld = &sLoads[i];
	if (!ld)
		return 0;

	if (t->size_set) {
		width = ((t->lrs - t->uls) >> 2) + 1;
		height = ((t->lrt - t->ult) >> 2) + 1;
	} else {
		width = t->mask_s ? 1u << t->mask_s : 32;
		height = t->mask_t ? 1u << t->mask_t : 32;
	}
	src.data = ld->data;
	src.stride = ld->stride ? ld->stride : t->line * 8;
	if (src.stride == 0)
		src.stride = (width * bits_per_texel(t->siz) + 7) / 8;
	src.fmt = t->fmt;
	src.siz = t->siz;
	src.width = width;
	src.height = height;
	sTexCurValid = vf_tex_acquire(&src, &sTexCur);
	sStat[sTexCurValid ? STAT_TEX_OK : STAT_TEX_FAIL]++;
	return sTexCurValid;
}

static void cmd_texture(u32 w0, u32 w1)
{
	sTexOn = w0 & 0xFF;
	sTexTile = (w0 >> 8) & 7;
	sTexScaleS = w1 >> 16;
	sTexScaleT = w1 & 0xFFFF;
	sTexDirty = 1;
}

static void cmd_settile(u32 w0, u32 w1)
{
	u32 idx = (w1 >> 24) & 7;
	Tile *t = &sTiles[idx];

	t->fmt = (w0 >> 21) & 7;
	t->siz = (w0 >> 19) & 3;
	t->line = (w0 >> 9) & 0x1FF;
	t->tmem = w0 & 0x1FF;
	t->mask_t = (w1 >> 14) & 15;
	t->mask_s = (w1 >> 4) & 15;
	t->mirror_t = (w1 >> 18) & 1;
	t->mirror_s = (w1 >> 8) & 1;
	t->size_set = 0;
	sTexDirty = 1;
}

static void cmd_settilesize(u32 w0, u32 w1)
{
	Tile *t = &sTiles[(w1 >> 24) & 7];

	t->uls = (w0 >> 12) & 0xFFF;
	t->ult = w0 & 0xFFF;
	t->lrs = (w1 >> 12) & 0xFFF;
	t->lrt = w1 & 0xFFF;
	t->size_set = 1;
	sTexDirty = 1;
}

static void record_load(u32 tmem, const u8 *data, u32 stride)
{
	int i;
	Load *ld = NULL;

	for (i = 0; i < 8; i++)
		if (sLoads[i].valid && sLoads[i].tmem == tmem)
			ld = &sLoads[i];
	if (!ld) {
		ld = &sLoads[sLoadNext];
		sLoadNext = (sLoadNext + 1) & 7;
	}
	ld->tmem = tmem;
	ld->data = data;
	ld->stride = stride;
	ld->valid = 1;
	sTexDirty = 1;
}

static void cmd_loadblock(u32 w0, u32 w1)
{
	(void) w0;
	record_load(sTiles[(w1 >> 24) & 7].tmem, sTimgData, 0);
}

static void cmd_loadtile(u32 w0, u32 w1)
{
	u32 uls = (w0 >> 12) & 0xFFF;
	u32 ult = w0 & 0xFFF;
	u32 stride = (sTimgWidth * bits_per_texel(sTimgSiz) + 7) / 8;
	u32 offset = (ult >> 2) * stride + ((uls >> 2) * bits_per_texel(sTimgSiz)) / 8;

	record_load(sTiles[(w1 >> 24) & 7].tmem, sTimgData + offset, stride);
}

static void cmd_viewport(u32 w1)
{
	const Vp *vp = (const Vp *) seg_ptr(w1);

	sVpScale[0] = vp->vp.vscale[0];
	sVpScale[1] = vp->vp.vscale[1];
	sVpTrans[0] = vp->vp.vtrans[0];
	sVpTrans[1] = vp->vp.vtrans[1];
	ge_set_origin(sVpTrans[0] / 4, sVpTrans[1] / 4);
}

static void cmd_movemem(u32 w0, u32 w1)
{
	u32 index = (w0 >> 16) & 0xFF;

	if (index == MV_VIEWPORT) {
		cmd_viewport(w1);
	} else if (index >= MV_LIGHT0 && index < MV_LIGHT0 + 2 * (MAX_LIGHTS + 1) && ((index - MV_LIGHT0) & 1) == 0) {
		const Light_t *l = (const Light_t *) seg_ptr(w1);
		LightState *s = &sLights[(index - MV_LIGHT0) / 2];
		int k;

		for (k = 0; k < 3; k++) {
			s->colour[k] = l->col[k];
			s->dir[k] = l->dir[k];
		}
	}
}

static void cmd_moveword(u32 w0, u32 w1)
{
	u32 index = w0 & 0xFF;
	u32 offset = (w0 >> 8) & 0xFFFF;

	if (index == MW_SEGMENT) {
		sSeg[(offset >> 2) & 15] = w1 & 0x00FFFFFF;
	} else if (index == MW_NUMLIGHT) {
		int n = (int) ((w1 & 0xFFFF) >> 5) - 1;

		sNumLights = n < 0 ? 0 : (n > MAX_LIGHTS ? MAX_LIGHTS : n);
	}
}

static void cmd_othermode(u32 w0, u32 w1, u32 *mode)
{
	u32 len = w0 & 0xFF;
	u32 shift = (w0 >> 8) & 0xFF;
	u32 mask = (len >= 32 ? 0xFFFFFFFFu : ((1u << len) - 1)) << shift;

	*mode = (*mode & ~mask) | (w1 & mask);
}

static u16 colour555_from_5551(u32 p)
{
	return (u16) ((p >> 11 & 31) | (p >> 6 & 31) << 5 | (p >> 1 & 31) << 10);
}

static u32 mul255(u32 a, u32 b)
{
	return (a * b * 257 + 32768) >> 16;
}

static int msb_u64(u64 v)
{
	u32 hi = (u32) (v >> 32);

	return hi ? 63 - __builtin_clz(hi) : (v ? 31 - __builtin_clz((u32) v) : 0);
}

static s64 abs64(s64 v)
{
	return v < 0 ? -v : v;
}

static int cull_triangle(const Vert *a, const Vert *b, const Vert *c)
{
	s64 big;
	int shift;
	s64 x0;
	s64 y0;
	s64 w0;
	s64 x1;
	s64 y1;
	s64 w1;
	s64 x2;
	s64 y2;
	s64 w2;
	s64 det;

	if (!(sGeom & (G_CULL_BACK | G_CULL_FRONT)))
		return 0;
	if (a->w <= 0 || b->w <= 0 || c->w <= 0)
		return 0;

	big = abs64(a->x) | abs64(a->y) | a->w | abs64(b->x) | abs64(b->y) | b->w | abs64(c->x) | abs64(c->y) | c->w;
	shift = msb_u64((u64) big) - 18;
	if (shift < 0)
		shift = 0;
	x0 = a->x >> shift;
	y0 = a->y >> shift;
	w0 = a->w >> shift;
	x1 = b->x >> shift;
	y1 = b->y >> shift;
	w1 = b->w >> shift;
	x2 = c->x >> shift;
	y2 = c->y >> shift;
	w2 = c->w >> shift;

	det = x0 * (y1 * w2 - y2 * w1) - y0 * (x1 * w2 - x2 * w1) + w0 * (x1 * y2 - x2 * y1);
	if (det == 0)
		return 1;
	if (CULL_FRONT_SIGN < 0)
		det = -det;
	if (det > 0)
		return (sGeom & G_CULL_FRONT) != 0;
	return (sGeom & G_CULL_BACK) != 0;
}

typedef struct {
	u16 light;
	u8 intensity[3];
	u8 alpha;
} TriShade;

static void compute_shade(const Vert *const v[3], TriShade *out)
{
	u32 chroma[3] = { 255, 255, 255 };
	u32 alpha = 255;
	int k;

	if (sComb.use_shade) {
		u32 avg[3];
		u32 peak = 0;

		for (k = 0; k < 3; k++) {
			avg[k] = ((u32) v[0]->shade[k] + v[1]->shade[k] + v[2]->shade[k]) * 21845 >> 16;
			if (avg[k] > peak)
				peak = avg[k];
		}
		for (k = 0; k < 3; k++) {
			u32 m = v[k]->shade[0];

			if (v[k]->shade[1] > m)
				m = v[k]->shade[1];
			if (v[k]->shade[2] > m)
				m = v[k]->shade[2];
			out->intensity[k] = (u8) m;
		}
		for (k = 0; k < 3; k++)
			chroma[k] = peak ? (avg[k] * 255 + peak / 2) / peak : 0;
	} else {
		out->intensity[0] = out->intensity[1] = out->intensity[2] = 255;
	}

	if (sComb.use_prim) {
		for (k = 0; k < 3; k++)
			chroma[k] = mul255(chroma[k], sPrim[k]);
	} else if (sComb.use_env) {
		for (k = 0; k < 3; k++)
			chroma[k] = mul255(chroma[k], sEnv[k]);
	}
	out->light = (u16) (((chroma[0] * 31 + 127) / 255) | ((chroma[1] * 31 + 127) / 255) << 5
			| ((chroma[2] * 31 + 127) / 255) << 10);

	if (sComb.alpha_shade)
		alpha = mul255(alpha, ((u32) v[0]->shade[3] + v[1]->shade[3] + v[2]->shade[3]) * 21845 >> 16);
	if (sComb.alpha_prim)
		alpha = mul255(alpha, sPrim[3]);
	out->alpha = (u8) alpha;
}

static void apply_render_state(const TriShade *shade, int depth)
{
	ge_set_depth(depth);
	ge_set_blend((sOmL & OM_L_FORCE_BL) != 0, shade->alpha);
	ge_set_light(shade->light);
}

static const GeTexture *select_texture(void)
{
	if (!sComb.use_tex || !sTexOn)
		return ge_white_texture();
	if (sTexDirty) {
		resolve_texture();
		sTexDirty = 0;
	}
	return sTexCurValid ? &sTexCur : ge_white_texture();
}

static u32 pack_uv(const Vert *v, const GeTexture *tex, int textured)
{
	const Tile *t = &sTiles[sTexTile];
	u32 sc_s = sTexScaleS == 0xFFFF ? 0x10000 : sTexScaleS;
	u32 sc_t = sTexScaleT == 0xFFFF ? 0x10000 : sTexScaleT;
	s64 u;
	s64 w;

	if (!textured)
		return 0;
	u = (s64) v->s * sc_s - ((s64) t->uls << 19);
	w = (s64) v->t * sc_t - ((s64) t->ult << 19);
	return (u32) vf_f16(u, 21 + tex->log_w + 3) | (u32) vf_f16(w, 21 + tex->log_h + 3) << 16;
}

static void make_vertex(GeVertex *out, const Vert *v, u32 intensity, const GeTexture *tex, int textured)
{
	u16 x = vf_f16(v->x * sVpScale[0], 18);
	u16 y = vf_f16(Y_SIGN * v->y * sVpScale[1], 18);
	u16 z = vf_f16(v->w, 16);

	out->w0 = x | (u32) y << 16;
	out->w1 = z | (u32) vf_f16_normal_x(intensity) << 16;
	out->w2 = (u32) vf_f16_normal_z(intensity) << 16;
	out->uv = pack_uv(v, tex, textured);
	out->col = 0x7FFF;
}

static void cmd_triangle(u32 w1)
{
	u32 i0 = ((w1 >> 16) & 0xFF) * 205 >> 11;
	u32 i1 = ((w1 >> 8) & 0xFF) * 205 >> 11;
	u32 i2 = (w1 & 0xFF) * 205 >> 11;
	const Vert *v[3];
	const GeTexture *tex;
	TriShade shade;
	GeVertex gv[3];
	int textured;
	int k;

	if (i0 >= VERT_CACHE || i1 >= VERT_CACHE || i2 >= VERT_CACHE)
		return;
	v[0] = &sVerts[i0];
	v[1] = &sVerts[i1];
	v[2] = &sVerts[i2];
	if (v[0]->clip & v[1]->clip & v[2]->clip)
		return;
	if (cull_triangle(v[0], v[1], v[2])) {
		sStat[STAT_CULL]++;
		return;
	}
	sStat[STAT_TRI]++;

	tex = select_texture();
	textured = tex != ge_white_texture();
	compute_shade(v, &shade);
	apply_render_state(&shade, (sGeom & G_ZBUFFER) && (sOmL & OM_L_Z_CMP));
	ge_set_texture(tex);
	for (k = 0; k < 3; k++)
		make_vertex(&gv[k], v[k], shade.intensity[k], tex, textured);
	ge_triangle(gv);
}

static void rect_vertex(GeVertex *out, s32 x, s32 y, s32 s, s32 t, const GeTexture *tex, int textured)
{
	out->w0 = vf_f16(x, 2) | (u32) vf_f16(y, 2) << 16;
	out->w1 = vf_f16(1, 0) | (u32) vf_f16_normal_x(255) << 16;
	out->w2 = (u32) vf_f16_normal_z(255) << 16;
	out->uv = textured ? ((u32) vf_f16(s, 5 + tex->log_w + 3) | (u32) vf_f16(t, 5 + tex->log_h + 3) << 16) : 0;
	out->col = 0x7FFF;
}

static void mirror_span(s32 *c0, s32 *c1, u32 mask, u32 mirror)
{
	s32 period = (s32) (1u << mask) << 5;
	s32 k;

	if (!mirror || !mask)
		return;
	k = *c0 >= 0 ? *c0 / period : -((-*c0 + period - 1) / period);
	if (k & 1) {
		*c0 = (k + 1) * period - *c0;
		*c1 = (k + 1) * period - *c1;
	} else {
		*c0 -= k * period;
		*c1 -= k * period;
	}
}

static void draw_rect(s32 x0, s32 y0, s32 x1, s32 y1, s32 s, s32 t, s32 dsdx, s32 dtdy, int flip)
{
	const GeTexture *tex = select_texture();
	int textured = tex != ge_white_texture();
	TriShade shade;
	GeVertex q[4];
	GeVertex tri[3];
	s32 s1;
	s32 t1;
	Vert dummy;
	const Vert *dv[3] = { &dummy, &dummy, &dummy };
	const Tile *tl = &sTiles[sTexTile];

	sStat[STAT_RECT]++;
	dummy.shade[0] = dummy.shade[1] = dummy.shade[2] = dummy.shade[3] = 255;
	compute_shade(dv, &shade);
	s -= (s32) tl->uls << 3;
	t -= (s32) tl->ult << 3;
	if (flip) {
		s1 = s + (s32) (((s64) (y1 - y0) * dtdy) >> 7);
		t1 = t + (s32) (((s64) (x1 - x0) * dsdx) >> 7);
	} else {
		s1 = s + (s32) (((s64) (x1 - x0) * dsdx) >> 7);
		t1 = t + (s32) (((s64) (y1 - y0) * dtdy) >> 7);
	}
	mirror_span(&s, &s1, tl->mask_s, tl->mirror_s);
	mirror_span(&t, &t1, tl->mask_t, tl->mirror_t);

	ge_set_origin(0, 0);
	ge_set_near(RECT_NEAR_F16);
	apply_render_state(&shade, 0);
	ge_set_texture(tex);
	if (flip) {
		rect_vertex(&q[0], x0, y0, s, t, tex, textured);
		rect_vertex(&q[1], x1, y0, s, t1, tex, textured);
		rect_vertex(&q[2], x1, y1, s1, t1, tex, textured);
		rect_vertex(&q[3], x0, y1, s1, t, tex, textured);
	} else {
		rect_vertex(&q[0], x0, y0, s, t, tex, textured);
		rect_vertex(&q[1], x1, y0, s1, t, tex, textured);
		rect_vertex(&q[2], x1, y1, s1, t1, tex, textured);
		rect_vertex(&q[3], x0, y1, s, t1, tex, textured);
	}
	tri[0] = q[0];
	tri[1] = q[1];
	tri[2] = q[2];
	ge_triangle(tri);
	tri[0] = q[0];
	tri[1] = q[2];
	tri[2] = q[3];
	ge_triangle(tri);
	ge_set_origin(sVpTrans[0] / 4, sVpTrans[1] / 4);
	ge_set_near(sNearF16 ? sNearF16 : RECT_NEAR_F16);
}

static void cmd_fillrect(u32 w0, u32 w1)
{
	s32 x1 = (s32) ((w0 >> 12) & 0xFFF);
	s32 y1 = (s32) (w0 & 0xFFF);
	s32 x0 = (s32) ((w1 >> 12) & 0xFFF);
	s32 y0 = (s32) (w1 & 0xFFF);
	u32 cycle = (sOmH >> 20) & 3;

	if (cycle >= 2) {
		int px0 = x0 >> 2;
		int py0 = y0 >> 2;
		int pw = (x1 >> 2) - px0 + 1;
		int ph = (y1 >> 2) - py0 + 1;

		sStat[STAT_FILL]++;
		if (sCimg == sZimg)
			ge_clear_depth(px0, py0, pw, ph);
		else
			ge_fill(px0, py0, pw, ph, colour555_from_5551(sFill >> 16));
		return;
	}
	draw_rect(x0, y0, x1, y1, 0, 0, 0, 0, 0);
}

static void cmd_texrect(const Gfx *dl, int flip)
{
	u32 w0 = dl[0].words.w0;
	u32 w1 = dl[0].words.w1;
	u32 st = dl[1].words.w1;
	u32 dd = dl[2].words.w1;
	s32 x1 = (s32) ((w0 >> 12) & 0xFFF);
	s32 y1 = (s32) (w0 & 0xFFF);
	s32 x0 = (s32) ((w1 >> 12) & 0xFFF);
	s32 y0 = (s32) (w1 & 0xFFF);

	s32 dsdx = (s16) (dd >> 16);
	s32 dtdy = (s16) dd;

	int tex_on = sTexOn;

	sTexTile = (w1 >> 24) & 7;
	sTexDirty = 1;
	if (((sOmH >> 20) & 3) == 2) {
		x1 += 4;
		y1 += 4;
		dsdx >>= 2;
	}
	Combine comb = sComb;

	sTexOn = 1;
	if (((sOmH >> 20) & 3) == 2) {
		sComb.use_tex = 1;
		sComb.use_shade = sComb.use_prim = sComb.use_env = 0;
		sComb.alpha_prim = sComb.alpha_shade = 0;
	}
	draw_rect(x0, y0, x1, y1, (s16) (st >> 16), (s16) st, dsdx, dtdy, flip);
	sComb = comb;
	sTexOn = tex_on;
}


#define AUDIO_POLL_MASK 0x3F

static void run_dl(const Gfx *dl)
{
	const Gfx *stack[DL_STACK];
	int depth = 0;
	u32 count = 0;

	for (;;) {
		u32 w0;
		u32 w1;

		if ((++count & AUDIO_POLL_MASK) == 0)
			vf_audio_poll();
		w0 = dl->words.w0;
		w1 = dl->words.w1;

		switch (w0 >> 24) {
		case OP_MTX:
			cmd_matrix(w0, w1);
			break;
		case OP_MOVEMEM:
			cmd_movemem(w0, w1);
			break;
		case OP_VTX:
			cmd_vertex(w0, w1);
			break;
		case OP_DL:
			if (depth < DL_STACK && !((w0 >> 16) & 0xFF))
				stack[depth++] = dl + 1;
			dl = (const Gfx *) seg_ptr(w1);
			continue;
		case OP_ENDDL:
			if (depth == 0)
				return;
			dl = stack[--depth];
			continue;
		case OP_POPMTX:
			if (sMvTop > 0)
				sMvTop--;
			sMpDirty = 1;
			break;
		case OP_MOVEWORD:
			cmd_moveword(w0, w1);
			break;
		case OP_TEXTURE:
			cmd_texture(w0, w1);
			break;
		case OP_SETGEOMETRYMODE:
			sGeom |= w1;
			break;
		case OP_CLEARGEOMETRYMODE:
			sGeom &= ~w1;
			break;
		case OP_SETOTHERMODE_L:
			cmd_othermode(w0, w1, &sOmL);
			break;
		case OP_SETOTHERMODE_H:
			cmd_othermode(w0, w1, &sOmH);
			break;
		case OP_TRI1:
			cmd_triangle(w1);
			break;
		case OP_TEXRECT:
		case OP_TEXRECTFLIP:
			cmd_texrect(dl, (w0 >> 24) == OP_TEXRECTFLIP);
			dl += 3;
			continue;
		case OP_SETSCISSOR:
			ge_set_scissor((int) (((w0 >> 12) & 0xFFF) >> 2), (int) ((w0 & 0xFFF) >> 2),
					(int) (((w1 >> 12) & 0xFFF) >> 2), (int) ((w1 & 0xFFF) >> 2));
			break;
		case OP_SETTILE:
			cmd_settile(w0, w1);
			break;
		case OP_SETTILESIZE:
			cmd_settilesize(w0, w1);
			break;
		case OP_LOADBLOCK:
			cmd_loadblock(w0, w1);
			break;
		case OP_LOADTILE:
			cmd_loadtile(w0, w1);
			break;
		case OP_FILLRECT:
			cmd_fillrect(w0, w1);
			break;
		case OP_SETFILLCOLOR:
			sFill = w1;
			break;
		case OP_SETPRIMCOLOR:
			sPrim[0] = (u8) (w1 >> 24);
			sPrim[1] = (u8) (w1 >> 16);
			sPrim[2] = (u8) (w1 >> 8);
			sPrim[3] = (u8) w1;
			break;
		case OP_SETENVCOLOR:
			sEnv[0] = (u8) (w1 >> 24);
			sEnv[1] = (u8) (w1 >> 16);
			sEnv[2] = (u8) (w1 >> 8);
			sEnv[3] = (u8) w1;
			break;
		case OP_SETCOMBINE:
			parse_combine(w0, w1);
			break;
		case OP_SETTIMG:
			sTimgSiz = (w0 >> 19) & 3;
			sTimgWidth = (w0 & 0xFFF) + 1;
			sTimgData = (const u8 *) seg_ptr(w1);
			break;
		case OP_SETZIMG:
			sZimg = w1;
			break;
		case OP_SETCIMG:
			sCimg = w1;
			break;
		default:
			break;
		}
		dl++;
	}
}

void vf_gfx_init(void)
{
	ge_init();
	sMv[0] = (Mat) { { { 65536, 0, 0, 0 }, { 0, 65536, 0, 0 }, { 0, 0, 65536, 0 }, { 0, 0, 0, 65536 } } };
	sProj = sMv[0];
	sVpScale[0] = 640;
	sVpScale[1] = 480;
	sVpTrans[0] = 640;
	sVpTrans[1] = 480;
}

void vf_gfx_vblank(void)
{
	ge_flip();
}

void vf_run_gfx_task(OSTask *task)
{
	vf_tex_new_frame();
	ge_begin_frame();
	sMvTop = 0;
	sMpDirty = 1;
	sTexDirty = 1;
	sGeom = 0;
	sOmL = 0;
	sOmH = 0;
	ge_set_origin(sVpTrans[0] / 4, sVpTrans[1] / 4);
	run_dl((const Gfx *) seg_ptr((u32) (uintptr_t) task->t.data_ptr));
	ge_end_frame();
	if (vf_gfx_frame() % 30 == 0) {
		VF_LOGX("tris", sStat[STAT_TRI]);
		VF_LOGX("culled", sStat[STAT_CULL]);
		VF_LOGX("rects", sStat[STAT_RECT]);
		VF_LOGX("fills", sStat[STAT_FILL]);
		VF_LOGX("tex ok", sStat[STAT_TEX_OK]);
		VF_LOGX("tex fail", sStat[STAT_TEX_FAIL]);
	}
	sStat[STAT_TRI] = sStat[STAT_CULL] = sStat[STAT_RECT] = sStat[STAT_FILL] = 0;
	sStat[STAT_TEX_OK] = sStat[STAT_TEX_FAIL] = 0;
}
