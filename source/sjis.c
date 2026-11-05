/*
 * sjis.c - Shift-JIS text, as used in Dreamcast VMS save descriptions
 *
 * shiftJIS_convTable (include/shiftjis.h) maps Shift-JIS to UTF-16, big endian:
 *   entries 0x0000-0x00FF   single bytes
 *   entries 0x0100-0x10FF   lead bytes 0x80-0x8F, then the trail byte
 *   entries 0x1100-0x20FF   lead bytes 0x90-0x9F
 *   entries 0x2100-0x30FF   lead bytes 0xE0-0xEF
 */

#include <stdlib.h>
#include <string.h>

#include "sjis.h"
#include "shiftjis.h"

#define SJIS_ENTRIES    (sizeof(shiftJIS_convTable) / 2)

static uint16_t table_unicode(size_t idx)
{
	return (shiftJIS_convTable[idx * 2] << 8) | shiftJIS_convTable[idx * 2 + 1];
}

static size_t table_index(uint8_t lead, uint8_t trail, int* two_bytes)
{
	size_t base;

	switch (lead >> 4)
	{
	case 0x8: base = 0x0100; break;
	case 0x9: base = 0x1100; break;
	case 0xE: base = 0x2100; break;
	default:
		*two_bytes = 0;
		return lead;
	}

	*two_bytes = 1;
	return base + ((lead & 0x0F) << 8) + trail;
}

static uint16_t index_to_sjis(size_t idx)
{
	if (idx < 0x100)
		return idx;

	uint8_t lead = (idx < 0x1100) ? 0x80 : (idx < 0x2100) ? 0x90 : 0xE0;
	idx -= (lead == 0x80) ? 0x0100 : (lead == 0x90) ? 0x1100 : 0x2100;

	return ((lead | (idx >> 8)) << 8) | (idx & 0xFF);
}

static size_t put_utf8(char* out, uint32_t ch)
{
	if (ch < 0x80)
	{
		out[0] = ch;
		return 1;
	}
	if (ch < 0x800)
	{
		out[0] = 0xC0 | (ch >> 6);
		out[1] = 0x80 | (ch & 0x3F);
		return 2;
	}
	out[0] = 0xE0 | (ch >> 12);
	out[1] = 0x80 | ((ch >> 6) & 0x3F);
	out[2] = 0x80 | (ch & 0x3F);
	return 3;
}

char* sjis_to_utf8(const char* in, size_t len, char* out, size_t outsize)
{
	size_t i = 0, o = 0;
	const uint8_t* s = (const uint8_t*) in;

	while (i < len && s[i] && o + 4 < outsize)
	{
		int two;
		size_t idx = table_index(s[i], (i + 1 < len) ? s[i + 1] : 0, &two);
		uint32_t ch;

		if (two && (i + 1 >= len || !s[i + 1]))
			break;      // truncated two-byte character

		ch = (idx < SJIS_ENTRIES) ? table_unicode(idx) : 0;
		i += two ? 2 : 1;

		if (ch == 0 && two)                     // not in the table
			ch = '?';
		else if (ch == 0x3000)                  // ideographic space
			ch = ' ';
		else if (ch >= 0xFF01 && ch <= 0xFF5E)  // full-width ASCII
			ch -= 0xFEE0;
		else if (ch < 0x20 || ch == 0x7F)
			ch = ' ';

		o += put_utf8(out + o, ch);
	}

	out[o] = 0;
	return out;
}

/* Unicode -> Shift-JIS: the table inverted once, sorted by Unicode, binary searched */
typedef struct { uint16_t unicode, sjis; } sjis_pair_t;

static sjis_pair_t* reverse;
static size_t reverse_count;

static int pair_cmp(const void* a, const void* b)
{
	return (int) ((const sjis_pair_t*) a)->unicode - (int) ((const sjis_pair_t*) b)->unicode;
}

static void build_reverse(void)
{
	reverse = malloc(SJIS_ENTRIES * sizeof(sjis_pair_t));
	if (!reverse)
		return;

	// only two-byte characters: single bytes are ASCII (drawn by the TTF) or half-width kana (mapped directly)
	for (size_t idx = 0x100; idx < SJIS_ENTRIES; idx++)
	{
		uint16_t u = table_unicode(idx);
		uint16_t sj = index_to_sjis(idx);

		// skip unassigned slots, and trail bytes outside the valid 0x40-0xFC range
		if (u <= 0x20 || (sj & 0xFF) < 0x40 || (sj & 0xFF) > 0xFC || (sj & 0xFF) == 0x7F)
			continue;

		reverse[reverse_count].unicode = u;
		reverse[reverse_count].sjis = sj;
		reverse_count++;
	}

	qsort(reverse, reverse_count, sizeof(sjis_pair_t), pair_cmp);
}

uint16_t unicode_to_sjis(uint32_t ch)
{
	sjis_pair_t key, *hit;

	if (ch >= 0xFF61 && ch <= 0xFF9F)   // half-width katakana
		return ch - 0xFEC0;

	if (ch > 0xFFFF)
		return 0;

	if (!reverse)
		build_reverse();

	key.unicode = ch;
	hit = reverse ? bsearch(&key, reverse, reverse_count, sizeof(sjis_pair_t), pair_cmp) : NULL;

	return hit ? hit->sjis : 0;
}
