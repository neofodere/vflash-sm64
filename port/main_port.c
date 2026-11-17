#include <ultra64.h>

#include "sm64.h"
#include "audio/external.h"
#include "game_init.h"
#include "memory.h"
#include "sound_init.h"
#include "buffers/buffers.h"
#include "segments.h"
#include "segment_symbols.h"
#include "main.h"
#include "vf_port.h"

#define MESG_VI_VBLANK 102

#define IDLE_STACK_SIZE 0x4000

OSThread D_80339210;
OSThread gIdleThread;
OSThread gMainThread;
OSThread gGameLoopThread;
OSThread gSoundThread;

OSIoMesg gDmaIoMesg;
OSMesg gMainReceivedMesg;

OSMesgQueue gDmaMesgQueue;
OSMesgQueue gSIEventMesgQueue;
OSMesgQueue gPIMesgQueue;
OSMesgQueue gIntrMesgQueue;
OSMesgQueue gSPTaskMesgQueue;

OSMesg gDmaMesgBuf[1];
OSMesg gPIMesgBuf[32];
OSMesg gSIEventMesgBuf[1];
OSMesg gIntrMesgBuf[16];
OSMesg gUnknownMesgBuf[16];

struct VblankHandler *gVblankHandler1 = NULL;
struct VblankHandler *gVblankHandler2 = NULL;
struct SPTask *gActiveSPTask = NULL;
u32 gNumVblanks = 0;
s8 gResetTimer = 0;
s8 gNmiResetBarsTimer = 0;
s8 gDebugLevelSelect = FALSE;
s8 D_8032C650 = 0;
s8 gShowProfiler = FALSE;
s8 gShowDebugText = FALSE;

static struct SPTask *sDisplayTask;

static u8 sIdleStack[IDLE_STACK_SIZE];

static void setup_mesg_queues(void)
{
	osCreateMesgQueue(&gDmaMesgQueue, gDmaMesgBuf, ARRAY_COUNT(gDmaMesgBuf));
	osCreateMesgQueue(&gSIEventMesgQueue, gSIEventMesgBuf, ARRAY_COUNT(gSIEventMesgBuf));
	osCreateMesgQueue(&gSPTaskMesgQueue, gUnknownMesgBuf, ARRAY_COUNT(gUnknownMesgBuf));
	osCreateMesgQueue(&gIntrMesgQueue, gIntrMesgBuf, ARRAY_COUNT(gIntrMesgBuf));
	osViSetEvent(&gIntrMesgQueue, (OSMesg) MESG_VI_VBLANK, 1);
}

#define VF_IMAGE_BASE 0x80500000u
#define VF_POOL_END 0x81000000u

static void alloc_pool(void)
{
	u32 image_end = VF_IMAGE_BASE + *(volatile u32 *) (VF_IMAGE_BASE + 4);
	u32 pool_start = (image_end + 0xFFF) & ~0xFFFu;

	main_pool_init((void *) pool_start, (void *) VF_POOL_END);
	gEffectsMemoryPool = mem_pool_init(0x4000, MEMORY_POOL_LEFT);
}

static void finish_task(struct SPTask *task)
{
	task->state = SPTASK_STATE_FINISHED_DP;
	if (task->msgqueue != NULL)
		osSendMesg(task->msgqueue, task->msg, OS_MESG_NOBLOCK);
}

static void handle_vblank(void)
{
	struct SPTask *task;

	gNumVblanks++;
	if (gNumVblanks % 30 == 0)
		VF_LOGX("vblank", gNumVblanks);
	if (gResetTimer > 0)
		gResetTimer++;

	if (sDisplayTask != NULL && sDisplayTask->state == SPTASK_STATE_NOT_STARTED) {
		task = sDisplayTask;
		gActiveSPTask = task;
		vf_run_gfx_task(&task->task);
		gActiveSPTask = NULL;
		sDisplayTask = NULL;
		finish_task(task);
	}
	vf_gfx_vblank();

	if (gVblankHandler2 != NULL)
		osSendMesg(gVblankHandler2->queue, gVblankHandler2->msg, OS_MESG_NOBLOCK);
}

static void thread3_main(UNUSED void *arg)
{
	VF_LOG("thread3");
	setup_mesg_queues();
	alloc_pool();
	vf_gfx_init();
	vf_audio_init();

	VF_LOG("pool ready");
	osCreateThread(&gSoundThread, 4, thread4_sound, NULL, gThread4Stack + VF_THREAD_STACK, 20);
	osStartThread(&gSoundThread);

	osCreateThread(&gGameLoopThread, 5, thread5_game_loop, NULL, gThread5Stack + VF_THREAD_STACK, 10);
	osStartThread(&gGameLoopThread);

	while (TRUE) {
		OSMesg msg;

		osRecvMesg(&gIntrMesgQueue, &msg, OS_MESG_BLOCK);
		if ((uintptr_t) msg == MESG_VI_VBLANK)
			handle_vblank();
	}
}

void set_vblank_handler(s32 index, struct VblankHandler *handler, OSMesgQueue *queue, OSMesg *msg)
{
	handler->queue = queue;
	handler->msg = msg;
	if (index == 1)
		gVblankHandler1 = handler;
	else
		gVblankHandler2 = handler;
}

void send_sp_task_message(OSMesg *msg)
{
	osSendMesg(&gSPTaskMesgQueue, msg, OS_MESG_NOBLOCK);
}

void dispatch_audio_sptask(struct SPTask *spTask)
{
	(void) spTask;
}

void exec_display_list(struct SPTask *spTask)
{
	if (spTask != NULL) {
		spTask->state = SPTASK_STATE_NOT_STARTED;
		sDisplayTask = spTask;
	}
}

void turn_on_audio(void)
{
	vf_audio_enable(TRUE);
}

void turn_off_audio(void)
{
	vf_audio_enable(FALSE);
}

static void thread1_idle(UNUSED void *arg)
{
	osCreateThread(&gMainThread, 3, thread3_main, NULL, gThread3Stack + VF_THREAD_STACK, 100);
	osStartThread(&gMainThread);
	osSetThreadPri(NULL, 0);
	for (;;)
		vf_idle();
}

void main_func(void)
{
	osInitialize();
	osCreateThread(&gIdleThread, 1, thread1_idle, NULL, sIdleStack + IDLE_STACK_SIZE, 100);
	osStartThread(&gIdleThread);
}
