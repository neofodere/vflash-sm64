#include "iso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREGAP_SECTORS 150
#define MAX_NODES 256

typedef struct node
{
	char			name[16];
	int				is_dir;
	int				parent;
	const uint8_t	*data;
	uint32_t		size;
	uint32_t		lba;
	uint32_t		dir_sectors;
}	node_t;

struct iso
{
	char		volume_id[33];
	node_t		nodes[MAX_NODES];
	int			count;
	uint32_t	total_sectors;
	uint32_t	path_table_size;
	uint32_t	path_table_l_lba;
	uint32_t	path_table_m_lba;
	uint32_t	path_table_sectors;
};

static uint8_t ecc_f[256];
static uint8_t ecc_b[256];
static uint32_t edc_table[256];
static int tables_ready;

static void init_tables(void)
{
	uint32_t i;
	uint32_t j;
	uint32_t edc;

	if (tables_ready)
		return;
	for (i = 0; i < 256; i++)
	{
		j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
		ecc_f[i] = (uint8_t)j;
		ecc_b[i ^ j] = (uint8_t)i;
		edc = i;
		for (j = 0; j < 8; j++)
			edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001u : 0);
		edc_table[i] = edc;
	}
	tables_ready = 1;
}

static uint32_t edc_compute(const uint8_t *src, size_t size)
{
	uint32_t edc = 0;

	while (size--)
		edc = (edc >> 8) ^ edc_table[(edc ^ *src++) & 0xFF];
	return edc;
}

static void ecc_block(uint8_t *sector, uint32_t major_count, uint32_t minor_count,
	uint32_t major_mult, uint32_t minor_inc, uint8_t *dest)
{
	uint32_t size = major_count * minor_count;
	uint32_t major;
	uint32_t minor;

	for (major = 0; major < major_count; major++)
	{
		uint32_t index = (major >> 1) * major_mult + (major & 1);
		uint8_t ecc_a = 0;
		uint8_t ecc_bb = 0;

		for (minor = 0; minor < minor_count; minor++)
		{
			uint8_t temp = sector[index];

			index += minor_inc;
			if (index >= size)
				index -= size;
			ecc_a ^= temp;
			ecc_bb ^= temp;
			ecc_a = ecc_f[ecc_a];
		}
		ecc_a = ecc_b[ecc_f[ecc_a] ^ ecc_bb];
		dest[major] = ecc_a;
		dest[major + major_count] = ecc_a ^ ecc_bb;
	}
}

static uint8_t bcd(uint32_t v)
{
	return (uint8_t)(((v / 10) << 4) | (v % 10));
}

static void encode_sector(uint8_t *out, const uint8_t *data, uint32_t lba)
{
	static const uint8_t sync[12] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};
	uint32_t abs = lba + PREGAP_SECTORS;
	uint32_t edc;

	memset(out, 0, ISO_SECTOR_RAW);
	memcpy(out, sync, 12);
	out[12] = bcd(abs / (60 * 75));
	out[13] = bcd(abs / 75 % 60);
	out[14] = bcd(abs % 75);
	out[15] = 1;
	if (data)
		memcpy(out + 16, data, ISO_SECTOR_DATA);
	edc = edc_compute(out, 2064);
	out[2064] = edc & 255;
	out[2065] = (edc >> 8) & 255;
	out[2066] = (edc >> 16) & 255;
	out[2067] = (edc >> 24) & 255;
	ecc_block(out + 12, 86, 24, 2, 86, out + 2076);
	ecc_block(out + 12, 52, 43, 86, 88, out + 2248);
}

iso_t *iso_new(const char *volume_id)
{
	iso_t *iso = calloc(1, sizeof(*iso));

	if (!iso)
		return NULL;
	init_tables();
	snprintf(iso->volume_id, sizeof(iso->volume_id), "%s", volume_id);
	iso->nodes[0].is_dir = 1;
	iso->nodes[0].parent = 0;
	iso->count = 1;
	return iso;
}

void iso_free(iso_t *iso)
{
	if (!iso)
		return;
	for (int i = 0; i < iso->count; i++)
		free((void *)iso->nodes[i].data);
	free(iso);
}

static int find_child(const iso_t *iso, int parent, const char *name)
{
	for (int i = 1; i < iso->count; i++)
		if (iso->nodes[i].parent == parent && !strcmp(iso->nodes[i].name, name))
			return i;
	return -1;
}

int iso_add_file(iso_t *iso, const char *path, const uint8_t *data, uint32_t size)
{
	char buf[256];
	char *tok;
	char *next;
	int parent = 0;

	snprintf(buf, sizeof(buf), "%s", path);
	for (tok = buf; tok; tok = next)
	{
		int found;

		next = strchr(tok, '/');
		if (next)
			*next++ = 0;
		if (strlen(tok) > 15 || iso->count >= MAX_NODES)
			return -1;
		found = find_child(iso, parent, tok);
		if (found < 0)
		{
			found = iso->count++;
			snprintf(iso->nodes[found].name, sizeof(iso->nodes[found].name), "%s", tok);
			iso->nodes[found].parent = parent;
			iso->nodes[found].is_dir = next != NULL;
		}
		if (!next)
		{
			uint8_t *copy = malloc(size ? size : 1);

			if (!copy)
				return -1;
			memcpy(copy, data, size);
			free((void *)iso->nodes[found].data);
			iso->nodes[found].data = copy;
			iso->nodes[found].size = size;
		}
		parent = found;
	}
	return 0;
}

static int node_cmp_name(const iso_t *iso, int a, int b)
{
	return strcmp(iso->nodes[a].name, iso->nodes[b].name);
}

static uint32_t dir_record_len(const node_t *n)
{
	uint32_t id = (uint32_t)strlen(n->name) + (n->is_dir ? 0 : 2);
	uint32_t len = 33 + id;

	return len + (len & 1);
}

static uint32_t dir_size_sectors(const iso_t *iso, int dir)
{
	uint32_t used = 34 + 34;
	uint32_t sectors = 1;

	for (int i = 1; i < iso->count; i++)
	{
		uint32_t len;

		if (iso->nodes[i].parent != dir)
			continue;
		len = dir_record_len(&iso->nodes[i]);
		if (used + len > ISO_SECTOR_DATA)
		{
			sectors++;
			used = 0;
		}
		used += len;
	}
	return sectors;
}

static void put32_both(uint8_t *p, uint32_t v)
{
	p[0] = v & 255;
	p[1] = (v >> 8) & 255;
	p[2] = (v >> 16) & 255;
	p[3] = (v >> 24) & 255;
	p[4] = (v >> 24) & 255;
	p[5] = (v >> 16) & 255;
	p[6] = (v >> 8) & 255;
	p[7] = v & 255;
}

static void put16_both(uint8_t *p, uint16_t v)
{
	p[0] = v & 255;
	p[1] = v >> 8;
	p[2] = v >> 8;
	p[3] = v & 255;
}

static uint32_t put_record(uint8_t *p, const node_t *n, const char *id, uint32_t id_len, int is_dir)
{
	uint32_t len = 33 + id_len;

	len += len & 1;
	memset(p, 0, len);
	p[0] = (uint8_t)len;
	put32_both(p + 2, n->lba);
	put32_both(p + 10, n->size);
	p[18] = 70;
	p[19] = 1;
	p[20] = 1;
	p[21] = 0;
	p[22] = 0;
	p[23] = 0;
	p[25] = is_dir ? 2 : 0;
	put16_both(p + 28, 1);
	p[32] = (uint8_t)id_len;
	memcpy(p + 33, id, id_len);
	return len;
}

static void build_dir(const iso_t *iso, int dir, uint8_t *out)
{
	uint8_t *p = out;
	uint32_t sector_end = ISO_SECTOR_DATA;
	int order[MAX_NODES];
	int n = 0;
	node_t self = iso->nodes[dir];
	node_t up = iso->nodes[iso->nodes[dir].parent];

	self.size = iso->nodes[dir].dir_sectors * ISO_SECTOR_DATA;
	up.size = iso->nodes[iso->nodes[dir].parent].dir_sectors * ISO_SECTOR_DATA;
	p += put_record(p, &self, "\0", 1, 1);
	p += put_record(p, &up, "\1", 1, 1);
	for (int i = 1; i < iso->count; i++)
		if (iso->nodes[i].parent == dir)
			order[n++] = i;
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && node_cmp_name(iso, order[j - 1], order[j]) > 0; j--)
		{
			int t = order[j];

			order[j] = order[j - 1];
			order[j - 1] = t;
		}
	for (int i = 0; i < n; i++)
	{
		const node_t *c = &iso->nodes[order[i]];
		char id[32];
		uint32_t id_len = (uint32_t)snprintf(id, sizeof(id), c->is_dir ? "%s" : "%s;1", c->name);
		uint32_t len = 33 + id_len;

		len += len & 1;
		if ((uint32_t)(p - out) + len > sector_end)
		{
			p = out + sector_end;
			sector_end += ISO_SECTOR_DATA;
		}
		p += put_record(p, c, id, id_len, c->is_dir);
	}
}

static uint32_t path_table_build(const iso_t *iso, const int *dirs, int ndirs, uint8_t *out, int big)
{
	uint8_t *p = out;

	for (int i = 0; i < ndirs; i++)
	{
		const node_t *n = &iso->nodes[dirs[i]];
		uint32_t id_len = dirs[i] == 0 ? 1 : (uint32_t)strlen(n->name);
		int parent_index = 1;

		for (int k = 0; k < ndirs; k++)
			if (dirs[k] == n->parent)
				parent_index = k + 1;
		p[0] = (uint8_t)id_len;
		p[1] = 0;
		if (big)
		{
			p[2] = n->lba >> 24;
			p[3] = (n->lba >> 16) & 255;
			p[4] = (n->lba >> 8) & 255;
			p[5] = n->lba & 255;
			p[6] = (uint8_t)(parent_index >> 8);
			p[7] = (uint8_t)parent_index;
		}
		else
		{
			p[2] = n->lba & 255;
			p[3] = (n->lba >> 8) & 255;
			p[4] = (n->lba >> 16) & 255;
			p[5] = n->lba >> 24;
			p[6] = (uint8_t)parent_index;
			p[7] = (uint8_t)(parent_index >> 8);
		}
		if (dirs[i] == 0)
			p[8] = 0;
		else
			memcpy(p + 8, n->name, id_len);
		p += 8 + id_len + (id_len & 1);
	}
	return (uint32_t)(p - out);
}

static int collect_dirs(const iso_t *iso, int *dirs)
{
	int n = 0;
	int head = 0;

	dirs[n++] = 0;
	while (head < n)
	{
		int tmp[MAX_NODES];
		int cnt = 0;

		for (int i = 1; i < iso->count; i++)
			if (iso->nodes[i].is_dir && iso->nodes[i].parent == dirs[head])
				tmp[cnt++] = i;
		for (int i = 1; i < cnt; i++)
			for (int j = i; j > 0 && node_cmp_name(iso, tmp[j - 1], tmp[j]) > 0; j--)
			{
				int t = tmp[j];

				tmp[j] = tmp[j - 1];
				tmp[j - 1] = t;
			}
		for (int i = 0; i < cnt; i++)
			dirs[n++] = tmp[i];
		head++;
	}
	return n;
}

static void layout(iso_t *iso, const int *dirs, int ndirs)
{
	uint8_t scratch[ISO_SECTOR_DATA * 8];
	uint32_t lba = 16 + 2;
	uint32_t pt_size;

	pt_size = path_table_build(iso, dirs, ndirs, scratch, 0);
	iso->path_table_sectors = (pt_size + ISO_SECTOR_DATA - 1) / ISO_SECTOR_DATA;
	iso->path_table_l_lba = lba;
	lba += iso->path_table_sectors;
	iso->path_table_m_lba = lba;
	lba += iso->path_table_sectors;
	for (int i = 0; i < ndirs; i++)
	{
		node_t *n = &iso->nodes[dirs[i]];

		n->dir_sectors = dir_size_sectors(iso, dirs[i]);
		n->lba = lba;
		n->size = n->dir_sectors * ISO_SECTOR_DATA;
		lba += n->dir_sectors;
	}
	for (int i = 1; i < iso->count; i++)
	{
		node_t *n = &iso->nodes[i];

		if (n->is_dir)
			continue;
		n->lba = lba;
		lba += (n->size + ISO_SECTOR_DATA - 1) / ISO_SECTOR_DATA;
	}
	iso->total_sectors = lba;
}

static void put_text(uint8_t *p, size_t len, const char *s)
{
	memset(p, ' ', len);
	memcpy(p, s, strlen(s) < len ? strlen(s) : len);
}

static void build_pvd(const iso_t *iso, uint8_t *s, const int *dirs, int ndirs)
{
	uint8_t scratch[ISO_SECTOR_DATA * 8];
	uint32_t pt_size = path_table_build(iso, dirs, ndirs, scratch, 0);
	const uint8_t *date = (const uint8_t *)"2026111200000000";
	node_t root = iso->nodes[0];

	memset(s, 0, ISO_SECTOR_DATA);
	s[0] = 1;
	memcpy(s + 1, "CD001", 5);
	s[6] = 1;
	put_text(s + 8, 32, "");
	put_text(s + 40, 32, iso->volume_id);
	put32_both(s + 80, iso->total_sectors);
	put16_both(s + 120, 1);
	put16_both(s + 124, 1);
	put16_both(s + 128, ISO_SECTOR_DATA);
	put32_both(s + 132, pt_size);
	s[140] = iso->path_table_l_lba & 255;
	s[141] = (iso->path_table_l_lba >> 8) & 255;
	s[142] = (iso->path_table_l_lba >> 16) & 255;
	s[143] = iso->path_table_l_lba >> 24;
	s[151] = iso->path_table_m_lba & 255;
	s[150] = (iso->path_table_m_lba >> 8) & 255;
	s[149] = (iso->path_table_m_lba >> 16) & 255;
	s[148] = iso->path_table_m_lba >> 24;
	put_record(s + 156, &root, "\0", 1, 1);
	put_text(s + 190, 128, "");
	put_text(s + 318, 128, "");
	put_text(s + 446, 128, "");
	put_text(s + 574, 128, "");
	put_text(s + 702, 37, "");
	put_text(s + 739, 37, "");
	put_text(s + 776, 37, "");
	memcpy(s + 813, date, 16);
	memcpy(s + 830, date, 16);
	memset(s + 847, '0', 16);
	memset(s + 864, '0', 16);
	s[881] = 1;
}

int iso_write_bin(iso_t *iso, const char *bin_path)
{
	int dirs[MAX_NODES];
	int ndirs = collect_dirs(iso, dirs);
	uint8_t data[ISO_SECTOR_DATA];
	uint8_t raw[ISO_SECTOR_RAW];
	uint8_t *pt = calloc(1, ISO_SECTOR_DATA * 8);
	uint8_t *dirbuf = calloc(1, ISO_SECTOR_DATA * 64);
	FILE *f;
	uint32_t lba = 0;

	layout(iso, dirs, ndirs);
	f = fopen(bin_path, "wb");
	if (!f || !pt || !dirbuf)
	{
		free(pt);
		free(dirbuf);
		if (f)
			fclose(f);
		return -1;
	}
	for (; lba < 16; lba++)
	{
		encode_sector(raw, NULL, lba);
		fwrite(raw, 1, ISO_SECTOR_RAW, f);
	}
	build_pvd(iso, data, dirs, ndirs);
	encode_sector(raw, data, lba++);
	fwrite(raw, 1, ISO_SECTOR_RAW, f);
	memset(data, 0, sizeof(data));
	data[0] = 255;
	memcpy(data + 1, "CD001", 5);
	data[6] = 1;
	encode_sector(raw, data, lba++);
	fwrite(raw, 1, ISO_SECTOR_RAW, f);
	for (int big = 0; big < 2; big++)
	{
		memset(pt, 0, ISO_SECTOR_DATA * 8);
		path_table_build(iso, dirs, ndirs, pt, big);
		for (uint32_t s = 0; s < iso->path_table_sectors; s++)
		{
			encode_sector(raw, pt + s * ISO_SECTOR_DATA, lba++);
			fwrite(raw, 1, ISO_SECTOR_RAW, f);
		}
	}
	for (int i = 0; i < ndirs; i++)
	{
		node_t *n = &iso->nodes[dirs[i]];

		memset(dirbuf, 0, ISO_SECTOR_DATA * 64);
		build_dir(iso, dirs[i], dirbuf);
		for (uint32_t s = 0; s < n->dir_sectors; s++)
		{
			encode_sector(raw, dirbuf + s * ISO_SECTOR_DATA, lba++);
			fwrite(raw, 1, ISO_SECTOR_RAW, f);
		}
	}
	for (int i = 1; i < iso->count; i++)
	{
		const node_t *n = &iso->nodes[i];
		uint32_t pos = 0;

		if (n->is_dir)
			continue;
		while (pos < n->size)
		{
			uint32_t chunk = n->size - pos < ISO_SECTOR_DATA ? n->size - pos : ISO_SECTOR_DATA;

			memset(data, 0, sizeof(data));
			memcpy(data, n->data + pos, chunk);
			encode_sector(raw, data, lba++);
			fwrite(raw, 1, ISO_SECTOR_RAW, f);
			pos += chunk;
		}
	}
	free(pt);
	free(dirbuf);
	if (fclose(f) != 0)
		return -1;
	return lba == iso->total_sectors ? 0 : -1;
}

int iso_write_cue(const char *cue_path, const char *bin_name)
{
	FILE *f = fopen(cue_path, "wb");

	if (!f)
		return -1;
	fprintf(f, "FILE \"%s\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n", bin_name);
	return fclose(f) == 0 ? 0 : -1;
}

uint32_t iso_file_lba(const iso_t *iso, const char *path)
{
	int parent = 0;
	char buf[256];
	char *tok;
	char *next;

	snprintf(buf, sizeof(buf), "%s", path);
	for (tok = buf; tok; tok = next)
	{
		int found;

		next = strchr(tok, '/');
		if (next)
			*next++ = 0;
		found = find_child(iso, parent, tok);
		if (found < 0)
			return 0;
		if (!next)
			return iso->nodes[found].lba;
		parent = found;
	}
	return 0;
}
