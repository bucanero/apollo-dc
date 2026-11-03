/*
 * vmu_dev.c - access to the VMUs plugged into the Dreamcast controllers
 *
 * KallistiOS vmufs on one side, raw directory entries on the other: see vmu_dev.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <dc/maple.h>
#include <dc/maple/vmu.h>
#include <dc/vmufs.h>

#include "vmu_dev.h"
#include "saves.h"

_Static_assert(sizeof(vmu_dir_t) == VMUDEV_DIRENT_SIZE, "vmu_dir_t is not the on-card directory entry");

// "/vmu/a1/" -> maple device at port A, unit 1
static maple_device_t* get_device(const char* path)
{
	maple_device_t* dev;
	size_t len = strlen(VMU_PATH);

	if (!path || strncmp(path, VMU_PATH, len) != 0 || strlen(path) < len + 2)
		return NULL;

	int port = tolower((unsigned char) path[len]) - 'a';
	int unit = path[len + 1] - '0';
	if (port < 0 || port >= MAPLE_PORT_COUNT || unit < 1 || unit >= MAPLE_UNIT_COUNT)
		return NULL;

	dev = maple_enum_dev(port, unit);
	if (!dev || !dev->valid || !(dev->info.functions & MAPLE_FUNC_MEMCARD))
		return NULL;

	return dev;
}

// the name as stored: 12 bytes, space padded for games, NUL padded for data
static void raw_name(const vmu_dir_t* de, char* out)
{
	memcpy(out, de->filename, VMU_FILENAME_SIZE);
	out[VMU_FILENAME_SIZE] = 0;
}

// the name as shown: padding trimmed
static void trimmed_name(const vmu_dir_t* de, char* out)
{
	raw_name(de, out);
	for (int i = VMU_FILENAME_SIZE - 1; i >= 0 && (out[i] == ' ' || out[i] == 0); i--)
		out[i] = 0;
}

int vmudev_list(char paths[][VMUDEV_PATH_LEN], char names[][VMUDEV_NAME_LEN], int max)
{
	int count = 0;
	maple_device_t* dev;

	for (int i = 0; count < max && (dev = maple_enum_type(i, MAPLE_FUNC_MEMCARD)) != NULL; i++)
	{
		snprintf(paths[count], VMUDEV_PATH_LEN, VMU_PATH "%c%d/", 'a' + dev->port, dev->unit);
		snprintf(names[count], VMUDEV_NAME_LEN, "VMU %c%d (Port %c, Slot %d) - %d blocks free",
			'A' + dev->port, dev->unit, 'A' + dev->port, dev->unit, vmufs_free_blocks(dev));

		LOG("Found VMU: %s [%s]", paths[count], names[count]);
		count++;
	}

	return count;
}

int vmudev_present(const char* path)
{
	return (get_device(path) != NULL);
}

int vmudev_free_blocks(const char* path)
{
	maple_device_t* dev = get_device(path);

	return dev ? vmufs_free_blocks(dev) : -1;
}

int vmudev_scan(const char* path, vmudev_file_t** files, int* count)
{
	uint8_t block[VMU_BLOCK_SIZE];
	vmu_root_t root;
	vmu_dir_t* dir = NULL;
	uint16_t* fat = NULL;
	int dir_count = 0, fat_count, n = 0;
	maple_device_t* dev = get_device(path);

	*files = NULL;
	*count = 0;

	if (!dev || vmufs_root_read(dev, &root) < 0)
	{
		LOG("Error reading VMU root block (%s)", path);
		return 0;
	}

	fat_count = vmufs_fat_blocks(&root) * VMU_BLOCK_SIZE / sizeof(uint16_t);
	fat = malloc(fat_count * sizeof(uint16_t));

	if (!fat || vmufs_fat_read(dev, &root, fat) < 0 || vmufs_readdir(dev, &dir, &dir_count) < 0)
	{
		LOG("Error reading VMU FAT/directory (%s)", path);
		free(fat);
		return 0;
	}

	*files = calloc(dir_count ? dir_count : 1, sizeof(vmudev_file_t));
	for (int i = 0; *files && i < dir_count; i++)
	{
		vmu_dir_t* de = &dir[i];
		vmudev_file_t* f = &(*files)[n];
		uint16_t blk = de->firstblk;
		// the VMS header is at block 0 of a data file, block `hdroff` (normally 1) of a game
		int hdr_blk = (de->hdroff < de->filesize) ? de->hdroff : (de->filetype == VMU_FILE_GAME);

		if (de->filetype != VMU_FILE_DATA && de->filetype != VMU_FILE_GAME)
			continue;

		memcpy(f->dirent, de, VMUDEV_DIRENT_SIZE);

		for (int b = 0; b < hdr_blk && blk < fat_count; b++)
			blk = fat[blk];

		if (blk < fat_count && vmu_block_read(dev, blk, block) == MAPLE_EOK)
		{
			memcpy(f->header, block, VMUDEV_HEADER_SIZE);
			f->has_header = 1;
		}
		else
			LOG("Error reading header block %d of '%.12s'", blk, de->filename);

		n++;
	}

	*count = n;
	free(dir);
	free(fat);

	return (*files != NULL);
}

static int find_dirent(maple_device_t* dev, const char* name, vmu_dir_t* out)
{
	vmu_dir_t* dir;
	int dir_count, found = 0;
	char fn[VMU_FILENAME_SIZE + 1];

	if (vmufs_readdir(dev, &dir, &dir_count) < 0)
		return 0;

	for (int i = 0; i < dir_count && !found; i++)
	{
		trimmed_name(&dir[i], fn);
		if (strcmp(fn, name) == 0)
		{
			memcpy(out, &dir[i], sizeof(vmu_dir_t));
			found = 1;
		}
	}

	free(dir);
	return found;
}

int vmudev_find(const char* path, const char* name, uint8_t dirent[VMUDEV_DIRENT_SIZE])
{
	vmu_dir_t de;
	maple_device_t* dev = get_device(path);

	if (!dev || !find_dirent(dev, name, &de))
		return 0;

	memcpy(dirent, &de, VMUDEV_DIRENT_SIZE);
	return 1;
}

int vmudev_read_file(const char* path, const char* name, uint8_t dirent[VMUDEV_DIRENT_SIZE], uint8_t** data, size_t* len)
{
	vmu_dir_t de;
	void* buf = NULL;
	int size = 0;
	maple_device_t* dev = get_device(path);

	*data = NULL;
	*len = 0;

	if (!dev || !find_dirent(dev, name, &de))
	{
		LOG("Error: '%s' not found on %s", name, path);
		return 0;
	}

	if (vmufs_read_dirent(dev, &de, &buf, &size) < 0)
	{
		LOG("Error reading '%s' from %s", name, path);
		return 0;
	}

	memcpy(dirent, &de, VMUDEV_DIRENT_SIZE);
	*data = buf;
	*len = size;

	return 1;
}

// vmufs_write() stamps the current time: put the original timestamp back
static void restore_timestamp(maple_device_t* dev, const vmu_dir_t* src)
{
	vmu_root_t root;
	vmu_dir_t* dir;
	char fn[VMU_FILENAME_SIZE + 1];
	int idx;

	vmufs_mutex_lock();

	if (vmufs_root_read(dev, &root) < 0 || (dir = malloc(vmufs_dir_blocks(&root) * VMU_BLOCK_SIZE)) == NULL)
		goto unlock;

	raw_name(src, fn);
	if (vmufs_dir_read(dev, &root, dir) == 0 && (idx = vmufs_dir_find(&root, dir, fn)) >= 0)
	{
		dir[idx].timestamp = src->timestamp;
		dir[idx].dirty = 1;
		if (vmufs_dir_write(dev, &root, dir) < 0)
			LOG("Warning: couldn't restore the timestamp of '%s'", fn);
	}

	free(dir);

unlock:
	vmufs_mutex_unlock();
}

int vmudev_write_file(const char* path, const uint8_t dirent[VMUDEV_DIRENT_SIZE], const uint8_t* data, size_t len)
{
	vmu_dir_t de;
	char fn[VMU_FILENAME_SIZE + 1];
	int flags = VMUFS_OVERWRITE, r;
	maple_device_t* dev = get_device(path);

	if (!dev)
		return 0;

	memcpy(&de, dirent, sizeof(vmu_dir_t));
	raw_name(&de, fn);

	if (de.filetype == VMU_FILE_GAME)
		flags |= VMUFS_VMUGAME;
	if (de.copyprotect)
		flags |= VMUFS_NOCOPY;

	// vmufs_write takes a non-const buffer, but only reads it
	r = vmufs_write(dev, fn, (void*) data, (int) len, flags);
	if (r < 0)
	{
		LOG("Error writing '%s' to %s (%d)", fn, path, r);
		return 0;
	}

	restore_timestamp(dev, &de);
	LOG("Wrote '%s' to %s (%d bytes)", fn, path, (int) len);

	return 1;
}

int vmudev_delete(const char* path, const char* name)
{
	vmu_dir_t de;
	char fn[VMU_FILENAME_SIZE + 1];
	maple_device_t* dev = get_device(path);

	if (!dev || !find_dirent(dev, name, &de))
		return 0;

	// vmufs matches all 12 stored bytes: pass the padded name, not the trimmed one
	raw_name(&de, fn);
	if (vmufs_delete(dev, fn) < 0)
	{
		LOG("Error deleting '%s' from %s", name, path);
		return 0;
	}

	LOG("Deleted '%s' from %s", name, path);
	return 1;
}
