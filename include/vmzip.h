/*
 * vmzip.h - Bucanero's VMZip compressed saves
 *
 * VMZip (2005, https://github.com/bucanero/vmzip) stores a bzip2-compressed
 * copy of a VMU save as a data file named VMZIPDAT.NNN. The file is a normal
 * VMS package:
 *
 *   0x00  VMU description   the original file name, space padded
 *   0x10  DC description    "VMZip Compressed Save" ("Bucanero's VMZip Compressed Save" in VMZip 1.0)
 *   0x30  app ID            byte 0: original size in blocks, then "VMZip/Apollo" (" Bucanero VMZip")
 *   0x40  1 icon, no animation, no eyecatch; CRC; data length
 *   0x60  VMZip icon palette, 0x80 icon bitmap
 *   0x280 bzip2 stream (block size 100k) of the whole original file
 *
 * VMZip 1.0 reads only the name prefix, the size byte, the VMU description and
 * the bzip2 stream, so Apollo's own descriptions don't get in its way.
 *
 * Only data files go through it: VMZip writes every save back as a data file,
 * so a mini-game would come back broken.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef VMZIP_H
#define VMZIP_H

#include <stddef.h>
#include <stdint.h>

#define VMZIP_PREFIX        "VMZIPDAT."
#define VMZIP_MAX_EXT       999

/* 1 if `name` is VMZIPDAT.NNN */
int vmzip_is_vmzip(const char* name);

/* NNN of a VMZIPDAT.NNN name, or -1 */
int vmzip_name_ext(const char* name);

/* Original name and size (blocks) from the first 0x40+ bytes of a VMZip file.
 * Returns 1 if the header looks like VMZip's. `name` takes 13 bytes. */
int vmzip_header_info(const uint8_t* hdr, size_t len, char* name, int* blocks);

/* Compress the save `orig_name` (`data`, `len` bytes, a multiple of 512) into a
 * VMZip file. *out is malloc'd and padded to whole blocks. Returns 1 on success. */
int vmzip_pack(const char* orig_name, const uint8_t* data, size_t len, uint8_t** out, size_t* out_len);

/* Decompress a VMZip file. *out is malloc'd (blocks * 512 bytes); `orig_name`
 * takes 13 bytes. Returns 1 on success. */
int vmzip_unpack(const uint8_t* data, size_t len, char* orig_name, uint8_t** out, size_t* out_len);

#endif
