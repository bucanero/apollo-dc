#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>

#include "saves.h"
#include "common.h"
#include "settings.h"
#include "utils.h"
#include "dcvmu.h"
#include "dcsave.h"
#include "vmu_dev.h"
#include "vmu_card.h"
#include "sjis.h"

#define UTF8_CHAR_STAR		"\xE2\x98\x85"

#define CHAR_ICON_NET		"\x09"
#define CHAR_ICON_ZIP		"\x0C"
#define CHAR_ICON_COPY		"\x0B"
#define CHAR_ICON_SIGN		"\x06"
#define CHAR_ICON_USER		"\x07"
#define CHAR_ICON_LOCK		"\x08"
#define CHAR_ICON_WARN		"\x0F"


/*
 * Function:		endsWith()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Checks to see if a ends with b
 * Arguments:
 *	a:				String
 *	b:				Potential end
 * Return:			pointer if true, NULL if false
 */
static char* endsWith(const char * a, const char * b)
{
	int al = strlen(a), bl = strlen(b);
    
	if (al < bl)
		return NULL;

	a += (al - bl);
	while (*a)
		if (toupper(*a++) != toupper(*b++)) return NULL;

	return (char*) (a - bl);
}

/*
 * Function:		readFile()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		reads the contents of a file into a new buffer
 * Arguments:
 *	path:			Path to file
 * Return:			Pointer to the newly allocated buffer
 */
char * readTextFile(const char * path, long* size)
{
	FILE *f = fopen(path, "rb");

	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsize <= 0)
	{
		fclose(f);
		return NULL;
	}

	char * string = malloc(fsize + 1);
	fread(string, fsize, 1, f);
	fclose(f);

	string[fsize] = 0;
	if (size)
		*size = fsize;

	return string;
}

static code_entry_t* _createCmdCode(uint8_t type, const char* name, char code)
{
	code_entry_t* entry = (code_entry_t *)calloc(1, sizeof(code_entry_t));
	entry->type = type;
	entry->name = strdup(name);
	asprintf(&entry->codes, "%c", code);

	return entry;
}

static option_entry_t* _initOptions(int count)
{
	option_entry_t* options = (option_entry_t*)calloc(count, sizeof(option_entry_t));

	for(int i = 0; i < count; i++)
	{
		options[i].sel = -1;
		options[i].opts = list_alloc();
	}

	return options;
}

static save_entry_t* _createSaveEntry(uint16_t flag, const char* name)
{
	save_entry_t* entry = (save_entry_t *)calloc(1, sizeof(save_entry_t));
	entry->flags = flag;
	entry->name = strdup(name);

	return entry;
}

/*
 * Dreamcast VMU helpers
 *
 * Saves live on the VMU as single files. Each one carries a VMS header (descriptions,
 * creator app, icons, CRC) at block 0 of a data file, or block `hdroff` of a mini-game.
 */

// A VMS text field for display: Shift-JIS converted to UTF-8, padding trimmed.
// UTF-8 can take 3 bytes per Shift-JIS byte (half-width kana): size `dst` for that
#define DC_TEXT_SIZE(n)     ((n) * 3 + 1)

static void dc_copy_text(char* dst, size_t dst_size, const char* src, int len)
{
	int j;

	sjis_to_utf8(src, len, dst, dst_size);

	j = strlen(dst);
	while (j > 0 && dst[j - 1] == ' ')
		dst[--j] = 0;

	char* start = dst;
	while (*start == ' ')
		start++;
	memmove(dst, start, strlen(start) + 1);
}

// Title from the first 0x80 bytes of a VMS header: the Dreamcast description, else the VMU one
static int dc_header_title(const uint8_t* hdr, char* title, int size)
{
	char tmp[DC_TEXT_SIZE(32)];

	// same plausibility check as vms_parse(): files like ICONDATA_VMS have no VMS header
	if (read_le_uint16(hdr + 0x40) > 16 || read_le_uint16(hdr + 0x44) > 3)
		return 0;

	dc_copy_text(tmp, sizeof(tmp), (const char*) hdr + 0x10, 32);
	if (!tmp[0])
		dc_copy_text(tmp, sizeof(tmp), (const char*) hdr, 16);

	snprintf(title, size, "%s", tmp);
	return (title[0] != 0);
}

// dctitleid.txt (from apollo-patches): "PATTERN<tab>PATCH_ID<tab>TITLE", first match wins
static void dc_get_patch_id(const char* titleid_db, const char* filename, char* patch_id, size_t size)
{
	char pattern[32], id[32];
	const char* line = titleid_db;

	snprintf(patch_id, size, "%s", filename);

	while (line && *line)
	{
		if (*line != '#' && sscanf(line, "%31[^\t\r\n]\t%31[^\t\r\n]", pattern, id) == 2 &&
			wildcard_match_icase(filename, pattern))
		{
			snprintf(patch_id, size, "%s", id);
			return;
		}

		line = strchr(line, '\n');
		if (line)
			line++;
	}
}

static option_value_t* _newOption(const char* name, const char* value, int value_len)
{
	option_value_t* optval = malloc(sizeof(option_value_t));

	optval->name = strdup(name);
	optval->value = malloc(value_len + 1);
	memcpy(optval->value, value, value_len);
	optval->value[value_len] = 0;

	return optval;
}

// Export / import destinations on the Dreamcast: SD card adapter, and the PC through dcload
static option_entry_t* _createStorageOptions(const char* name, char cmd)
{
	char label[128];
	char value[2] = {cmd, STORAGE_MASS};
	option_entry_t* options = _initOptions(1);

	snprintf(label, sizeof(label), "%s (%s)", name, USB_PATH);
	list_append(options[0].opts, _newOption(label, value, 2));

	value[1] = STORAGE_HOST;
	snprintf(label, sizeof(label), "%s (%s)", name, HOST_PATH);
	list_append(options[0].opts, _newOption(label, value, 2));

	return options;
}

// Every other VMU plugged in; the value is {cmd, STORAGE_VMU, "/vmu/xN/"}. NULL if there is none.
static option_entry_t* _createVmuOptions(const char* name, char cmd, const char* exclude)
{
	char paths[MAX_VMU_DEVICES][VMUDEV_PATH_LEN];
	char names[MAX_VMU_DEVICES][VMUDEV_NAME_LEN];
	char label[128], value[2 + VMUDEV_PATH_LEN];
	option_entry_t* options = NULL;
	int count = vmudev_list(paths, names, MAX_VMU_DEVICES);

	for (int i = 0; i < count; i++)
	{
		if (exclude && strcmp(paths[i], exclude) == 0)
			continue;

		if (!options)
			options = _initOptions(1);

		value[0] = cmd;
		value[1] = STORAGE_VMU;
		strcpy(value + 2, paths[i]);
		snprintf(label, sizeof(label), "%.60s %.47s", name, names[i]);
		list_append(options[0].opts, _newOption(label, value, 2 + strlen(paths[i])));
	}

	return options;
}

static void _addVmuCopyCommand(list_t* codes, const char* title, const char* name, char cmd, const char* exclude)
{
	code_entry_t* entry;
	option_entry_t* options = _createVmuOptions(name, cmd, exclude);

	if (!options)
		return;

	entry = _createCmdCode(PATCH_COMMAND, title, CMD_CODE_NULL);
	entry->options_count = 1;
	entry->options = options;
	list_append(codes, entry);
}

static void _addStorageCommand(list_t* codes, const char* title, const char* name, char cmd, int id)
{
	code_entry_t* entry = _createCmdCode(PATCH_COMMAND, title, CMD_CODE_NULL);

	entry->options_count = 1;
	entry->options = _createStorageOptions(name, cmd);
	entry->options[0].id = id;
	list_append(codes, entry);
}

// A save on a VMU or in a VMU image, by name (see vmu_card.h)
int vmu_file_read(const char* vmu_path, const char* name, uint8_t** data, size_t* len)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];

	return vmucard_read(vmu_path, name, raw, data, len);
}

int vmu_save_read(const save_entry_t* save, uint8_t** data, size_t* len)
{
	return vmu_file_read(save->path, save->dir_name, data, len);
}

// Same-size rewrite; a VMS CRC that was valid stays valid (see vmucard_overwrite)
int vmu_file_write(const char* vmu_path, const char* name, const uint8_t* data, size_t len)
{
	return vmucard_overwrite(vmu_path, name, data, len);
}

int vmu_save_write(const save_entry_t* save, const uint8_t* data, size_t len)
{
	return vmu_file_write(save->path, save->dir_name, data, len);
}

int vmu_save_delete(const save_entry_t* save)
{
	return vmucard_delete(save->path, save->dir_name);
}

// What a Saves DB entry can do: import it into any VMU plugged in, or extract the
// .VMI/.VMS to SD/PC (DC/SAVEDATA/, where "Import Saves to VMU" finds them)
static option_entry_t* _createDbSaveOptions(void)
{
	char label[128];
	char value[2] = {CMD_DOWNLOAD_USB, STORAGE_MASS};
	option_entry_t* options = _createVmuOptions("Import to", CMD_DOWNLOAD_HDD, NULL);

	if (!options)
		options = _initOptions(1);

	snprintf(label, sizeof(label), "Extract .VMI/.VMS to %s" DC_SAVES_PATH, USB_PATH);
	list_append(options[0].opts, _newOption(label, value, 2));

	value[1] = STORAGE_HOST;
	snprintf(label, sizeof(label), "Extract .VMI/.VMS to %s" DC_SAVES_PATH, HOST_PATH);
	list_append(options[0].opts, _newOption(label, value, 2));

	return options;
}

/*
 * The .VMS a .VMI describes is named after its resource field, and lives next
 * to the .VMI. Browsers and file systems do not agree on the case of either,
 * so try the resource name both ways, then the .VMI's own base name.
 */
static int read_vms(const char *vmi_path, const char *resource, uint8_t **buf, size_t *len)
{
	char path[256];
	const char *base = strrchr(vmi_path, '/') ? strrchr(vmi_path, '/') + 1 : vmi_path;
	int dirlen = (int)(base - vmi_path);
	int stemlen = (int)strlen(base) - 4;

	const char *tries[][2] = {
		{ resource, ".VMS" }, { resource, ".vms" },
		{ NULL, ".VMS" }, { NULL, ".vms" },
	};

	for (int i = 0; i < 4; i++)
	{
		if (tries[i][0])
			snprintf(path, sizeof(path), "%.*s%s%s", dirlen, vmi_path, tries[i][0], tries[i][1]);
		else
			snprintf(path, sizeof(path), "%.*s%s", dirlen + stemlen, vmi_path, tries[i][1]);

		if (read_buffer(path, buf, len) == 0)
			return 1;
	}

	LOG("Error: can't find '%s.VMS' next to '%s'", resource, vmi_path);
	return 0;
}

// Decode a .DCI, or a .VMI and its .VMS, into a directory entry and file data
static int read_dc_save_file(const char* path, vmu_dirent_t* ent, uint8_t** data, size_t* dlen)
{
	uint8_t *buf;
	size_t len;
	int r;

	if (read_buffer(path, &buf, &len) < 0)
		return 0;

	if (wildcard_match_icase(path, "*.DCI"))
	{
		r = dci_decode(buf, len, ent, data, dlen);
		free(buf);

		// keep the header offset only where it points inside the file
		ent->hdroff = vms_header_offset(ent) / VMU_BLOCK_SIZE;
		return (r == 0);
	}

	vmi_info_t info;
	r = vmi_decode(buf, len, ent, &info);
	free(buf);

	if (r < 0 || !read_vms(path, info.resource, data, dlen))
		return 0;

	if (*dlen < info.filesize)
	{
		LOG("Error: the VMS holds %d bytes, the VMI expects %d", (int) *dlen, (int) info.filesize);
		free(*data);
		return 0;
	}

	*dlen = info.filesize;
	return 1;
}

// A .DCI, or a .VMI and its .VMS: raw directory entry and file data (malloc'd)
int dc_save_file_read(const char* path, uint8_t dirent[32], uint8_t** data, size_t* len)
{
	vmu_dirent_t ent;

	if (!read_dc_save_file(path, &ent, data, len))
		return 0;

	vmu_dirent_encode(&ent, dirent);
	return 1;
}

// The save's first icon frame as 32x32 RGBA (malloc'd), or NULL
uint8_t* get_vmu_save_icon(const save_entry_t* save)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t *data, *icon = NULL;
	vmu_dirent_t ent;
	vms_header_t vms;
	size_t len;

	if (!vmucard_read(save->path, save->dir_name, raw, &data, &len))
		return NULL;

	vmu_dirent_decode(raw, &ent);
	if (vms_parse(data, len, vms_header_offset(&ent), &vms) == 0)
		icon = vms_icon_rgba(&vms, 0);

	// ICONDATA_VMS has no VMS header, just the card's own icons
	if (!icon && strcmp(ent.filename, "ICONDATA_VMS") == 0 && (icon = icondata_rgba(data, len, 1)) == NULL)
		icon = icondata_rgba(data, len, 0);

	free(data);
	return icon;
}

static void _walk_dir_list(const char* startdir, const char* inputdir, const char* mask, list_t* list)
{
	// a VMU or a VMU image: its save files, read through the VMU layer
	if (vmucard_is_device(inputdir) || vmucard_is_image_name(inputdir))
	{
		vmudev_file_t* files;
		vmu_dirent_t ent;
		int count;

		if (!vmucard_scan(inputdir, &files, &count))
			return;

		for (int i = 0; i < count; i++)
		{
			vmu_dirent_decode(files[i].dirent, &ent);
			if (wildcard_match_icase(ent.filename, mask))
				list_append(list, strdup(ent.filename));
		}

		free(files);
		return;
	}

	char fullname[256];	
	struct dirent *dirp;
	int len = strlen(startdir);
	DIR *dp = opendir(inputdir);

	if (!dp) {
		LOG("Failed to open input directory: '%s'", inputdir);
		return;
	}

	while ((dirp = readdir(dp)) != NULL)
	{
		if ((strcmp(dirp->d_name, ".")  == 0) || (strcmp(dirp->d_name, "..") == 0) ||
			(strcmp(dirp->d_name, "ICON0.PNG") == 0) || (strcmp(dirp->d_name, "PARAM.SFO") == 0) || (strcmp(dirp->d_name,"PIC1.PNG") == 0) ||
			(strcmp(dirp->d_name, "icon.sys") == 0) || (strcmp(dirp->d_name, "SND0.AT3") == 0))
			continue;

		snprintf(fullname, sizeof(fullname), "%s%s", inputdir, dirp->d_name);

		if (wildcard_match_icase(dirp->d_name, mask))
		{
			//LOG("Adding file '%s'", fullname+len);
			list_append(list, strdup(fullname+len));
		}
	}
	closedir(dp);
}

static option_entry_t* _getFileOptions(const char* save_path, const char* mask, uint8_t is_cmd)
{
	char *filename;
	list_t* file_list;
	list_node_t* node;
	option_value_t* optval;
	option_entry_t* opt;

	LOG("Loading filenames {%s} from '%s'...", mask, save_path);

	file_list = list_alloc();
	_walk_dir_list(save_path, save_path, mask, file_list);

	if (!list_count(file_list))
	{
		is_cmd = 0;
		asprintf(&filename, CHAR_ICON_WARN " --- %s%s --- " CHAR_ICON_WARN, save_path, mask);
		list_append(file_list, filename);
	}

	opt = _initOptions(1);

	for (node = list_head(file_list); (filename = list_get(node)); node = list_next(node))
	{
		LOG("Adding '%s' (%s)", filename, mask);

		optval = malloc(sizeof(option_value_t));
		optval->name = filename;

		if (is_cmd)
			asprintf(&optval->value, "%c", is_cmd);
		else
			asprintf(&optval->value, "%s", mask);

		list_append(opt->opts, optval);
	}

	list_free(file_list);

	return opt;
}

option_entry_t* get_file_entries(const char* path, const char* mask)
{
	return _getFileOptions(path, mask, CMD_CODE_NULL);
}

/*
 * Function:		ReadLocalCodes()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Reads an entire NCL file into an array of code_entry
 * Arguments:
 *	path:			Path to ncl
 *	_count_count:	Pointer to int (set to the number of codes within the ncl)
 * Return:			Returns an array of code_entry, null if failed to load
 */
// Cheat patches from APOLLO_DATA_PATH "<title_id>.savepatch"
static void _addPatchCodes(save_entry_t * save)
{
	code_entry_t * code;
	char filePath[256];
	char * buffer = NULL;

	snprintf(filePath, sizeof(filePath), APOLLO_DATA_PATH "%s.savepatch", save->title_id);
	if ((buffer = readTextFile(filePath, NULL)) == NULL)
		return;

	code = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Cheats " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);	
	list_append(save->codes, code);

	code = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Raw Patch File", CMD_VIEW_RAW_PATCH);
	list_append(save->codes, code);

	LOG("Loading BSD codes '%s'...", filePath);
	apollo_load_code_list(buffer, save->codes, &get_file_entries, save->path);
	free (buffer);
}

// .DCI / .VMI saves in <storage>DC/SAVEDATA/, listed as import commands
static void add_vmu_import_saves(list_t* list, const char* path)
{
	code_entry_t * cmd;
	DIR *d;
	struct dirent *dir;
	char dcPath[256];

	snprintf(dcPath, sizeof(dcPath), "%s%s", path, DC_SAVES_PATH);
	if ((d = opendir(dcPath)) == NULL)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (!endsWith(dir->d_name, ".DCI") && !endsWith(dir->d_name, ".VMI"))
			continue;

		snprintf(dcPath, sizeof(dcPath), CHAR_ICON_COPY " Import %s", dir->d_name);
		cmd = _createCmdCode(PATCH_COMMAND, dcPath, CMD_IMP_SAVE_MC);
		asprintf(&cmd->file, "%s%s%s", path, DC_SAVES_PATH, dir->d_name);
		list_append(list, cmd);

		LOG("[%s] name '%s'", cmd->file, cmd->name + 2);
	}

	closedir(d);
}

int ReadVmuCodes(save_entry_t * save)
{
	code_entry_t * cmd;

	// "Import Saves to VMU": what's on the storage device
	if (save->type == FILE_TYPE_MENU)
	{
		save->codes = list_alloc();
		add_vmu_import_saves(save->codes, save->path);

		if (!list_count(save->codes))
		{
			list_free(save->codes);
			save->codes = NULL;
			return 0;
		}

		list_bubbleSort(save->codes, &sortCodeList_Compare);
		return list_count(save->codes);
	}

	save->codes = list_alloc();

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Apply Changes", CMD_RESIGN_SAVE);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Save Details", CMD_VIEW_DETAILS);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Save Game Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(save->codes, cmd);

	_addVmuCopyCommand(save->codes, CHAR_ICON_COPY " Copy save game to another VMU", "Copy to", CMD_COPY_SAVE_HDD, save->path);
	_addStorageCommand(save->codes, CHAR_ICON_COPY " Export save game to .DCI format", "Export .DCI to", CMD_COPY_SAVE_USB, FILE_TYPE_DCI);
	_addStorageCommand(save->codes, CHAR_ICON_COPY " Export save game to .VMI/.VMS format", "Export .VMI/.VMS to", CMD_COPY_SAVE_USB, FILE_TYPE_VMI);

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Save Game Tools " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(save->codes, cmd);

	// a VMU save is a single file: the only option is the save itself
	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Hex Edit save game file", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _initOptions(1);
	list_append(cmd->options[0].opts, _newOption(save->dir_name, (char[]){CMD_HEX_EDIT_FILE}, 1));
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_WARN " Delete Save Game", CMD_DELETE_SAVE);
	list_append(save->codes, cmd);

	_addPatchCodes(save);
	LOG("Loaded %ld codes", list_count(save->codes));

	return list_count(save->codes);
}

/*
 * Function:		ReadOnlineSaves()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Downloads an entire NCL file into an array of code_entry
 * Arguments:
 *	filename:		File name ncl
 *	_count_count:	Pointer to int (set to the number of codes within the ncl)
 * Return:			Returns an array of code_entry, null if failed to load
 */
int ReadOnlineSaves(save_entry_t * game)
{
	code_entry_t* item;
	char path[256];
	if (game->path[0] == '/')
		snprintf(path, sizeof(path), "%ssaves.txt", game->path);
	else
	{
		snprintf(path, sizeof(path), APOLLO_LOCAL_CACHE "%s.txt", game->title_id);

		if (file_exists(path) == SUCCESS && strcmp(apollo_config.save_db, ONLINE_URL) == 0)
		{
			struct stat stats;
			stat(path, &stats);
			// re-download if file is +1 day old
			if ((stats.st_mtime + ONLINE_CACHE_TIMEOUT) < time(NULL))
				http_download(game->path, "saves.txt", path, 0);
		}
		else if (!http_download(game->path, "saves.txt", path, 0))
			return 0;
	}

	long fsize;
	char *data = readTextFile(path, &fsize);
	if (!data)
		return 0;
	
	char *ptr = data;
	char *end = data + fsize;

	game->codes = list_alloc();

	while (ptr < end && *ptr)
	{
		const char* content = ptr;

		while (ptr < end && *ptr != '\n' && *ptr != '\r')
		{
			ptr++;
		}
		*ptr++ = 0;

		// "v12719.zip=(CRAZYTAXI_DC) description": zip names vary in length
		const char* eq = strchr(content, '=');
		if (eq && eq > content)
		{
			snprintf(path, sizeof(path), CHAR_ICON_ZIP " %s", eq + 1);
			item = _createCmdCode(PATCH_COMMAND, path, CMD_CODE_NULL);
			asprintf(&item->file, "%.*s", (int)(eq - content), content);

			item->options_count = 1;
			item->options = _createDbSaveOptions();
			list_append(game->codes, item);

			LOG("[%s%s] %s", game->path, item->file, item->name + 1);
		}

		if (ptr < end && *ptr == '\r')
		{
			ptr++;
		}
		if (ptr < end && *ptr == '\n')
		{
			ptr++;
		}
	}

	free(data);
	LOG("Loaded %d saves", list_count(game->codes));

	return (list_count(game->codes));
}

list_t * ReadBackupList(const char* userPath)
{
	save_entry_t * item;
	list_t *list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_ZIP, CHAR_ICON_ZIP " Extract Archives (Zip, 7z)");
	item->path = strdup(menu_options[3].options[apollo_config.storage]);
	item->type = FILE_TYPE_ZIP;
	list_append(list, item);
/*
	item = _createSaveEntry(0, CHAR_ICON_NET " Network Tools");
	item->path = strdup(MC0_PATH);
	item->type = FILE_TYPE_NET;
	list_append(list, item);
*/
	return list;
}

int ReadBackupCodes(save_entry_t * bup)
{
	code_entry_t * cmd;
	char tmp[256];

	switch(bup->type)
	{
	case FILE_TYPE_ZIP:
		break;

	case FILE_TYPE_NET:
		bup->codes = list_alloc();
//		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " URL link Downloader (http, https, ftp, ftps)", CMD_URL_DOWNLOAD);
//		list_append(bup->codes, cmd);
		cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " Local Web Server (full system access)", CMD_NET_WEBSERVER);
		list_append(bup->codes, cmd);
		return list_count(bup->codes);

	default:
		return 0;
	}

	bup->codes = list_alloc();

	LOG("Loading files from '%s'...", bup->path);

	DIR *d;
	struct dirent *dir;
	d = opendir(bup->path);

	if (d)
	{
		while ((dir = readdir(d)) != NULL)
		{
			if (!endsWith(dir->d_name, ".RAR") && !endsWith(dir->d_name, ".ZIP") && !endsWith(dir->d_name, ".7Z"))
				continue;

			snprintf(tmp, sizeof(tmp), CHAR_ICON_ZIP " Extract %s%s", bup->path, dir->d_name);
			cmd = _createCmdCode(PATCH_COMMAND, tmp, CMD_EXTRACT_ARCHIVE);
			asprintf(&cmd->file, "%s%s", bup->path, dir->d_name);

			LOG("[%s] name '%s'", cmd->file, cmd->name +2);
			list_append(bup->codes, cmd);
		}
		closedir(d);
	}

	if (!list_count(bup->codes))
	{
		list_free(bup->codes);
		bup->codes = NULL;
		return 0;
	}

	LOG("%ld items loaded", list_count(bup->codes));

	return list_count(bup->codes);
}

/*
 * Function:		UnloadGameList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Free entire array of game_entry
 * Arguments:
 *	list:			Array of game_entry to free
 *	count:			number of game entries
 * Return:			void
 */
void UnloadGameList(list_t * list)
{
	list_node_t *node, *nc, *no;
	save_entry_t *item;
	code_entry_t *code;
	option_value_t* optval;

	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		if (item->name)
		{
			free(item->name);
			item->name = NULL;
		}

		if (item->path)
		{
			free(item->path);
			item->path = NULL;
		}

		if (item->dir_name)
		{
			free(item->dir_name);
			item->dir_name = NULL;
		}

		if (item->title_id)
		{
			free(item->title_id);
			item->title_id = NULL;
		}

		if (item->icon)
		{
			free(item->icon);
			item->icon = NULL;
		}

		if (item->codes)
		{
			for (nc = list_head(item->codes); (code = list_get(nc)); nc = list_next(nc))
			{
				if (code->codes)
				{
					free (code->codes);
					code->codes = NULL;
				}
				if (code->name)
				{
					free (code->name);
					code->name = NULL;
				}
				if (code->options && code->options_count > 0)
				{
					for (int z = 0; z < code->options_count; z++)
					{
						for (no = list_head(code->options[z].opts); (optval = list_get(no)); no = list_next(no))
						{
							if (optval->name)
								free(optval->name);
							if (optval->value)
								free(optval->value);

							free(optval);
						}
						list_free(code->options[z].opts);

						if (code->options[z].line)
							free(code->options[z].line);
					}
					
					free (code->options);
				}

				free(code);
			}
			
			list_free(item->codes);
			item->codes = NULL;
		}

		free(item);
	}

	list_free(list);
	
	LOG("UnloadGameList() :: Successfully unloaded game list");
}

int sortCodeList_Compare(const void* a, const void* b)
{
	return strcasecmp(((code_entry_t*) a)->name, ((code_entry_t*) b)->name);
}

/*
 * Function:		qsortSaveList_Compare()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Compares two game_entry for QuickSort
 * Arguments:
 *	a:				First code
 *	b:				Second code
 * Return:			1 if greater, -1 if less, or 0 if equal
 */
int sortSaveList_Compare(const void* a, const void* b)
{
	return strcasecmp(((save_entry_t*) a)->name, ((save_entry_t*) b)->name);
}

int sortSaveList_Compare_TitleID(const void* a, const void* b)
{
	char* ta = ((save_entry_t*) a)->title_id;
	char* tb = ((save_entry_t*) b)->title_id;

	if (!ta)
		return (-1);

	if (!tb)
		return (1);

	return strcasecmp(ta, tb);
}

/*
 * External Saves: on the SD card and the PC (dcload)
 *   <root>DC/SAVEDATA/   .DCI and .VMI (+ .VMS) saves, to import into a VMU
 *   <root>DC/VMU/        VMU images (.BIN, .VMU, .DCM), opened like a VMU
 */
static const char* ext_roots[] = {USB_PATH, HOST_PATH, NULL};

// Title and VMU file name of a .DCI / .VMI. A .VMI carries its own description,
// so only the small .VMI is read; a .DCI holds the save, header included.
static int dc_ext_info(const char* path, int type, char* title, size_t tsize, char* vmu_name, size_t nsize)
{
	uint8_t *buf, *data;
	size_t len, dlen;
	vmu_dirent_t ent;
	vms_header_t vms;
	vmi_info_t info;
	int ok = 0;

	if (read_buffer(path, &buf, &len) < 0)
		return 0;

	if (type == FILE_TYPE_VMI && vmi_decode(buf, len, &ent, &info) == 0)
	{
		dc_copy_text(title, tsize, info.description, sizeof(info.description) - 1);
		ok = 1;
	}
	else if (type == FILE_TYPE_DCI && dci_decode(buf, len, &ent, &data, &dlen) == 0)
	{
		title[0] = 0;
		if (vms_parse(data, dlen, vms_header_offset(&ent), &vms) == 0)
		{
			dc_copy_text(title, tsize, vms.desc_dc, 32);
			if (!title[0])
				dc_copy_text(title, tsize, vms.desc_vms, 16);
		}
		free(data);
		ok = 1;
	}
	free(buf);

	if (ok)
		snprintf(vmu_name, nsize, "%s", ent.filename);

	return ok;
}

static void read_dc_ext_saves(const char* root, list_t* list)
{
	char path[256], title[DC_TEXT_SIZE(32)], vmu_name[VMU_NAME_LEN + 1];
	struct dirent* e;
	save_entry_t* item;
	DIR* d;

	snprintf(path, sizeof(path), "%s" DC_SAVES_PATH, root);
	if ((d = opendir(path)) == NULL)
		return;

	while ((e = readdir(d)) != NULL)
	{
		int type = endsWith(e->d_name, ".DCI") ? FILE_TYPE_DCI : endsWith(e->d_name, ".VMI") ? FILE_TYPE_VMI : 0;

		if (!type)
			continue;

		snprintf(path, sizeof(path), "%s" DC_SAVES_PATH "%s", root, e->d_name);
		if (!dc_ext_info(path, type, title, sizeof(title), vmu_name, sizeof(vmu_name)))
		{
			LOG("Skipping '%s': not a valid Dreamcast save", path);
			continue;
		}

		item = _createSaveEntry(SAVE_FLAG_DC | SAVE_FLAG_PACKED, title[0] ? title : e->d_name);
		item->type = type;
		item->path = strdup(path);
		item->dir_name = strdup(e->d_name);
		item->title_id = strdup(vmu_name);

		LOG("[%s] F(%X) %s '%s'", item->title_id, item->flags, item->path, item->name);
		list_append(list, item);
	}

	closedir(d);
}

static void read_dc_vmu_images(const char* root, list_t* list)
{
	char path[256];
	struct dirent* e;
	save_entry_t* item;
	uint64_t size;
	DIR* d;

	snprintf(path, sizeof(path), "%s" DC_VMU_PATH, root);
	if ((d = opendir(path)) == NULL)
		return;

	while ((e = readdir(d)) != NULL)
	{
		if (!vmucard_is_image_name(e->d_name))
			continue;

		snprintf(path, sizeof(path), "%s" DC_VMU_PATH "%s", root, e->d_name);
		if (get_file_size(path, &size) != SUCCESS || size != VMUDEV_IMAGE_SIZE)
			continue;

		item = _createSaveEntry(SAVE_FLAG_DC | SAVE_FLAG_VMC, e->d_name);
		item->type = FILE_TYPE_VMC;
		item->path = strdup(path);
		item->title_id = strdup(endsWith(e->d_name, ".DCM") ? "DCM" : "VMU image");

		LOG("[%s] VMU image %s", item->title_id, item->path);
		list_append(list, item);
	}

	closedir(d);
}

list_t * ReadUsbList(const char* userPath)
{
	save_entry_t *item;
	list_t *list;

	if (!userPath[0])
		return NULL;

	list = list_alloc();

	// Bulk management: import external saves into a VMU
	item = _createSaveEntry(SAVE_FLAG_DC, CHAR_ICON_COPY " Bulk Save Management");
	item->type = FILE_TYPE_MENU;
	item->codes = list_alloc();
	item->path = strdup(userPath);
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	_addVmuCopyCommand(item->codes, CHAR_ICON_COPY " Copy selected Saves to a VMU", "Copy to", CMD_COPY_SAVES_HDD, NULL);
	_addVmuCopyCommand(item->codes, CHAR_ICON_COPY " Copy all Saves to a VMU", "Copy to", CMD_COPY_ALL_SAVES_HDD, NULL);

//	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_NET " Start local Web Server", CMD_SAVE_WEBSERVER);
//	list_append(item->codes, cmd);
	if (list_count(item->codes))
		list_append(list, item);
	else
	{
		list_free(item->codes);
		free(item->path);
		free(item->dir_name);
		free(item->name);
		free(item);
	}

	for (int i = 0; ext_roots[i]; i++)
	{
		if (dir_exists(ext_roots[i]) != SUCCESS)
			continue;

		read_dc_ext_saves(ext_roots[i], list);
		read_dc_vmu_images(ext_roots[i], list);
	}

	return list;
}

int ReadExtCodes(save_entry_t * save)
{
	code_entry_t * cmd;

	save->codes = list_alloc();

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Save Details", CMD_VIEW_DETAILS);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Save Game Import " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(save->codes, cmd);

	_addVmuCopyCommand(save->codes, CHAR_ICON_COPY " Copy save game to a VMU", "Copy to", CMD_COPY_SAVE_HDD, NULL);

	LOG("Loaded %ld codes", list_count(save->codes));
	return list_count(save->codes);
}

// External .DCI / .VMI save: its first icon frame as 32x32 RGBA (malloc'd), or NULL
uint8_t* get_dc_file_icon(const char* path)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t *data, *icon = NULL;
	vmu_dirent_t ent;
	vms_header_t vms;
	size_t len;

	if (!dc_save_file_read(path, raw, &data, &len))
		return NULL;

	vmu_dirent_decode(raw, &ent);
	if (vms_parse(data, len, vms_header_offset(&ent), &vms) == 0)
		icon = vms_icon_rgba(&vms, 0);

	free(data);
	return icon;
}

list_t * ReadUserList(const char* userPath)
{
	vmu_dirent_t ent;
	vmudev_file_t *files;
	int count;
	char title[DC_TEXT_SIZE(32)], patch_id[32];
	char *titleid_db;
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;

	// a VMU plugged into a controller, or a VMU image file (see vmu_card.h)
	int device = vmucard_is_device(userPath);

	if (!vmucard_open(userPath) || !vmucard_scan(userPath, &files, &count))
	{
		LOG("No VMU found at '%s'", userPath);
		return NULL;
	}

	LOG("Reading %d files from %s...", count, userPath);
	list = list_alloc();

	// Bulk management: copy to another VMU, export to storage
	item = _createSaveEntry(SAVE_FLAG_DC, CHAR_ICON_COPY " Bulk Save Management");
	item->type = FILE_TYPE_MENU;
	item->codes = list_alloc();
	item->path = strdup(userPath);
	if (device)
		asprintf(&item->title_id, "VMU %.2s", userPath + strlen(VMU_PATH));
	else
		item->title_id = strdup(vmucard_is_dcm(userPath) ? "DCM" : "VMU image");
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	_addVmuCopyCommand(item->codes, device ? CHAR_ICON_COPY " Copy selected Saves to another VMU" : CHAR_ICON_COPY " Copy selected Saves to a VMU", "Copy to", CMD_COPY_SAVES_HDD, userPath);
	_addVmuCopyCommand(item->codes, device ? CHAR_ICON_COPY " Copy all Saves to another VMU" : CHAR_ICON_COPY " Copy all Saves to a VMU", "Copy to", CMD_COPY_ALL_SAVES_HDD, userPath);
	_addStorageCommand(item->codes, CHAR_ICON_COPY " Export selected Saves to .DCI", "Export .DCI to", CMD_COPY_SAVES_USB, FILE_TYPE_DCI);
	_addStorageCommand(item->codes, CHAR_ICON_COPY " Export all Saves to .DCI", "Export .DCI to", CMD_COPY_ALL_SAVES_USB, FILE_TYPE_DCI);

	if (device)
	{
		cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Memory Card Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
		list_append(item->codes, cmd);
		_addStorageCommand(item->codes, CHAR_ICON_COPY " Dump full VMU image (.BIN)", "Dump VMU image to", CMD_EXP_VMU_IMAGE, 0);
	}
	list_append(list, item);

	// Import .DCI / .VMI saves from each storage device; dir_name is the target VMU
	const char* import_src[] = {USB_PATH, HOST_PATH};
	for (int i = 0; i < 2; i++)
	{
		item = _createSaveEntry(SAVE_FLAG_DC, device ? CHAR_ICON_COPY " Import Saves to VMU" : CHAR_ICON_COPY " Import Saves to VMU image");
		item->type = FILE_TYPE_MENU;
		item->path = strdup(import_src[i]);
		item->dir_name = strdup(userPath);
		asprintf(&item->title_id, "%s", import_src[i]);
		list_append(list, item);
	}

	titleid_db = readTextFile(APOLLO_DATA_PATH DC_TITLEID_FILE, NULL);

	for (int i = 0; i < count; i++)
	{
		vmu_dirent_decode(files[i].dirent, &ent);

		if (!files[i].has_header || !dc_header_title(files[i].header, title, sizeof(title)))
			snprintf(title, sizeof(title), "%s", ent.filename);

		dc_get_patch_id(titleid_db, ent.filename, patch_id, sizeof(patch_id));

		item = _createSaveEntry(SAVE_FLAG_DC | SAVE_FLAG_MEMCARD | (device ? 0 : SAVE_FLAG_VMC), title);
		item->type = FILE_TYPE_VMS;
		item->path = strdup(userPath);
		item->dir_name = strdup(ent.filename);
		item->title_id = strdup(patch_id);

		if (ent.copyprotect == VMU_COPY_PROTECTED)
			item->flags |= SAVE_FLAG_LOCKED;

		LOG("[%s] F(%X) %d blocks, name '%s'", item->dir_name, item->flags, ent.filesize, item->name);
		list_append(list, item);
	}

	free(titleid_db);
	free(files);

	return list;
}

/*
 * Function:		ReadOnlineList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Downloads the entire gamelist file into a game_entry array
 * Arguments:
 *	gmc:			Set as the number of games read
 * Return:			Pointer to array of game_entry, null if failed
 */
static void _ReadOnlineListEx(const char* urlPath, uint16_t flag, list_t *list)
{
	save_entry_t *item;
	char path[256 + sizeof("games.txt")];

	// a local Saves DB (disc, SD, PC) is read in place; an online one goes through the cache
	if (urlPath[0] == '/')
		snprintf(path, sizeof(path), "%sgames.txt", urlPath);
	else
	{
		snprintf(path, sizeof(path), APOLLO_LOCAL_CACHE "%04X_games.txt", flag);

		if (file_exists(path) == SUCCESS && strcmp(apollo_config.save_db, ONLINE_URL) == 0)
		{
			struct stat stats;
			stat(path, &stats);
			// re-download if file is +1 day old
			if ((stats.st_mtime + ONLINE_CACHE_TIMEOUT) < time(NULL))
				http_download(urlPath, "games.txt", path, 0);
		}
		else if (!http_download(urlPath, "games.txt", path, 0))
			return;
	}

	long fsize;
	char *data = readTextFile(path, &fsize);
	if (!data)
		return;

	char *ptr = data;
	char *end = data + fsize;

	while (ptr < end && *ptr)
	{
		char *tmp, *content = ptr;

		while (ptr < end && *ptr != '\n' && *ptr != '\r')
		{
			ptr++;
		}
		*ptr++ = 0;

		if ((tmp = strchr(content, '=')) != NULL)
		{
			*tmp++ = 0;
			item = _createSaveEntry(flag | SAVE_FLAG_ONLINE, tmp);
			item->title_id = strdup(content);
			asprintf(&item->path, "%s%s/", urlPath, item->title_id);

			LOG("+ [%s] %s", item->title_id, item->name);
			list_append(list, item);
		}

		if (ptr < end && *ptr == '\r')
		{
			ptr++;
		}
		if (ptr < end && *ptr == '\n')
		{
			ptr++;
		}
	}

	if (data) free(data);
}

list_t * ReadOnlineList(const char* urlPath)
{
	char url[256];
	list_t *list = list_alloc();

	// DC save-games (Zip VMI/VMS): a local DB path already points at the DC folder
	snprintf(url, sizeof(url), (urlPath[0] == '/') ? "%s" : "%sDC/", urlPath);
	_ReadOnlineListEx(url, SAVE_FLAG_DC, list);

	if (!list_count(list))
	{
		list_free(list);
		return NULL;
	}

	return list;
}

int get_save_details(const save_entry_t* save, char **details)
{
	// a save on a VMU / in a VMU image, or an external .DCI / .VMI file
	if (save->type == FILE_TYPE_VMS || save->type == FILE_TYPE_DCI || save->type == FILE_TYPE_VMI)
	{
		uint8_t raw[VMUDEV_DIRENT_SIZE];
		uint8_t *data;
		size_t len;
		vmu_dirent_t ent;
		vms_header_t vms;
		char desc_dc[DC_TEXT_SIZE(32)] = "", desc_vms[DC_TEXT_SIZE(16)] = "", app_id[DC_TEXT_SIZE(16)] = "", crc[32] = "n/a";
		int icons = 0;

		if (save->type == FILE_TYPE_VMS ? !vmucard_read(save->path, save->dir_name, raw, &data, &len) :
			!dc_save_file_read(save->path, raw, &data, &len))
			return 0;

		vmu_dirent_decode(raw, &ent);
		if (vms_parse(data, len, vms_header_offset(&ent), &vms) == 0)
		{
			int calc = vms_calc_crc(&vms);

			dc_copy_text(desc_dc, sizeof(desc_dc), vms.desc_dc, 32);
			dc_copy_text(desc_vms, sizeof(desc_vms), vms.desc_vms, 16);
			dc_copy_text(app_id, sizeof(app_id), vms.app_id, 16);
			icons = vms.icons;
			snprintf(crc, sizeof(crc), "%04X (%s)", vms.crc, (calc == vms.crc) ? "OK" : "mismatch");
		}
		free(data);

		asprintf(details, "%s%s\n----- Dreamcast VMU Save -----\n"
			"File: %s\n"
			"Title: %s\n"
			"VMU Desc: %s\n"
			"Created by: %s\n"
			"Type: %s\n"
			"Size: %d blocks\n"
			"Date: %02d%02d-%02d-%02d %02d:%02d:%02d\n"
			"Icons: %d\n"
			"CRC: %s\n"
			"Copy Protected: %s\n"
			"Patch ID: %s\n",
			save->path, (save->type == FILE_TYPE_VMS) ? save->dir_name : "",
			ent.filename,
			desc_dc,
			desc_vms,
			app_id,
			(ent.filetype == VMU_FILE_GAME) ? "Mini-game" : "Data",
			ent.filesize,
			vmu_bcd_to_dec(ent.timestamp.cent), vmu_bcd_to_dec(ent.timestamp.year), vmu_bcd_to_dec(ent.timestamp.month),
			vmu_bcd_to_dec(ent.timestamp.day), vmu_bcd_to_dec(ent.timestamp.hour), vmu_bcd_to_dec(ent.timestamp.min),
			vmu_bcd_to_dec(ent.timestamp.sec),
			icons,
			crc,
			(ent.copyprotect == VMU_COPY_PROTECTED) ? "Yes" : "No",
			save->title_id);

		return 1;
	}

	if (save->flags & SAVE_FLAG_PACKED)
	{
		asprintf(details, "%s\n----- Packed PS2 Save -----\n"
			"File: %s\n"
			"Title ID: %s\n"
			"Folder: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name);

		return 1;
	}

	if(save->type == FILE_TYPE_VMC)
	{
		asprintf(details, "%s\n----- Virtual Mem Card -----\n"
			"File: %s\n"
			"Folder: %s\n",
			save->path,
			save->title_id,
			save->dir_name);

		return 1;
	}

	if (save->flags & SAVE_FLAG_PS2)
	{
		asprintf(details, "%s\n----- PS2 Save -----\n"
			"Game: %s\n"
			"Title ID: %s\n"
			"Folder: %s\n"
			"Icon: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name,
			save->icon);

		return 1;
	}

	asprintf(details, "%s\n----- Save -----\n"
		"Title: %s\n"
		"Title ID: %s\n"
		"Dir Name: %s\n",
		save->path, save->name,
		save->title_id,
		save->dir_name);

	return 1;
}
