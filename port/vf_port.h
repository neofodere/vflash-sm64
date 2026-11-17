#ifndef VF_PORT_H
#define VF_PORT_H

#include <ultra64.h>

#define VF_THREAD_STACK 0x10000

void vf_run_gfx_task(OSTask *task);
void vf_run_audio_task(OSTask *task);
void vf_audio_hle_init(void);
void vf_audio_init(void);
void vf_audio_poll(void);
void vf_audio_enable(int on);
void vf_gfx_init(void);
void vf_gfx_vblank(void);
void vf_idle(void);
void vf_input_poll(void);
u32 vf_vblank_count(void);
void osPostEvent(s32 event);

#ifdef VF_TRACE
#include "vf_hw.h"
#define VF_LOG(msg) vf_puts(msg "\n")
#define VF_LOGX(msg, v) (vf_puts(msg " "), vf_puthex(v))
#else
#define VF_LOG(msg) ((void) 0)
#define VF_LOGX(msg, v) ((void) 0)
#endif

#endif
