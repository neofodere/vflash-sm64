#include "asset_table.h"
#include "image.h"
#include "iso.h"
#include "rom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VERSION
#define VERSION "dev"
#endif

static void usage(const char *prog)
{
	fprintf(stderr,
		"vflash-sm64 %s\n"
		"Usage: %s [--region <name>] <baserom.z64> [output-name]\n"
		"Builds <output-name>.cue and <output-name>.bin (default: SuperMario64)\n"
		"from a Super Mario 64 ROM (z64, v64 or n64 byte order). The ROM is not modified.\n"
		"The release is detected from the ROM; --region skips the check (for development).\n"
		"Supported releases:",
		VERSION, prog);
	for (unsigned i = 0; i < region_count; i++)
		fprintf(stderr, " %s", regions[i].name);
	fprintf(stderr, "\n");
}

static const region_t *find_region(const char *name, const char *sha1)
{
	for (unsigned i = 0; i < region_count; i++)
		if (name ? !strcmp(regions[i].name, name) : !strcmp(regions[i].sha1, sha1))
			return &regions[i];
	return NULL;
}

static void add_ext(char *dst, size_t n, const char *base, const char *ext)
{
	snprintf(dst, n, "%s%s", base, ext);
}

int main(int argc, char **argv)
{
	const char *forced = NULL;
	int ai = 1;
	if (argc > 2 && !strcmp(argv[1], "--region"))
	{
		forced = argv[2];
		ai += 2;
	}
	if (argc - ai < 1 || argc - ai > 2 || !strcmp(argv[ai], "-h") || !strcmp(argv[ai], "--help"))
	{
		usage(argv[0]);
		return argc - ai < 1 ? 2 : 0;
	}
	const char *base = argc - ai == 2 ? argv[ai + 1] : "SuperMario64";
	char err[256];
	rom_t rom;
	char sha1[41];
	if (rom_load(&rom, argv[ai], err, sizeof err))
	{
		fprintf(stderr, "error: %s\n", err);
		return 1;
	}
	rom_sha1_hex(&rom, sha1);
	const region_t *region = find_region(forced, sha1);
	if (!region)
	{
		fprintf(stderr, forced ? "error: unknown region %s\n" : "error: unsupported ROM (SHA-1 %s)\n",
			forced ? forced : sha1);
		rom_free(&rom);
		return 1;
	}
	printf("detected release: %s\n", region->name);
	iso_t *iso = iso_new("SUPER_MARIO_64");
	if (!iso || image_build(iso, region, &rom, err, sizeof err))
	{
		fprintf(stderr, "error: %s\n", iso ? err : "out of memory");
		rom_free(&rom);
		return 1;
	}
	rom_free(&rom);
	char bin[1024], cue[1024];
	add_ext(bin, sizeof bin, base, ".bin");
	add_ext(cue, sizeof cue, base, ".cue");
	const char *slash = strrchr(bin, '/');
	const char *back = strrchr(bin, '\\');
	const char *name = slash > back ? slash + 1 : back ? back + 1 : bin;
	if (iso_write_bin(iso, bin) || iso_write_cue(cue, name))
	{
		fprintf(stderr, "error: cannot write %s / %s\n", bin, cue);
		iso_free(iso);
		return 1;
	}
	iso_free(iso);
	printf("wrote %s and %s\n", cue, bin);
	return 0;
}
