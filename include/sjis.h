/*
 * sjis.h - Shift-JIS text, as used in Dreamcast VMS save descriptions
 */

#ifndef SJIS_H
#define SJIS_H

#include <stddef.h>
#include <stdint.h>

/* Convert up to `len` bytes of Shift-JIS (stops at NUL) into UTF-8. Full-width
 * ASCII and the ideographic space become plain ASCII, control bytes become
 * spaces. `out` is always NUL terminated. Returns `out`. */
char* sjis_to_utf8(const char* in, size_t len, char* out, size_t outsize);

/* Shift-JIS code of a Unicode character: 0xA1-0xDF for half-width katakana,
 * 0x8140-0xEFFC for two-byte characters. 0 if there is none. */
uint16_t unicode_to_sjis(uint32_t ch);

#endif
