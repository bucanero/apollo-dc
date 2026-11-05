/*
 * vmu_dev.h - access to the VMUs plugged into the Dreamcast controllers
 *
 * The only part of Apollo that talks to KallistiOS' vmufs. Its VMU types clash
 * with dcvmu.h, so files cross this boundary as raw 32-byte directory entries
 * (the on-card layout, also used by .DCI), decoded with vmu_dirent_decode().
 *
 * A device is named by its fs_vmu path: "/vmu/a1/" is port A, slot 1.
 */

#ifndef VMU_DEV_H
#define VMU_DEV_H

#include <stddef.h>
#include <stdint.h>

#define VMUDEV_DIRENT_SIZE      32
#define VMUDEV_HEADER_SIZE      0x80
#define VMUDEV_PATH_LEN         16
#define VMUDEV_NAME_LEN         48

typedef struct {
	uint8_t dirent[VMUDEV_DIRENT_SIZE];     /* raw directory entry */
	uint8_t header[VMUDEV_HEADER_SIZE];     /* first 0x80 bytes of the VMS header block */
	int     has_header;                     /* header block was read */
} vmudev_file_t;

/* Find every VMU. Fills `paths` ("/vmu/a1/") and display `names`; returns the count. */
int vmudev_list(char paths[][VMUDEV_PATH_LEN], char names[][VMUDEV_NAME_LEN], int max);

/* 1 if a VMU is plugged in at `path` */
int vmudev_present(const char* path);

int vmudev_free_blocks(const char* path);

/* Read the directory, plus each file's header block. `files` is malloc'd. */
int vmudev_scan(const char* path, vmudev_file_t** files, int* count);

/* Find a file by name (trimmed, as shown to the user). Returns 1 if found. */
int vmudev_find(const char* path, const char* name, uint8_t dirent[VMUDEV_DIRENT_SIZE]);

/* Read a whole file; `data` is malloc'd. `dirent` gets its directory entry. */
int vmudev_read_file(const char* path, const char* name, uint8_t dirent[VMUDEV_DIRENT_SIZE], uint8_t** data, size_t* len);

/* Write a file described by `dirent` (name, type, copy flag, timestamp),
 * replacing one with the same name. Returns 1 on success. */
int vmudev_write_file(const char* path, const uint8_t dirent[VMUDEV_DIRENT_SIZE], const uint8_t* data, size_t len);

/* Raw image of the whole card: every block, below the filesystem */
#define VMUDEV_BLOCKS           256
#define VMUDEV_IMAGE_SIZE       (VMUDEV_BLOCKS * 512)

/* Read all VMUDEV_BLOCKS blocks into `image` (VMUDEV_IMAGE_SIZE bytes). `progress`,
 * if set, is called after each block. Returns 1 on success. */
int vmudev_dump(const char* path, uint8_t* image, void (*progress)(int done, int total));

/* Delete a file by name (trimmed). Returns 1 on success. */
int vmudev_delete(const char* path, const char* name);

#endif
