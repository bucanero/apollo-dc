/*
 * vmu_card.c - one interface for saves on a real VMU and on a VMU image file
 *
 * Image handling (load, byte order, write-back) follows apollo-ps3's dccard.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fat/fs_fat.h>

#include "vmu_card.h"
#include "dcvmu.h"
#include "dcsave.h"
#include "saves.h"
#include "utils.h"

// The open image: 128 KB, kept off the stack
static vmu_card_t* card;
static char card_path[256];
static int card_dcm;

int vmucard_is_device(const char* path)
{
	return (strncmp(path, VMU_PATH, strlen(VMU_PATH)) == 0);
}

static int has_ext(const char* name, const char* ext)
{
	size_t nl = strlen(name), el = strlen(ext);

	return (nl >= el && strcasecmp(name + nl - el, ext) == 0);
}

int vmucard_is_image_name(const char* name)
{
	return has_ext(name, ".BIN") || has_ext(name, ".VMU") || has_ext(name, ".DCM");
}

/*
 * A .DCM holds the card with every 4-byte group reversed. The 0x55 signature
 * reads the same either way, so the byte order is settled by which one gives
 * a usable root block, trying the one the file name suggests first.
 */
static int load_image(const char* path)
{
	uint8_t* buf;
	size_t len;
	int r;

	card_path[0] = 0;
	if (!card && (card = malloc(sizeof(vmu_card_t))) == NULL)
		return 0;

	if (read_buffer(path, &buf, &len) < 0)
		return 0;

	if (len != VMU_SIZE)
	{
		LOG("Error: '%s' is not a VMU image (%d bytes)", path, (int) len);
		free(buf);
		return 0;
	}

	card_dcm = has_ext(path, ".DCM");
	if (card_dcm)
		vmu_swap32(buf, len);

	r = vmu_load(card, buf, len);
	if (r == VMU_ERR_FORMAT)
	{
		vmu_swap32(buf, len);
		if ((r = vmu_load(card, buf, len)) == VMU_OK)
			card_dcm = !card_dcm;
	}
	free(buf);

	LOG("VMU image '%s' loaded (%s, %s)", path, card_dcm ? "DCM" : "raw", vmu_strerror(r));
	if (r != VMU_OK)
		return 0;

	snprintf(card_path, sizeof(card_path), "%s", path);
	return 1;
}

// the image at `path`, loading it if another one (or none) is open
static vmu_card_t* image(const char* path)
{
	if (strcmp(card_path, path) == 0 || load_image(path))
		return card;

	return NULL;
}

// write the open image back to its file, in the byte order it came in
static int save_image(void)
{
	uint8_t* buf;
	int ok;

	if ((buf = malloc(VMU_SIZE)) == NULL)
		return 0;

	memcpy(buf, card->img, VMU_SIZE);
	if (card_dcm)
		vmu_swap32(buf, VMU_SIZE);

	ok = (write_buffer(card_path, buf, VMU_SIZE) == 0);
	free(buf);

	// FAT writes are cached: flush them so the card is safe to pull out
	if (ok && strncmp(card_path, "/sd/", 4) == 0)
		fs_fat_sync("/sd");

	if (!ok)
		LOG("Error saving VMU image '%s'", card_path);

	return ok;
}

int vmucard_open(const char* path)
{
	if (vmucard_is_device(path))
		return vmudev_present(path);

	card_path[0] = 0;   // always re-read the file
	return (image(path) != NULL);
}

int vmucard_is_dcm(const char* path)
{
	return (!vmucard_is_device(path) && image(path) && card_dcm);
}

int vmucard_free_blocks(const char* path)
{
	if (vmucard_is_device(path))
		return vmudev_free_blocks(path);

	return image(path) ? vmu_free_blocks(card) : -1;
}

int vmucard_scan(const char* path, vmudev_file_t** files, int* count)
{
	vmu_dirent_t ent;
	uint8_t* data;
	size_t len, off;
	int n = 0;

	if (vmucard_is_device(path))
		return vmudev_scan(path, files, count);

	*files = NULL;
	*count = 0;
	if (!image(path))
		return 0;

	*files = calloc(vmu_dir_count(card) + 1, sizeof(vmudev_file_t));
	for (int i = 0; *files && i < vmu_dir_count(card); i++)
	{
		vmudev_file_t* f = &(*files)[n];

		if (vmu_dir_get(card, i, &ent) != 1)
			continue;

		vmu_dirent_encode(&ent, f->dirent);
		if (vmu_read_file(card, i, &data, &len) == VMU_OK)
		{
			off = vms_header_offset(&ent);
			if (off + VMUDEV_HEADER_SIZE <= len)
			{
				memcpy(f->header, data + off, VMUDEV_HEADER_SIZE);
				f->has_header = 1;
			}
			free(data);
		}
		n++;
	}

	*count = n;
	return (*files != NULL);
}

int vmucard_find(const char* path, const char* name, uint8_t dirent[VMUDEV_DIRENT_SIZE])
{
	vmu_dirent_t ent;
	int idx;

	if (vmucard_is_device(path))
		return vmudev_find(path, name, dirent);

	if (!image(path) || (idx = vmu_find(card, name)) < 0 || vmu_dir_get(card, idx, &ent) != 1)
		return 0;

	vmu_dirent_encode(&ent, dirent);
	return 1;
}

int vmucard_read(const char* path, const char* name, uint8_t dirent[VMUDEV_DIRENT_SIZE], uint8_t** data, size_t* len)
{
	int idx;

	if (vmucard_is_device(path))
		return vmudev_read_file(path, name, dirent, data, len);

	*data = NULL;
	*len = 0;
	if (!vmucard_find(path, name, dirent) || (idx = vmu_find(card, name)) < 0)
	{
		LOG("Error: '%s' not found in %s", name, path);
		return 0;
	}

	return (vmu_read_file(card, idx, data, len) == VMU_OK);
}

int vmucard_write(const char* path, const uint8_t dirent[VMUDEV_DIRENT_SIZE], const uint8_t* data, size_t len)
{
	vmu_dirent_t ent;
	uint8_t* backup;
	int idx, r;

	if (vmucard_is_device(path))
		return vmudev_write_file(path, dirent, data, len);

	if (!image(path) || (backup = malloc(VMU_SIZE)) == NULL)
		return 0;

	vmu_dirent_decode(dirent, &ent);

	// replace a file with the same name; on failure, leave the card as it was
	memcpy(backup, card->img, VMU_SIZE);
	if ((idx = vmu_find(card, ent.filename)) >= 0)
		vmu_delete_file(card, idx);

	r = vmu_write_file(card, &ent, data, len);
	if (r != VMU_OK)
	{
		LOG("Error: can't add '%s' to %s (%s)", ent.filename, path, vmu_strerror(r));
		// file writes only touch blocks in the image (FAT, directory, data), never the decoded root
		memcpy(card->img, backup, VMU_SIZE);
	}
	free(backup);

	return (r == VMU_OK) && save_image();
}

int vmucard_overwrite(const char* path, const char* name, const uint8_t* data, size_t len)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t *old, *buf;
	size_t old_len, off;
	vmu_dirent_t ent;
	vms_header_t vms;
	int ret, fix_crc = 0;

	if (!vmucard_read(path, name, raw, &old, &old_len))
		return 0;

	if (old_len != len || (buf = malloc(len)) == NULL)
	{
		LOG("Error: can't write '%s' (%d bytes, expected %d)", name, (int) len, (int) old_len);
		free(old);
		return 0;
	}

	memcpy(buf, data, len);
	vmu_dirent_decode(raw, &ent);
	off = vms_header_offset(&ent);

	if (vms_parse(old, old_len, off, &vms) == 0 && vms_calc_crc(&vms) == vms.crc &&
		memcmp(old + off + VMS_CRC_OFFSET, buf + off + VMS_CRC_OFFSET, 2) == 0)
		fix_crc = 1;
	free(old);

	if (fix_crc && vms_update_crc(buf, len, off) == 0)
		LOG("Updated '%s' VMS CRC", name);

	if (vmucard_is_device(path))
		ret = vmudev_write_file(path, raw, buf, len);
	else
		ret = (vmu_overwrite_file(card, vmu_find(card, name), buf, len) == VMU_OK) && save_image();

	free(buf);
	return ret;
}

int vmucard_delete(const char* path, const char* name)
{
	int idx;

	if (vmucard_is_device(path))
		return vmudev_delete(path, name);

	if (!image(path) || (idx = vmu_find(card, name)) < 0)
		return 0;

	return (vmu_delete_file(card, idx) == VMU_OK) && save_image();
}
