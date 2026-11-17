#include <ultra64.h>
#include <stdarg.h>
#include <PR/libaudio.h>
#include "vf_port.h"

void osCreateViManager(OSPri pri) {}
void osViSetMode(OSViMode *mode) {}
void osViBlack(u8 active) {}
void osViSetSpecialFeatures(u32 func) {}
void osViSwapBuffer(void *frameBuf) {}
void osCreatePiManager(OSPri pri, OSMesgQueue *cmdQ, OSMesg *cmdBuf, s32 cmdMsgCnt) {}

void alSeqFileNew(ALSeqFile *f, u8 *base)
{
	s32 i;

	for (i = 0; i < f->seqCount; i++)
		f->seqArray[i].offset += (uintptr_t) base;
}

static char *put_uint(char *p, u32 v, u32 base, int width, char pad)
{
	char tmp[12];
	int n = 0;

	do {
		tmp[n++] = "0123456789abcdef"[v % base];
		v /= base;
	} while (v);
	while (width > n) {
		*p++ = pad;
		width--;
	}
	while (n)
		*p++ = tmp[--n];
	return p;
}

int sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	char *p = buf;

	va_start(ap, fmt);
	while (*fmt) {
		int width = 0;
		char pad = ' ';

		if (*fmt != '%') {
			*p++ = *fmt++;
			continue;
		}
		fmt++;
		if (*fmt == '0') {
			pad = '0';
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		switch (*fmt) {
		case 'd': {
			s32 v = va_arg(ap, s32);

			if (v < 0) {
				*p++ = '-';
				v = -v;
			}
			p = put_uint(p, (u32) v, 10, width, pad);
			break;
		}
		case 'u':
			p = put_uint(p, va_arg(ap, u32), 10, width, pad);
			break;
		case 'x':
			p = put_uint(p, va_arg(ap, u32), 16, width, pad);
			break;
		case 'c':
			*p++ = (char) va_arg(ap, int);
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);

			while (*s)
				*p++ = *s++;
			break;
		}
		case '%':
			*p++ = '%';
			break;
		}
		if (*fmt)
			fmt++;
	}
	va_end(ap);
	*p = 0;
	return (int) (p - buf);
}
