#include <ultra64.h>
#include "vf_port.h"

#define DMEM_SIZE 0x1000
#define DMEM_MASK 0xFFF
#define DRAM_BASE 0x80000000u
#define DRAM_MASK 0x00FFFFFFu
#define BOOK_ENTRIES 256
#define ADPCM_STATE 16
#define FRAME_SAMPLES 16
#define FRAME_BYTES 9
#define RESAMPLE_TAPS 4
#define RESAMPLE_PHASES 64
#define RAMP_BLOCK 8
#define UNITY_Q16 0x10000u
#define STATE_ENV_WORDS 8

#define OP_ADPCM 1
#define OP_CLEARBUFF 2
#define OP_ENVMIXER 3
#define OP_LOADBUFF 4
#define OP_RESAMPLE 5
#define OP_SAVEBUFF 6
#define OP_SEGMENT 7
#define OP_SETBUFF 8
#define OP_SETVOL 9
#define OP_DMEMMOVE 10
#define OP_LOADADPCM 11
#define OP_MIXER 12
#define OP_INTERLEAVE 13
#define OP_SETLOOP 15

#define FLAG_INIT 0x01
#define FLAG_LOOP 0x02
#define FLAG_LEFT 0x02
#define FLAG_VOL 0x04
#define FLAG_AUX 0x08

typedef struct {
	u32 vol[2];
	u32 target[2];
	u32 rate[2];
	s16 dry;
	s16 wet;
} EnvState;

static u8 sDmem[DMEM_SIZE + 64] __attribute__((aligned(16)));
static s16 sBook[BOOK_ENTRIES];
static s16 sResampleLut[RESAMPLE_PHASES][RESAMPLE_TAPS];
static u32 sLoopAddr;

static struct {
	u16 in, out, count;
	u16 dryRight, wetLeft, wetRight;
	u32 vol[2];
	u32 target[2];
	u32 rate[2];
	s16 dry, wet;
} sAu;

static u8 *dram(u32 addr)
{
	return (u8 *) (DRAM_BASE | (addr & DRAM_MASK));
}

static s16 *dm16(u32 addr)
{
	return (s16 *) (sDmem + (addr & DMEM_MASK & ~1u));
}

static u32 clamp_count(u32 addr, u32 count)
{
	addr &= DMEM_MASK;
	return addr + count > DMEM_SIZE ? DMEM_SIZE - addr : count;
}

static s32 sat16(s32 v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

void vf_audio_hle_init(void)
{
	u32 i;

	for (i = 0; i < RESAMPLE_PHASES; i++) {
		s32 t = (s32) i;
		s32 t2 = t * t;
		s32 t3 = t2 * t;
		s32 c0 = -t3 + 2 * 64 * t2 - 64 * 64 * t;
		s32 c1 = 3 * t3 - 5 * 64 * t2 + 2 * 64 * 64 * 64;
		s32 c2 = -3 * t3 + 4 * 64 * t2 + 64 * 64 * t;
		s32 c3 = t3 - 64 * t2;

		sResampleLut[i][0] = (s16) sat16(c0 / 16);
		sResampleLut[i][1] = (s16) sat16(c1 / 16);
		sResampleLut[i][2] = (s16) sat16(c2 / 16);
		sResampleLut[i][3] = (s16) sat16(c3 / 16);
	}
}

static void op_loadbuff(u32 addr)
{
	u32 n = clamp_count(sAu.in, sAu.count);
	const u8 *src = dram(addr);
	u8 *dst = sDmem + (sAu.in & DMEM_MASK);
	u32 i;

	for (i = 0; i < n; i++)
		dst[i] = src[i];
}

static void op_savebuff(u32 addr)
{
	u32 n = clamp_count(sAu.out, sAu.count);
	u8 *dst = dram(addr);
	const u8 *src = sDmem + (sAu.out & DMEM_MASK);
	u32 i;

	for (i = 0; i < n; i++)
		dst[i] = src[i];
}

static void op_clearbuff(u32 addr, u32 count)
{
	u32 n = clamp_count(addr, (count + 15) & ~15u);
	u8 *p = sDmem + (addr & DMEM_MASK);
	u32 i;

	for (i = 0; i < n; i++)
		p[i] = 0;
}

static void op_dmemmove(u32 in, u32 out, u32 count)
{
	u32 n = (count + 15) & ~15u;
	u8 *src = sDmem + (in & DMEM_MASK);
	u8 *dst = sDmem + (out & DMEM_MASK);
	u32 i;

	if ((in & DMEM_MASK) + n > DMEM_SIZE)
		n = DMEM_SIZE - (in & DMEM_MASK);
	if ((out & DMEM_MASK) + n > DMEM_SIZE)
		n = DMEM_SIZE - (out & DMEM_MASK);
	for (i = 0; i < n; i++)
		dst[i] = src[i];
}

static void op_loadadpcm(u32 count, u32 addr)
{
	const s16 *src = (const s16 *) dram(addr);
	u32 n = count / 2;
	u32 i;

	if (n > BOOK_ENTRIES)
		n = BOOK_ENTRIES;
	for (i = 0; i < n; i++)
		sBook[i] = src[i];
}

static void decode_frame(const u8 *frame, s16 *out, s32 prev2, s32 prev1)
{
	s32 ix[FRAME_SAMPLES];
	const s16 *book = sBook + (frame[0] & 0xF) * 16;
	s32 shift = frame[0] >> 4;
	u32 i, g;

	for (i = 0; i < FRAME_SAMPLES; i += 2) {
		u32 c = frame[1 + i / 2];
		s32 hi = (s32) (c >> 4);
		s32 lo = (s32) (c & 15);

		ix[i] = (hi >= 8 ? hi - 16 : hi) << shift;
		ix[i + 1] = (lo >= 8 ? lo - 16 : lo) << shift;
	}
	for (g = 0; g < 2; g++) {
		const s32 *x = ix + g * 8;

		for (i = 0; i < 8; i++) {
			s32 acc = book[i] * prev2 + book[8 + i] * prev1;
			u32 m;
			s32 v;

			for (m = 0; m < i; m++)
				acc += x[m] * book[8 + i - m - 1];
			v = (acc >> 11) + x[i];
			out[g * 8 + i] = (s16) sat16(v);
		}
		prev2 = out[g * 8 + 6];
		prev1 = out[g * 8 + 7];
	}
}

static void op_adpcm(u32 flags, u32 addr)
{
	s16 *out = dm16(sAu.out);
	const u8 *in = sDmem + (sAu.in & DMEM_MASK);
	s16 last[ADPCM_STATE];
	u32 frames = sAu.count / (FRAME_SAMPLES * 2);
	u32 i;

	if (flags & FLAG_INIT) {
		for (i = 0; i < ADPCM_STATE; i++)
			last[i] = 0;
	} else {
		const s16 *src = (const s16 *) dram(flags & FLAG_LOOP ? sLoopAddr : addr);

		for (i = 0; i < ADPCM_STATE; i++)
			last[i] = src[i];
	}
	if (((sAu.out & DMEM_MASK) + (frames + 1) * 32 > DMEM_SIZE) || ((sAu.in & DMEM_MASK) + frames * FRAME_BYTES > DMEM_SIZE))
		return;
	for (i = 0; i < ADPCM_STATE; i++)
		out[i] = last[i];
	out += ADPCM_STATE;
	{
		s32 prev2 = last[14];
		s32 prev1 = last[15];

		for (i = 0; i < frames; i++) {
			decode_frame(in + i * FRAME_BYTES, out + i * FRAME_SAMPLES, prev2, prev1);
			prev2 = out[i * FRAME_SAMPLES + 14];
			prev1 = out[i * FRAME_SAMPLES + 15];
		}
	}
	{
		s16 *dst = (s16 *) dram(addr);

		for (i = 0; i < ADPCM_STATE; i++)
			dst[i] = out[frames * FRAME_SAMPLES - ADPCM_STATE + i];
	}
}

static void op_resample(u32 flags, u32 pitch, u32 addr)
{
	s16 *out = dm16(sAu.out);
	s16 *src = dm16(sAu.in) - RESAMPLE_TAPS;
	s16 *state = (s16 *) dram(addr);
	u32 samples = sAu.count / 2;
	u32 accu = 0;
	u32 i;

	if (((sAu.in & DMEM_MASK) < RESAMPLE_TAPS * 2) || ((sAu.out & DMEM_MASK) + samples * 2 > DMEM_SIZE))
		return;
	if (flags & FLAG_INIT) {
		for (i = 0; i < RESAMPLE_TAPS; i++)
			src[i] = 0;
	} else {
		for (i = 0; i < RESAMPLE_TAPS; i++)
			src[i] = state[i];
		accu = (u16) state[RESAMPLE_TAPS];
	}
	for (i = 0; i < samples; i++) {
		const s16 *lut = sResampleLut[accu >> 10];
		s32 v = (src[0] * lut[0] + src[1] * lut[1] + src[2] * lut[2] + src[3] * lut[3]) >> 15;

		out[i] = (s16) sat16(v);
		accu += pitch << 1;
		src += accu >> 16;
		accu &= 0xFFFF;
		if ((u8 *) src + RESAMPLE_TAPS * 2 > sDmem + DMEM_SIZE + 64)
			break;
	}
	for (i = 0; i < RESAMPLE_TAPS; i++)
		state[i] = src[i];
	state[RESAMPLE_TAPS] = (s16) accu;
}

static void op_mixer(u32 gain, u32 in, u32 out)
{
	u32 n = clamp_count(in, (sAu.count + 31) & ~31u);
	const s16 *src = dm16(in);
	s16 *dst = dm16(out);
	u32 i;

	n = clamp_count(out, n) / 2;
	for (i = 0; i < n; i++)
		dst[i] = (s16) sat16(dst[i] + (((s32) src[i] * (s32) (s16) gain) >> 15));
}

static void op_interleave(u32 left, u32 right)
{
	const s16 *l = dm16(left);
	const s16 *r = dm16(right);
	s16 *out = dm16(sAu.out);
	u32 n = sAu.count / 2;
	u32 i;

	if (((sAu.out & DMEM_MASK) + n * 4 > DMEM_SIZE) || ((left & DMEM_MASK) + n * 2 > DMEM_SIZE) ||
	    ((right & DMEM_MASK) + n * 2 > DMEM_SIZE))
		return;
	for (i = 0; i < n; i++) {
		out[i * 2] = l[i];
		out[i * 2 + 1] = r[i];
	}
}

static void op_setvol(u32 w0, u32 w1)
{
	u32 flags = (w0 >> 16) & 0xFFFF;
	u32 side = flags & FLAG_LEFT ? 0 : 1;

	if (flags & FLAG_AUX) {
		sAu.dry = (s16) (w0 & 0xFFFF);
		sAu.wet = (s16) (w1 & 0xFFFF);
	} else if (flags & FLAG_VOL) {
		sAu.vol[side] = (w0 & 0xFFFF) << 16;
	} else {
		sAu.target[side] = (w0 & 0xFFFF) << 16;
		sAu.rate[side] = w1;
	}
}

static u32 ramp(u32 v, u32 target, u32 *rate)
{
	u32 r = *rate;

	if (r == UNITY_Q16)
		return v;
	v = (u32) (((unsigned long long) v * r) >> 16);
	if ((r > UNITY_Q16 && v >= target) || (r < UNITY_Q16 && v <= target)) {
		v = target;
		*rate = UNITY_Q16;
	}
	return v;
}

static void op_envmixer(u32 flags, u32 addr)
{
	EnvState *st = (EnvState *) dram(addr);
	u32 samples = ((sAu.count + 15) & ~15u) / 2;
	const s16 *in = dm16(sAu.in);
	s16 *dl = dm16(sAu.out);
	s16 *dr = dm16(sAu.dryRight);
	s16 *wl = dm16(sAu.wetLeft);
	s16 *wr = dm16(sAu.wetRight);
	u32 i;
	u32 gl = 0, gr = 0;
	int aux = flags & FLAG_AUX;

	if (flags & FLAG_INIT) {
		st->vol[0] = sAu.vol[0];
		st->vol[1] = sAu.vol[1];
		st->target[0] = sAu.target[0];
		st->target[1] = sAu.target[1];
		st->rate[0] = sAu.rate[0];
		st->rate[1] = sAu.rate[1];
		st->dry = sAu.dry;
		st->wet = sAu.wet;
	}
	if (((sAu.in & DMEM_MASK) + samples * 2 > DMEM_SIZE) || ((sAu.out & DMEM_MASK) + samples * 2 > DMEM_SIZE) ||
	    ((sAu.dryRight & DMEM_MASK) + samples * 2 > DMEM_SIZE) ||
	    (aux && (((sAu.wetLeft & DMEM_MASK) + samples * 2 > DMEM_SIZE) ||
		     ((sAu.wetRight & DMEM_MASK) + samples * 2 > DMEM_SIZE))))
		return;
	for (i = 0; i < samples; i++) {
		s32 s = in[i];
		s32 l, r;

		if ((i % RAMP_BLOCK) == 0) {
			st->vol[0] = ramp(st->vol[0], st->target[0], &st->rate[0]);
			st->vol[1] = ramp(st->vol[1], st->target[1], &st->rate[1]);
			gl = st->vol[0] >> 16;
			gr = st->vol[1] >> 16;
		}
		l = (s * (s32) gl) >> 15;
		r = (s * (s32) gr) >> 15;
		dl[i] = (s16) sat16(dl[i] + ((l * st->dry) >> 15));
		dr[i] = (s16) sat16(dr[i] + ((r * st->dry) >> 15));
		if (aux) {
			wl[i] = (s16) sat16(wl[i] + ((l * st->wet) >> 15));
			wr[i] = (s16) sat16(wr[i] + ((r * st->wet) >> 15));
		}
	}
}

void vf_run_audio_task(OSTask *task)
{
	const u32 *cmd = (const u32 *) task->t.data_ptr;
	u32 count = task->t.data_size / 8;
	u32 i;

	for (i = 0; i < count; i++, cmd += 2) {
		u32 w0 = cmd[0];
		u32 w1 = cmd[1];

		switch (w0 >> 24) {
		case OP_ADPCM:
			op_adpcm((w0 >> 16) & 0xFF, w1);
			break;
		case OP_CLEARBUFF:
			op_clearbuff(w0 & 0xFFFFFF, w1 & 0xFFFF);
			break;
		case OP_ENVMIXER:
			op_envmixer((w0 >> 16) & 0xFF, w1);
			break;
		case OP_LOADBUFF:
			op_loadbuff(w1);
			break;
		case OP_RESAMPLE:
			op_resample((w0 >> 16) & 0xFF, w0 & 0xFFFF, w1);
			break;
		case OP_SAVEBUFF:
			op_savebuff(w1);
			break;
		case OP_SEGMENT:
			break;
		case OP_SETBUFF:
			if ((w0 >> 16) & FLAG_AUX) {
				sAu.dryRight = (u16) (w0 & 0xFFFF);
				sAu.wetLeft = (u16) (w1 >> 16);
				sAu.wetRight = (u16) (w1 & 0xFFFF);
			} else {
				sAu.in = (u16) (w0 & 0xFFFF);
				sAu.out = (u16) (w1 >> 16);
				sAu.count = (u16) (w1 & 0xFFFF);
			}
			break;
		case OP_SETVOL:
			op_setvol(w0, w1);
			break;
		case OP_DMEMMOVE:
			op_dmemmove(w0 & 0xFFFFFF, w1 >> 16, w1 & 0xFFFF);
			break;
		case OP_LOADADPCM:
			op_loadadpcm(w0 & 0xFFFFFF, w1);
			break;
		case OP_MIXER:
			op_mixer(w0 & 0xFFFF, w1 >> 16, w1 & 0xFFFF);
			break;
		case OP_INTERLEAVE:
			op_interleave(w1 >> 16, w1 & 0xFFFF);
			break;
		case OP_SETLOOP:
			sLoopAddr = w1;
			break;
		default:
			break;
		}
	}
}
