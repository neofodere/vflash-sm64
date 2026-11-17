#include "sound.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEQ_FILE_HEADER 4
#define SEQ_FILE_ENTRY 8
#define BANK_HEADER 16
#define BANK_SETS_SIZE 0x100
#define BANK_SETS_SEARCH 0x200
#define ALIGN 16
#define SAMPLE_SIZE 20
#define INSTRUMENT_SIZE 32
#define DRUM_SIZE 16
#define LOOP_HEADER 16
#define LOOP_STATE 32

typedef struct
{
	uint8_t *data;
	size_t size;
	uint8_t *done;
	int ok;
} bank_t;

static uint32_t be16(const uint8_t *p)
{
	return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int fits(size_t size, size_t off, size_t len)
{
	return off <= size && size - off >= len;
}

static void swap_bytes(uint8_t *p, unsigned n)
{
	for (unsigned i = 0; i < n / 2; i++)
	{
		uint8_t t = p[i];

		p[i] = p[n - 1 - i];
		p[n - 1 - i] = t;
	}
}

static uint32_t bank_swap(bank_t *b, size_t off, unsigned width)
{
	uint32_t v = 0;

	if (!fits(b->size, off, width))
	{
		b->ok = 0;
		return 0;
	}
	if (!b->done[off])
	{
		swap_bytes(b->data + off, width);
		memset(b->done + off, 1, width);
	}
	for (unsigned i = width; i-- > 0;)
		v = v << 8 | b->data[off + i];
	return v;
}

static void bank_envelope(bank_t *b, size_t off)
{
	for (unsigned i = 0; i < 64 && b->ok; i++, off += 4)
	{
		int16_t delay = (int16_t)bank_swap(b, off, 2);

		bank_swap(b, off + 2, 2);
		if (delay <= 0 && delay >= -3)
			break;
	}
}

static void bank_sample(bank_t *b, size_t off)
{
	if (!fits(b->size, off, SAMPLE_SIZE))
	{
		b->ok = 0;
		return;
	}
	bank_swap(b, off + 4, 4);
	uint32_t loop = bank_swap(b, off + 8, 4);
	uint32_t book = bank_swap(b, off + 12, 4);

	bank_swap(b, off + 16, 4);
	if (loop)
	{
		uint32_t count = 0;

		for (unsigned i = 0; i < LOOP_HEADER; i += 4)
		{
			uint32_t v = bank_swap(b, loop + i, 4);

			if (i == 8)
				count = v;
		}
		if (count)
			for (unsigned i = 0; i < LOOP_STATE; i += 2)
				bank_swap(b, loop + LOOP_HEADER + i, 2);
	}
	if (book)
	{
		uint32_t order = bank_swap(b, book, 4);
		uint32_t predictors = bank_swap(b, book + 4, 4);

		if (order > 8 || predictors > 64)
		{
			b->ok = 0;
			return;
		}
		for (uint32_t i = 0; i < 8 * order * predictors && b->ok; i++)
			bank_swap(b, book + 8 + i * 2, 2);
	}
}

static void bank_sound(bank_t *b, size_t off)
{
	uint32_t sample = bank_swap(b, off, 4);

	bank_swap(b, off + 4, 4);
	if (sample)
		bank_sample(b, sample);
}

static void bank_instrument(bank_t *b, size_t off)
{
	if (!fits(b->size, off, INSTRUMENT_SIZE))
	{
		b->ok = 0;
		return;
	}
	uint32_t env = bank_swap(b, off + 4, 4);

	if (env)
		bank_envelope(b, env);
	for (unsigned i = 0; i < 3; i++)
		bank_sound(b, off + 8 + i * 8);
}

static void bank_drum(bank_t *b, size_t off)
{
	if (!fits(b->size, off, DRUM_SIZE))
	{
		b->ok = 0;
		return;
	}
	bank_sound(b, off + 4);
	uint32_t env = bank_swap(b, off + 12, 4);

	if (env)
		bank_envelope(b, env);
}

static int convert_bank(uint8_t *entry, size_t len)
{
	bank_t b = { entry + BANK_HEADER, len - BANK_HEADER, NULL, 1 };
	uint32_t instruments, drums, drum_table;

	if (len < BANK_HEADER + 4)
		return -1;
	instruments = be32(entry);
	drums = be32(entry + 4);
	swap_bytes(entry, 4);
	swap_bytes(entry + 4, 4);
	if (instruments > 256 || drums > 256)
		return -1;
	b.done = calloc(b.size ? b.size : 1, 1);
	if (!b.done)
		return -1;
	drum_table = bank_swap(&b, 0, 4);
	for (uint32_t i = 0; i < instruments && b.ok; i++)
	{
		uint32_t inst = bank_swap(&b, 4 + i * 4, 4);

		if (inst)
			bank_instrument(&b, inst);
	}
	if (drum_table && drums)
	{
		for (uint32_t i = 0; i < drums && b.ok; i++)
		{
			uint32_t drum = bank_swap(&b, drum_table + i * 4, 4);

			if (drum)
				bank_drum(&b, drum);
		}
	}
	free(b.done);
	return b.ok ? 0 : -1;
}

static int convert_seq_file(uint8_t *file, size_t size, unsigned *count_out)
{
	unsigned count;

	if (size < SEQ_FILE_HEADER)
		return -1;
	count = be16(file + 2);
	if (!fits(size, SEQ_FILE_HEADER, (size_t)count * SEQ_FILE_ENTRY))
		return -1;
	swap_bytes(file, 2);
	swap_bytes(file + 2, 2);
	for (unsigned i = 0; i < count; i++)
	{
		swap_bytes(file + SEQ_FILE_HEADER + i * SEQ_FILE_ENTRY, 4);
		swap_bytes(file + SEQ_FILE_HEADER + i * SEQ_FILE_ENTRY + 4, 4);
	}
	*count_out = count;
	return 0;
}

static int file_extent(const uint8_t *file, size_t size, size_t *extent)
{
	unsigned count = be16(file + 2);
	size_t end = SEQ_FILE_HEADER + (size_t)count * SEQ_FILE_ENTRY;

	if (size < SEQ_FILE_HEADER || !fits(size, SEQ_FILE_HEADER, (size_t)count * SEQ_FILE_ENTRY))
		return -1;
	for (unsigned i = 0; i < count; i++)
	{
		size_t off = be32(file + SEQ_FILE_HEADER + i * SEQ_FILE_ENTRY);
		size_t len = be32(file + SEQ_FILE_HEADER + i * SEQ_FILE_ENTRY + 4);

		if (!fits(size, off, len))
			return -1;
		if (off + len > end)
			end = off + len;
	}
	*extent = end;
	return 0;
}

static int build_ctl(uint8_t *ctl, size_t size)
{
	size_t extent;
	unsigned count;
	size_t *offsets;
	size_t *lengths;
	int rc = -1;

	if (file_extent(ctl, size, &extent))
		return -1;
	count = be16(ctl + 2);
	offsets = malloc(count * sizeof *offsets + 1);
	lengths = malloc(count * sizeof *lengths + 1);
	if (!offsets || !lengths)
		goto done;
	for (unsigned i = 0; i < count; i++)
	{
		offsets[i] = be32(ctl + SEQ_FILE_HEADER + i * SEQ_FILE_ENTRY);
		lengths[i] = be32(ctl + SEQ_FILE_HEADER + i * SEQ_FILE_ENTRY + 4);
	}
	if (convert_seq_file(ctl, size, &count))
		goto done;
	for (unsigned i = 0; i < count; i++)
		if (convert_bank(ctl + offsets[i], lengths[i]))
			goto done;
	rc = 0;
done:
	free(offsets);
	free(lengths);
	return rc;
}

static int find_bank_sets(const rom_t *rom, size_t start, unsigned seq_count, size_t *found)
{
	size_t first = (size_t)seq_count * 2;

	for (size_t pos = (start + ALIGN - 1) & ~(size_t)(ALIGN - 1); pos < start + BANK_SETS_SEARCH; pos += ALIGN)
	{
		if (!fits(rom->size, pos, BANK_SETS_SIZE) || be16(rom->data + pos) != first)
			continue;
		unsigned i;

		for (i = 1; i < seq_count; i++)
			if (be16(rom->data + pos + i * 2) < be16(rom->data + pos + (i - 1) * 2))
				break;
		if (i == seq_count)
		{
			*found = pos;
			return 0;
		}
	}
	return -1;
}

void sound_free(sound_blobs_t *blobs)
{
	for (unsigned i = 0; i < SOUND_BLOBS; i++)
	{
		free(blobs->data[i]);
		blobs->data[i] = NULL;
	}
}

static uint8_t *copy_rom(const rom_t *rom, size_t off, size_t size)
{
	uint8_t *p;

	if (!fits(rom->size, off, size))
		return NULL;
	p = malloc(size ? size : 1);
	if (p)
		memcpy(p, rom->data + off, size);
	return p;
}

int sound_build(const sound_t *sound, const rom_t *rom, sound_blobs_t *out, char *err, size_t err_size)
{
	size_t seq_offset, seq_size, sets_offset;
	unsigned tbl_count, seq_count;

	memset(out, 0, sizeof *out);
	out->size[0] = sound->ctl_size;
	out->size[1] = sound->tbl_size;
	out->data[0] = copy_rom(rom, sound->ctl_offset, sound->ctl_size);
	out->data[1] = copy_rom(rom, sound->tbl_offset, sound->tbl_size);
	if (!out->data[0] || !out->data[1] || build_ctl(out->data[0], out->size[0]) ||
	    convert_seq_file(out->data[1], out->size[1], &tbl_count))
		goto bad;
	seq_offset = (size_t)sound->tbl_offset + sound->tbl_size;
	if (!fits(rom->size, seq_offset, SEQ_FILE_HEADER + SEQ_FILE_ENTRY) ||
	    file_extent(rom->data + seq_offset, rom->size - seq_offset, &seq_size))
		goto bad;
	seq_count = be16(rom->data + seq_offset + 2);
	out->size[2] = seq_size;
	out->data[2] = copy_rom(rom, seq_offset, seq_size);
	if (!out->data[2] || convert_seq_file(out->data[2], seq_size, &seq_count))
		goto bad;
	if (find_bank_sets(rom, seq_offset + seq_size, seq_count, &sets_offset))
		goto bad;
	out->size[3] = BANK_SETS_SIZE;
	out->data[3] = copy_rom(rom, sets_offset, BANK_SETS_SIZE);
	if (!out->data[3])
		goto bad;
	for (unsigned i = 0; i < seq_count; i++)
		swap_bytes(out->data[3] + i * 2, 2);
	return 0;
bad:
	sound_free(out);
	snprintf(err, err_size, "sound data in the ROM is invalid; is this the right ROM version?");
	return -1;
}
