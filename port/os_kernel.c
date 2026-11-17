#include <ultra64.h>
#include "vf_hw.h"
#include "vf_port.h"

typedef struct {
	u32 r[10];
} Ctx;

extern void vf_ctx_switch(Ctx *from, Ctx *to);
extern void vf_thread_entry(void);

#define CTX(t) ((Ctx *) &(t)->context)
#define NUM_EVENTS 32

static OSThread *sThreads;
static OSThread *sCurrent;
static OSThread sBoot;
static u32 sStamp;
static u32 sVblankCount;
static u32 sViRetraceCount;
static u32 sViCounter;
static OSMesgQueue *sViQueue;
static OSMesg sViMsg;

static struct {
	OSMesgQueue *queue;
	OSMesg msg;
} sEvents[NUM_EVENTS];

u32 osTvType = 0;
u32 osMemSize = 0x1000000;
s32 osAppNMIBuffer[16];
u64 osClockRate = 62500000;

u32 vf_vblank_count(void)
{
	return sVblankCount;
}

static void queue_insert(OSThread **q, OSThread *t)
{
	OSThread **p = q;

	while (*p && (*p)->priority >= t->priority)
		p = &(*p)->next;
	t->next = *p;
	*p = t;
	t->queue = q;
}

static OSThread *queue_pop(OSThread **q)
{
	OSThread *t = *q;

	if (t) {
		*q = t->next;
		t->next = NULL;
		t->queue = NULL;
	}
	return t;
}

static void queue_remove(OSThread *t)
{
	OSThread **p;

	if (!t->queue)
		return;
	for (p = t->queue; *p; p = &(*p)->next) {
		if (*p == t) {
			*p = t->next;
			break;
		}
	}
	t->next = NULL;
	t->queue = NULL;
}

void osPostEvent(s32 event)
{
	if (sEvents[event].queue)
		osSendMesg(sEvents[event].queue, sEvents[event].msg, OS_MESG_NOBLOCK);
}

static void poll_hardware(void)
{
	vf_audio_poll();
	if (VF_REG(VF_VE_STATUS) & 2) {
		VF_REG(VF_VE_STATUS) = 2;
		sVblankCount++;
		if (sViQueue && ++sViCounter >= sViRetraceCount) {
			sViCounter = 0;
			osSendMesg(sViQueue, sViMsg, OS_MESG_NOBLOCK);
		}
	}
}

static void reschedule(void)
{
	OSThread *t, *best;

	for (;;) {
		best = NULL;
		for (t = sThreads; t; t = t->tlnext) {
			if (t->state != OS_STATE_RUNNABLE && t->state != OS_STATE_RUNNING)
				continue;
			if (!best || t->priority > best->priority
			    || (t->priority == best->priority && (u32) t->fp < (u32) best->fp))
				best = t;
		}
		if (best)
			break;
		poll_hardware();
	}
	if (best == sCurrent) {
		best->state = OS_STATE_RUNNING;
		return;
	}
	t = sCurrent;
	if (t->state == OS_STATE_RUNNING)
		t->state = OS_STATE_RUNNABLE;
	t->fp = (int) ++sStamp;
	best->state = OS_STATE_RUNNING;
	sCurrent = best;
	vf_ctx_switch(CTX(t), CTX(best));
}

static void preempt_check(void)
{
	OSThread *t;

	for (t = sThreads; t; t = t->tlnext) {
		if (t->state == OS_STATE_RUNNABLE && t->priority > sCurrent->priority) {
			reschedule();
			return;
		}
	}
}

void osInitialize(void)
{
	sBoot.priority = -1;
	sBoot.state = OS_STATE_RUNNING;
	sBoot.id = 0;
	sCurrent = &sBoot;
}

void osCreateThread(OSThread *thread, OSId id, void (*entry)(void *), void *arg, void *sp, OSPri pri)
{
	Ctx *c = CTX(thread);

	thread->next = NULL;
	thread->queue = NULL;
	thread->tlnext = NULL;
	thread->priority = pri;
	thread->id = id;
	thread->state = OS_STATE_STOPPED;
	thread->flags = 0;
	thread->fp = 0;
	c->r[0] = (u32) entry;
	c->r[1] = (u32) arg;
	c->r[8] = ((u32) sp - 16) & ~7u;
	c->r[9] = (u32) vf_thread_entry;
}

void osStartThread(OSThread *thread)
{
	OSThread *t;

	for (t = sThreads; t; t = t->tlnext)
		if (t == thread)
			break;
	if (!t) {
		thread->tlnext = sThreads;
		sThreads = thread;
	}
	if (thread->state == OS_STATE_STOPPED)
		thread->state = OS_STATE_RUNNABLE;
	preempt_check();
}

void osStopThread(OSThread *thread)
{
	if (!thread)
		thread = sCurrent;
	queue_remove(thread);
	thread->state = OS_STATE_STOPPED;
	if (thread == sCurrent)
		reschedule();
}

void osDestroyThread(OSThread *thread)
{
	OSThread **p;

	if (!thread)
		thread = sCurrent;
	queue_remove(thread);
	for (p = &sThreads; *p; p = &(*p)->tlnext) {
		if (*p == thread) {
			*p = thread->tlnext;
			break;
		}
	}
	thread->state = OS_STATE_STOPPED;
	if (thread == sCurrent)
		reschedule();
}

void osThreadExit(void)
{
	osDestroyThread(NULL);
}

void osSetThreadPri(OSThread *thread, OSPri pri)
{
	if (!thread)
		thread = sCurrent;
	thread->priority = pri;
	if (thread->queue) {
		OSThread **q = thread->queue;

		queue_remove(thread);
		queue_insert(q, thread);
	}
	reschedule();
}

OSPri osGetThreadPri(OSThread *thread)
{
	return (thread ? thread : sCurrent)->priority;
}

OSId osGetThreadId(OSThread *thread)
{
	return (thread ? thread : sCurrent)->id;
}

void osYieldThread(void)
{
	poll_hardware();
	sCurrent->state = OS_STATE_RUNNABLE;
	reschedule();
}

void osCreateMesgQueue(OSMesgQueue *mq, OSMesg *msg, s32 count)
{
	mq->mtqueue = NULL;
	mq->fullqueue = NULL;
	mq->validCount = 0;
	mq->first = 0;
	mq->msgCount = count;
	mq->msg = msg;
}

static void block_on(OSThread **q)
{
	queue_insert(q, sCurrent);
	sCurrent->state = OS_STATE_WAITING;
	reschedule();
}

static void wake_one(OSThread **q)
{
	OSThread *t = queue_pop(q);

	if (t) {
		t->state = OS_STATE_RUNNABLE;
		preempt_check();
	}
}

s32 osSendMesg(OSMesgQueue *mq, OSMesg msg, s32 flags)
{
	while (mq->validCount >= mq->msgCount) {
		if (flags == OS_MESG_NOBLOCK)
			return -1;
		block_on(&mq->fullqueue);
	}
	mq->msg[(mq->first + mq->validCount) % mq->msgCount] = msg;
	mq->validCount++;
	wake_one(&mq->mtqueue);
	return 0;
}

s32 osJamMesg(OSMesgQueue *mq, OSMesg msg, s32 flags)
{
	while (mq->validCount >= mq->msgCount) {
		if (flags == OS_MESG_NOBLOCK)
			return -1;
		block_on(&mq->fullqueue);
	}
	mq->first = (mq->first + mq->msgCount - 1) % mq->msgCount;
	mq->msg[mq->first] = msg;
	mq->validCount++;
	wake_one(&mq->mtqueue);
	return 0;
}

s32 osRecvMesg(OSMesgQueue *mq, OSMesg *msg, s32 flags)
{
	while (mq->validCount == 0) {
		if (flags == OS_MESG_NOBLOCK)
			return -1;
		block_on(&mq->mtqueue);
	}
	if (msg)
		*msg = mq->msg[mq->first];
	mq->first = (mq->first + 1) % mq->msgCount;
	mq->validCount--;
	wake_one(&mq->fullqueue);
	return 0;
}

void osSetEventMesg(OSEvent event, OSMesgQueue *mq, OSMesg msg)
{
	sEvents[event].queue = mq;
	sEvents[event].msg = msg;
}

void osViSetEvent(OSMesgQueue *mq, OSMesg msg, u32 retraceCount)
{
	sViQueue = mq;
	sViMsg = msg;
	sViRetraceCount = retraceCount ? retraceCount : 1;
	sViCounter = 0;
}

void vf_idle(void)
{
	poll_hardware();
	osYieldThread();
}

OSTime osGetTime(void)
{
	return (OSTime) sVblankCount * 1250000u;
}

void osWritebackDCache(void *addr, size_t size) {}
void osWritebackDCacheAll(void) {}
void osInvalDCache(void *addr, size_t size) {}
void osInvalICache(void *addr, size_t size) {}
#define TLB_FIRST_SECTION 0x40
#define TLB_SECTIONS 8
#define PAGE_TABLE ((volatile u32 *) VF_VIRT(0x10004000))
#define SMALL_PAGE_FLAGS 0xFFA
#define COARSE_FLAGS 0x11
#define SECTION_IDENTITY 0xC02

static u32 sTlbTables[TLB_SECTIONS][256] __attribute__((aligned(1024)));

static void mmu_flush(void)
{
	vf_drain_writes();
	__asm__ volatile("mcr p15, 0, %0, c7, c6, 0" : : "r"(0) : "memory");
	__asm__ volatile("mcr p15, 0, %0, c8, c7, 0" : : "r"(0) : "memory");
}

void osMapTLB(s32 index, OSPageMask pm, void *vaddr, u32 evenpaddr, u32 oddpaddr, s32 asid)
{
	u32 half;

	for (half = 0; half < 2; half++) {
		u32 va = (u32) vaddr + half * 0x10000;
		u32 phys = (half ? oddpaddr : evenpaddr) & 0x00FFFFFF;
		u32 section = (va >> 20) - TLB_FIRST_SECTION;
		u32 first = (va >> 12) & 0xFF;
		u32 page;

		if (section >= TLB_SECTIONS)
			continue;
		for (page = 0; page < 16; page++)
			sTlbTables[section][first + page] = (VF_RAM_PHYS | phys) + page * 0x1000 | SMALL_PAGE_FLAGS;
		PAGE_TABLE[va >> 20] = VF_PHYS(sTlbTables[section]) | COARSE_FLAGS;
	}
	mmu_flush();
}

void osUnmapTLBAll(void)
{
	u32 section;
	u32 page;

	for (section = 0; section < TLB_SECTIONS; section++) {
		u32 idx = TLB_FIRST_SECTION + section;

		for (page = 0; page < 256; page++)
			sTlbTables[section][page] = 0;
		PAGE_TABLE[idx] = idx << 20 | SECTION_IDENTITY;
	}
	mmu_flush();
}

uintptr_t osVirtualToPhysical(void *addr)
{
	return (u32) addr;
}
