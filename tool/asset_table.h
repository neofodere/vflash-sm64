#ifndef ASSET_TABLE_H
#define ASSET_TABLE_H

#include <stddef.h>
#include <stdint.h>

enum
{
	ASSET_RAW = 0,
	ASSET_SKYBOX = 1
};

typedef struct
{
	uint32_t size;
	int32_t mio0;
	uint32_t pos;
	uint32_t kind;
} asset_t;

typedef struct
{
	uint32_t ctl_offset;
	uint32_t ctl_size;
	uint32_t tbl_offset;
	uint32_t tbl_size;
} sound_t;

enum
{
	SOUND_ID_CTL = 236,
	SOUND_ID_TBL = 237,
	SOUND_ID_SEQ = 238,
	SOUND_ID_BANKSETS = 239
};

typedef struct
{
	const char *name;
	const char *sha1;
	const unsigned char *image;
	size_t image_size;
	const asset_t *table;
	unsigned count;
	sound_t sound;
} region_t;

extern const region_t regions[];
extern const unsigned region_count;

#endif
