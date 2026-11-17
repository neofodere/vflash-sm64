#include <ultra64.h>
#include "audio/external.h"
#include "main.h"
#include "vf_hw.h"
#include "vf_port.h"

#define RING_FRAMES 16384
#define VOICE_BASE 0xB0000000u
#define VOICE_STRIDE 0x40
#define VOICE_KEY_ON 0xB0001010u
#define VOICE_CURSOR 0x28
#define VOICE_COUNT 2
#define MODE_LOOP 0x10
#define PITCH_UNITY 0x1000
#define GAIN_LEFT 0xFF
#define GAIN_RIGHT 0xFF00
#define SOURCE_RATE 32000u
#define OUTPUT_RATE 44100u
#define STEP_Q16 ((SOURCE_RATE << 16) / OUTPUT_RATE)
#define LEAD_FRAMES 256
#define TARGET_FILL 11000
#define MAX_TASKS_PER_POLL 16
#define AI_REPORTED_BYTES 0x4000

extern struct VblankHandler *gVblankHandler1;
extern s8 gResetTimer;

static s16 sRing[VOICE_COUNT][RING_FRAMES] __attribute__((aligned(32)));
static u32 sWritePos;
static u32 sPlayPos;
static u32 sLastCursor;
static u32 sPhase;
static s16 sPrev[2];
static s16 sNext[2];
static u8 sStarted;
static u8 sBusy;
static u8 sEnabled = 1;

static u32 ring_start(void)
{
	return VF_PHYS(sRing[0]);
}

static u32 cursor_frame(void)
{
	u32 byte = VF_REG(VOICE_BASE + 0 * VOICE_STRIDE + VOICE_CURSOR) - ring_start();

	return (byte >> 1) % RING_FRAMES;
}

static void start_voice(u32 v)
{
	volatile u32 *r = (volatile u32 *) (VOICE_BASE + v * VOICE_STRIDE);
	u32 start = VF_PHYS(sRing[v]);

	r[0] = start;
	r[1] = start + (RING_FRAMES - 1) * 2;
	r[2] = start;
	r[3] = MODE_LOOP;
	r[4] = PITCH_UNITY;
	r[5] = 0xFFFFFFFFu;
	r[6] = 0xFFFF0000u;
	r[7] = 0xFFFFFFFFu;
	r[8] = v == 0 ? GAIN_LEFT : GAIN_RIGHT;
	r[9] = 0;
}

void vf_audio_init(void)
{
	u32 v;

	vf_audio_hle_init();
	for (v = 0; v < VOICE_COUNT; v++)
		start_voice(v);
	vf_drain_writes();
	VF_REG(VOICE_KEY_ON) = (1u << VOICE_COUNT) - 1;
	sLastCursor = 0;
	sPlayPos = 0;
	sWritePos = LEAD_FRAMES;
	sStarted = 1;
}

static void advance_play_position(void)
{
	u32 cur = cursor_frame();
	u32 played = (cur + RING_FRAMES - sLastCursor) % RING_FRAMES;
	u32 i;

	for (i = 0; i < played; i++) {
		u32 idx = (sLastCursor + i) % RING_FRAMES;

		sRing[0][idx] = 0;
		sRing[1][idx] = 0;
	}
	sPlayPos += played;
	sLastCursor = cur;
	if ((s32) (sWritePos - sPlayPos) < 0) {
		VF_LOGX("audio underrun", sPlayPos - sWritePos);
		sWritePos = sPlayPos + LEAD_FRAMES;
	}
}

static s32 fill_frames(void)
{
	return (s32) (sWritePos - sPlayPos);
}

static void push_frame(s16 l, s16 r)
{
	u32 idx = sWritePos % RING_FRAMES;

	sRing[0][idx] = l;
	sRing[1][idx] = r;
	sWritePos++;
}

s32 osAiSetFrequency(u32 frequency)
{
	return (s32) SOURCE_RATE;
}

s32 osAiSetNextBuffer(void *buff, u32 len)
{
	const s16 *in = (const s16 *) buff;
	u32 frames = len / 4;
	u32 i;

	if (!sStarted)
		return 0;
	for (i = 0; i < frames; i++) {
		sPrev[0] = sNext[0];
		sPrev[1] = sNext[1];
		sNext[0] = in[i * 2];
		sNext[1] = in[i * 2 + 1];
		while (sPhase < 0x10000u) {
			s32 f = (s32) (sPhase >> 1);

			if (fill_frames() >= RING_FRAMES - 64)
				break;
			push_frame((s16) (sPrev[0] + (((sNext[0] - sPrev[0]) * f) >> 15)),
				   (s16) (sPrev[1] + (((sNext[1] - sPrev[1]) * f) >> 15)));
			sPhase += STEP_Q16;
		}
		sPhase -= 0x10000u;
	}
	vf_drain_writes();
	return 0;
}

u32 osAiGetLength(void)
{
	return AI_REPORTED_BYTES;
}

void vf_audio_enable(int on)
{
	sEnabled = on != 0;
}

void vf_audio_poll(void)
{
	u32 n;

	if (!sStarted || sBusy || gVblankHandler1 == NULL)
		return;
	sBusy = 1;
	advance_play_position();
	for (n = 0; sEnabled && gResetTimer < 25 && n < MAX_TASKS_PER_POLL && fill_frames() < TARGET_FILL; n++) {
		struct SPTask *task = create_next_audio_frame_task();

		if (task != NULL)
			vf_run_audio_task(&task->task);
	}
	sBusy = 0;
}
