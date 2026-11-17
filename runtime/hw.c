#include "vf_hw.h"

void vf_putc(char c)
{
	VF_REG(VF_UART0) = (unsigned int) c;
}

void vf_puts(const char *s)
{
	while (*s)
		vf_putc(*s++);
}

void vf_puthex(unsigned int v)
{
	int i;

	for (i = 28; i >= 0; i -= 4)
		vf_putc("0123456789ABCDEF"[v >> i & 15]);
	vf_putc('\n');
}

void vf_drain_writes(void)
{
	__asm__ volatile("mcr p15, 0, %0, c7, c10, 4" : : "r"(0) : "memory");
}

void vf_fault(unsigned int kind, unsigned int pc, const unsigned int *regs)
{
	unsigned int fsr, far, i;

	__asm__ volatile("mrc p15, 0, %0, c5, c0, 0" : "=r"(fsr));
	__asm__ volatile("mrc p15, 0, %0, c6, c0, 0" : "=r"(far));
	vf_puts("FAULT kind=");
	vf_puthex(kind);
	vf_puts("pc=");
	vf_puthex(pc);
	vf_puts("fsr=");
	vf_puthex(fsr);
	vf_puts("far=");
	vf_puthex(far);
	for (i = 0; i < 14; i++) {
		vf_puts("r");
		vf_puthex(i);
		vf_puthex(regs[i]);
	}
	for (i = 0; i < 14; i++) {
		unsigned int a = regs[i];

		if (a >= 0x80000000u && a < 0x81000000u) {
			vf_puts("*r");
			vf_puthex(i);
			vf_puthex(*(const unsigned int *) a);
			a = *(const unsigned int *) a;
			if (a >= 0x80000000u && a < 0x81000000u) {
				unsigned int j;

				vf_puts("**");
				for (j = 0; j < 4; j++)
					vf_puthex(((const unsigned int *) a)[j]);
			}
		}
	}
	for (;;)
		;
}
