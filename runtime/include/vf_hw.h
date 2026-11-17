#ifndef VF_HW_H
#define VF_HW_H

#define VF_REG(a) (*(volatile unsigned int *)(a))

#define VF_UART0 0x90020000u
#define VF_VE 0xB8000000u
#define VF_GE 0xA8000000u

#define VF_VE_STATUS (VF_VE + 0x00)
#define VF_VE_SIZE (VF_VE + 0x0C)
#define VF_VE_LAYER_ENABLE (VF_VE + 0x100)
#define VF_VE_LAYER_MODE (VF_VE + 0x108)
#define VF_VE_WINDOW_START (VF_VE + 0x130)
#define VF_VE_WINDOW_END (VF_VE + 0x134)
#define VF_VE_LAYER0_ADDR (VF_VE + 0x160)

#define VF_GE_KICK (VF_GE + 0x00)
#define VF_GE_STATUS (VF_GE + 0x14)
#define VF_GE_LIST (VF_GE + 0x28)
#define VF_GE_SURF_CLIP (VF_GE + 0x90)
#define VF_GE_SURF_DRAW (VF_GE + 0x94)
#define VF_GE_SURF_BASE (VF_GE + 0x98)

#define VF_RAM_PHYS 0x10000000u
#define VF_RAM_VIRT 0x80000000u
#define VF_PHYS(v) (((unsigned int) (v) & 0x00FFFFFFu) | VF_RAM_PHYS)
#define VF_VIRT(p) (((unsigned int) (p) & 0x00FFFFFFu) | VF_RAM_VIRT)

#define VF_SURF_TOP 128
#define VF_SURF_BASE 0x10010000u
#define VF_SURF_PITCH 1024

void vf_putc(char c);
void vf_puts(const char *s);
void vf_puthex(unsigned int v);
void vf_drain_writes(void);

#endif
