#include <ultra64.h>
#include "vf_hw.h"
#include "vf_port.h"

#define ROM_ENTRIES 240
#define ROM_MAGIC 0x564F5254u
#define IMAGE_BASE 0x80500000u
#define WINDOW_SHIFT 22
#define WINDOW_MASK 0x3FFFFFu

typedef struct {
	u32 offset;
	u32 stored;
	u32 raw;
	u32 kind;
} RomEntry;

__attribute__((section(".romtab"), used)) RomEntry vf_rom_table[ROM_ENTRIES] = { { ROM_MAGIC, ROM_ENTRIES, 0, 0 } };

void vf_rom_read(u32 devAddr, void *dest, u32 size)
{
	u32 id = devAddr >> WINDOW_SHIFT;
	u32 off = devAddr & WINDOW_MASK;
	const RomEntry *e;
	const u8 *src;
	u8 *dst = dest;
	u32 n;

	VF_LOGX("dma rom", devAddr);
	VF_LOGX("  dest", (u32) dest);
	VF_LOGX("  size", size);
	if (id == 0 || id >= ROM_ENTRIES || !vf_rom_table[id].offset) {
		while (size--)
			*dst++ = 0;
		return;
	}
	e = &vf_rom_table[id];
	src = (const u8 *) (IMAGE_BASE + e->offset);
	n = off < e->stored ? e->stored - off : 0;
	if (n > size)
		n = size;
	for (u32 i = 0; i < n; i++)
		dst[i] = src[off + i];
	for (u32 i = n; i < size; i++)
		dst[i] = 0;
}

s32 osPiStartDma(OSIoMesg *mb, s32 priority, s32 direction, uintptr_t devAddr, void *vAddr, u32 nbytes, OSMesgQueue *mq)
{
	vf_rom_read(devAddr, vAddr, nbytes);
	mb->hdr.retQueue = mq;
	if (mq)
		osSendMesg(mq, (OSMesg) mb, OS_MESG_NOBLOCK);
	return 0;
}

void decompress(void *src, void *dest)
{
	const u8 *in = (const u8 *) src + 8;
	u8 *out = (u8 *) dest;
	u8 *end = out + ((const u32 *) src)[1];

	while (out < end) {
		u32 token = *in++;
		u32 lit = token >> 4;
		u32 len = token & 15;
		u32 off;

		if (lit == 15) {
			u32 b;

			do {
				b = *in++;
				lit += b;
			} while (b == 255);
		}
		while (lit--)
			*out++ = *in++;
		if (out >= end)
			break;
		off = in[0] | in[1] << 8;
		in += 2;
		if (len == 15) {
			u32 b;

			do {
				b = *in++;
				len += b;
			} while (b == 255);
		}
		len += 4;
		{
			const u8 *from = out - off;

			while (len--)
				*out++ = *from++;
		}
	}
}
