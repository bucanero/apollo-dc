/*
 * vmu_card.h - one interface for saves on a real VMU and on a VMU image file
 *
 * `path` is either a VMU plugged into a controller ("/vmu/a1/", through vmu_dev)
 * or a VMU image on storage ("/sd/DC/VMU/card.bin": raw .BIN/.VMU, or Nexus .DCM,
 * through dcvmu). Files are passed around as raw 32-byte directory entries plus
 * data, as vmu_dev does.
 *
 * Images are kept in memory, one at a time, and written back to their file after
 * every change.
 */

#ifndef VMU_CARD_H
#define VMU_CARD_H

#include <stddef.h>
#include <stdint.h>

#include "vmu_dev.h"

/* 1 for "/vmu/xN/", 0 for an image file */
int vmucard_is_device(const char* path);

/* 1 if the file name looks like a VMU image (.BIN, .VMU, .DCM) */
int vmucard_is_image_name(const char* name);

/* Make `path` ready: check the VMU is plugged in, or (re)load the image from its
 * file. Returns 1 on success; an image that isn't formatted fails. */
int vmucard_open(const char* path);

/* 1 if the open image was a Nexus .DCM */
int vmucard_is_dcm(const char* path);

int vmucard_free_blocks(const char* path);

/* Directory plus each file's header block; `files` is malloc'd */
int vmucard_scan(const char* path, vmudev_file_t** files, int* count);

/* Find / read a file by name (trimmed, as shown to the user) */
int vmucard_find(const char* path, const char* name, uint8_t dirent[VMUDEV_DIRENT_SIZE]);
int vmucard_read(const char* path, const char* name, uint8_t dirent[VMUDEV_DIRENT_SIZE], uint8_t** data, size_t* len);

/* Add the file described by `dirent`, replacing one with the same name */
int vmucard_write(const char* path, const uint8_t dirent[VMUDEV_DIRENT_SIZE], const uint8_t* data, size_t len);

/* Replace a file's contents (same size). A VMS CRC that was valid before, and
 * that the new data leaves untouched, is updated to match. */
int vmucard_overwrite(const char* path, const char* name, const uint8_t* data, size_t len);

int vmucard_delete(const char* path, const char* name);

#endif
