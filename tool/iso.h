#ifndef ISO_H
#define ISO_H

#include <stddef.h>
#include <stdint.h>

#define ISO_SECTOR_RAW 2352
#define ISO_SECTOR_DATA 2048

typedef struct iso iso_t;

iso_t	*iso_new(const char *volume_id);
void	iso_free(iso_t *iso);
int		iso_add_file(iso_t *iso, const char *path, const uint8_t *data, uint32_t size);
int		iso_write_bin(iso_t *iso, const char *bin_path);
int		iso_write_cue(const char *cue_path, const char *bin_name);
uint32_t	iso_file_lba(const iso_t *iso, const char *path);

#endif
