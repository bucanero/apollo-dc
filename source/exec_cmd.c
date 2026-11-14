#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <polarssl/md5.h>

#include "saves.h"
#include "menu.h"
#include "common.h"
#include "utils.h"
#include "dcvmu.h"
#include "dcsave.h"
#include "vmu_dev.h"
#include "vmu_card.h"

#include <ctype.h>
#include <fat/fs_fat.h>

static char host_buf[256];

static void sync_storage(const char* path);

static int _set_dest_path(char* path, int dest, const char* folder)
{
	switch (dest)
	{
	case STORAGE_MC1:
		sprintf(path, "%s%s", MC1_PATH, folder);
		break;

	case STORAGE_MC0:
		sprintf(path, "%s%s", MC0_PATH, folder);
		break;

	case STORAGE_MASS:
		sprintf(path, "%s%s", USB_PATH, folder);
		break;

	case STORAGE_HOST:
		sprintf(path, "%s%s", HOST_PATH, folder);
		break;

	default:
		path[0] = 0;
		return 0;
	}

	return 1;
}

// create the export folder; an unmounted SD card gets its own message
static int check_export_dir(const char* path)
{
	if (mkdirs(path) == SUCCESS)
		return 1;

	if (strncmp(path, USB_PATH, strlen(USB_PATH)) == 0 && dir_exists(USB_PATH) != SUCCESS)
		show_message("Error! No SD card found\n\nUse a FAT formatted card in an SD card adapter");
	else
		show_message("Error! Export folder is not available:\n%s", path);

	return 0;
}

static void downloadSave(const save_entry_t* entry, const char* file, int dst)
{
	char path[256], zip[256];

	_set_dest_path(path, dst, (entry->flags & SAVE_FLAG_DC) ? DC_SAVES_PATH : PS2_SAVES_PATH_USB);
	if (dst == STORAGE_MC0)
		snprintf(path, sizeof(path), PSP_SAVES_PATH_HDD);

	if (!check_export_dir(path))
		return;

	// local Saves DB (disc, SD, PC): unzip straight from it
	if (entry->path[0] == '/')
	{
		snprintf(zip, sizeof(zip), "%s%s", entry->path, file);
		if (extract_zip(zip, path))
			show_message("Save game successfully extracted to:\n%s", path);
		else
			show_message("Error extracting save game:\n%s", zip);

		sync_storage(path);
		return;
	}

	if (!http_download(entry->path, file, APOLLO_LOCAL_CACHE "tmpsave.zip", 1))
	{
		show_message("Error downloading save game from:\n%s%s", entry->path, file);
		return;
	}

	if (extract_zip(APOLLO_LOCAL_CACHE "tmpsave.zip", path))
		show_message("Save game successfully downloaded to:\n%s", path);
	else
		show_message("Error extracting save game!");

	unlink_secure(APOLLO_LOCAL_CACHE "tmpsave.zip");
}

static void extractArchive(const char* file_path)
{
	int ret = 0;
	char exp_path[256];

	strncpy(exp_path, file_path, sizeof(exp_path)-1);
	*strrchr(exp_path, '.') = 0;

	switch (strrchr(file_path, '.')[1])
	{
	case 'z':
	case 'Z':
		/* ZIP */
		strcat(exp_path, "/");
		ret = extract_zip(file_path, exp_path);
		break;

	case 'r':
	case 'R':
		/* RAR */
//		ret = extract_rar(file_path, exp_path);
		break;

	case '7':
		/* 7-Zip */
//		ret = extract_7zip(file_path, exp_path);
		break;

	default:
		break;
	}

	if (ret)
		show_message("All files extracted to:\n%s", exp_path);
	else
		show_message("Error: %s couldn't be extracted", file_path);
}

/*
static int webReqHandler(dWebRequest_t* req, dWebResponse_t* res, void* list)
{
	list_node_t *node;
	save_entry_t *item;

	// http://ps3-ip:8080/
	if (strcmp(req->resource, "/") == 0)
	{
		uint64_t hash[2];
		md5_context ctx;

		md5_starts(&ctx);
		for (node = list_head(list); (item = list_get(node)); node = list_next(node))
			md5_update(&ctx, (uint8_t*) item->name, strlen(item->name));

		md5_finish(&ctx, (uint8_t*) hash);
		asprintf(&res->data, APOLLO_LOCAL_CACHE "web%016llx%016llx.html", hash[0], hash[1]);

		if (file_exists(res->data) == SUCCESS)
			return 1;

		FILE* f = fopen(res->data, "w");
		if (!f)
			return 0;

		fprintf(f, "<html><head><meta charset=\"UTF-8\"><style>h1, h2 { font-family: arial; } img { display: none; } table { border-collapse: collapse; margin: 25px 0; font-size: 0.9em; font-family: sans-serif; min-width: 400px; box-shadow: 0 0 20px rgba(0, 0, 0, 0.15); } table thead tr { background-color: #009879; color: #ffffff; text-align: left; } table th, td { padding: 12px 15px; } table tbody tr { border-bottom: 1px solid #dddddd; } table tbody tr:nth-of-type(even) { background-color: #f3f3f3; } table tbody tr:last-of-type { border-bottom: 2px solid #009879; }</style>");
		fprintf(f, "<script language=\"javascript\">function show(sid,src){var im=document.getElementById('img'+sid);im.src=src;im.style.display='block';document.getElementById('btn'+sid).style.display='none';}</script>");
		fprintf(f, "<title>Apollo Save Tool</title></head><body><h1>.:: Apollo Save Tool</h1><h2>Index of %s</h2><table><thead><tr><th>Name</th><th>Icon</th><th>Title ID</th><th>Folder</th><th>Location</th></tr></thead><tbody>", selected_entry->path);

		int i = 0;
		for (node = list_head(list); (item = list_get(node)); node = list_next(node), i++)
		{
			if (item->type == FILE_TYPE_MENU || !(item->flags & (SAVE_FLAG_PS1|SAVE_FLAG_DC)))
				continue;

			fprintf(f, "<tr><td><a href=\"/zip/%08d/%s_%s.zip\">%s</a></td>", i, item->title_id, item->dir_name, item->name);
			fprintf(f, "<td><button type=\"button\" id=\"btn%d\" onclick=\"show(%d,'/icon/%08x/ICON0.PNG", i, i, i);
			fprintf(f, "')\">Show Icon</button><img id=\"img%d\" alt=\"%s\" height=\"80\"></td>", i, item->name);
			fprintf(f, "<td>%s</td>", item->title_id);
			fprintf(f, "<td>%s</td>", item->dir_name);
			fprintf(f, "<td>%.4s</td></tr>", item->path);
		}

		fprintf(f, "</tbody></table></body></html>");
		fclose(f);
		return 1;
	}

	// http://psp-ip:8080/PSP/games.txt
	if (wildcard_match(req->resource, "/PSP/games.txt"))
	{
		asprintf(&res->data, "%s%s", APOLLO_LOCAL_CACHE, "PSP_games.txt");

		FILE* f = fopen(res->data, "w");
		if (!f)
			return 0;

		for (node = list_head(list); (item = list_get(node)); node = list_next(node))
		{
			if (item->type == FILE_TYPE_MENU || !(item->flags & SAVE_FLAG_DC))
				continue;

			fprintf(f, "%s=%s\n", item->title_id, item->name);
		}

		fclose(f);
		return 1;
	}

	// http://psp-ip:8080/PSP/BLUS12345/saves.txt
	if (wildcard_match(req->resource, "/PSP/\?\?\?\?\?\?\?\?\?/saves.txt"))
	{
		asprintf(&res->data, "%sweb%.9s_saves.txt", APOLLO_LOCAL_CACHE, req->resource + 5);

		FILE* f = fopen(res->data, "w");
		if (!f)
			return 0;

		int i = 0;
		for (node = list_head(list); (item = list_get(node)); node = list_next(node), i++)
		{
			if (item->type == FILE_TYPE_MENU || !(item->flags & SAVE_FLAG_DC) || strncmp(item->title_id, req->resource + 5, 9))
				continue;

			fprintf(f, "%08d.zip=(%s) %s\n", i, item->dir_name, item->name);
		}

		fclose(f);
		return 1;
	}

	// http://ps3-ip:8080/zip/00000000/CUSA12345_DIR-NAME.zip
	// http://psp-ip:8080/PSP/BLUS12345/00000000.zip
	if (wildcard_match(req->resource, "/zip/\?\?\?\?\?\?\?\?/\?\?\?\?\?\?\?\?\?_*.zip") ||
		wildcard_match(req->resource, "/PSP/\?\?\?\?\?\?\?\?\?/*.zip"))
	{
		char *base, *path;
		int id = 0;

		sscanf(req->resource + (strncmp(req->resource, "/PSP", 4) == 0 ? 15 : 5), "%08d", &id);
		item = list_get_item(list, id);
		asprintf(&res->data, "%s%s_%s.zip", APOLLO_LOCAL_CACHE, item->title_id, item->dir_name);

		base = strdup(item->path);
		path = strdup(item->path);
		*strrchr(base, '/') = 0;
		*strrchr(base, '/') = 0;

		id = zip_directory(base, path, res->data);

		free(base);
		free(path);
		return id;
	}

	// http://psp-ip:8080/PSP/BLUS12345/ICON0.PNG
	if (wildcard_match(req->resource, "/PSP/\?\?\?\?\?\?\?\?\?/ICON0.PNG"))
	{
		for (node = list_head(list); (item = list_get(node)); node = list_next(node))
		{
			if (item->type == FILE_TYPE_MENU || !(item->flags & SAVE_FLAG_DC) || strncmp(item->title_id, req->resource + 5, 9))
				continue;

			asprintf(&res->data, "%sICON0.PNG", item->path);
			return (file_exists(res->data) == SUCCESS);
		}

		return 0;
	}

	// http://vita-ip:8080/icon/00000000/ICON0.PNG
	if (wildcard_match(req->resource, "/icon/\?\?\?\?\?\?\?\?/ICON0.PNG"))
	{
		int id = 0;

		sscanf(req->resource + 6, "%08x", &id);
		item = list_get_item(list, id);
		asprintf(&res->data, "%sICON0.PNG", item->path);

		return (file_exists(res->data) == SUCCESS);
	}

	return 0;
}
*/
static void enableWebServer(dWebReqHandler_t handler, void* data, int port)
{
/*
	union SceNetApctlInfo ip_info;

	if (!network_up())
		return;

	memset(&ip_info, 0, sizeof(ip_info));
	sceNetApctlGetInfo(PSP_NET_APCTL_INFO_IP, &ip_info);
	LOG("Starting local web server %s:%d ...", ip_info.ip, port);

	if (dbg_webserver_start(port, handler, data))
	{
		show_message("Web Server on http://%s:%d\nPress OK to stop the Server.", ip_info.ip, port);
		dbg_webserver_stop();
	}
	else show_message("Error starting Web Server!");
*/
}

static void* dc_host_callback(int id, int* size)
{
	memset(host_buf, 0, sizeof(host_buf));

	switch (id)
	{
	case APOLLO_HOST_TEMP_PATH:
		return APOLLO_LOCAL_CACHE;

	case APOLLO_HOST_LAN_ADDR:
		if (1)//(sceWlanGetEtherAddr(host_buf) < 0)
			LOG("Error getting LAN Ethernet Address");

		if (size) *size = 6;
		return host_buf;
	}

	if (size) *size = 1;
	return host_buf;
}

static int list_has_name(list_t* list, const char* fname)
{
	list_node_t *node;

	for (node = list_head(list); node; node = list_next(node))
		if (strcmp(list_get(node), fname) == 0)
			return 1;

	return 0;
}

// VMU saves can't be patched in place: copy the file to RAM (once), patch that copy
static int vmu_patch_copy(const save_entry_t* entry, const char* filename, list_t* copied, char* tmpfile, size_t size)
{
	uint8_t* data;
	size_t len;
	int ok;

	snprintf(tmpfile, size, SAVEDB_TMP_PATH "%s", filename);
	if (list_has_name(copied, filename))
		return 1;

	if (!vmu_file_read(entry->path, filename, &data, &len))
		return 0;

	ok = (write_buffer(tmpfile, data, len) == SUCCESS);
	free(data);

	if (ok)
		list_append(copied, strdup(filename));

	return ok;
}

static int apply_cheat_patches(const save_entry_t* entry)
{
	int ret = 1;
	char tmpfile[256];
	char* filename;
	code_entry_t* code;
	list_node_t* node;
	list_t* vmu_files = list_alloc();

	init_loading_screen("Applying changes...");

	for (node = list_head(entry->codes); (code = list_get(node)); node = list_next(node))
	{
		if (!code->activated || (code->flags & APOLLO_CODE_FLAG_DISABLED) ||
			(code->type != PATCH_GAMEGENIE && code->type != PATCH_BSD))
			continue;

		LOG("Active code: [%s]", code->name);

		if (strrchr(code->file, '\\'))
			filename = strrchr(code->file, '\\')+1;
		else
			filename = code->file;

		// a wildcard target ("MVLVS??2_SYS"): the VMU save that was opened, when it matches;
		// otherwise the file picked in the code's options
		if (strpbrk(filename, "*?"))
		{
			if (entry->type == FILE_TYPE_VMS && wildcard_match_icase(entry->dir_name, filename))
				filename = entry->dir_name;
			else if (code->options)
			{
				option_value_t* optval = list_get_item(code->options[0].opts, code->options[0].sel);
				filename = optval->name;
			}
		}

		if (strstr(code->file, "~extracted\\"))
			snprintf(tmpfile, sizeof(tmpfile), "%s[%s]%s", APOLLO_LOCAL_CACHE, entry->title_id, filename);

		else if (entry->type == FILE_TYPE_VMS)
		{
			if (!vmu_patch_copy(entry, filename, vmu_files, tmpfile, sizeof(tmpfile)))
			{
				LOG("Error: can't read '%s' from %s", filename, entry->path);
				ret = 0;
				continue;
			}
		}
		else
			snprintf(tmpfile, sizeof(tmpfile), "%s%s", entry->path, filename);

		if (!apollo_apply_code(tmpfile, code, &dc_host_callback))
		{
			LOG("Error: failed to apply (%s)", code->name);
			ret = 0;
		}

		code->activated = 0;
	}

	// write the patched copies back to the VMU (vmu_file_write keeps a valid VMS CRC valid)
	for (node = list_head(vmu_files); (filename = list_get(node)); node = list_next(node))
	{
		uint8_t* data = NULL;
		size_t len;

		snprintf(tmpfile, sizeof(tmpfile), SAVEDB_TMP_PATH "%s", filename);
		LOG("Writing '%s' back to %s...", filename, entry->path);

		if (read_buffer(tmpfile, &data, &len) != 0 || !vmu_file_write(entry->path, filename, data, len))
		{
			LOG("Error: failed to write back (%s)", filename);
			ret = 0;
		}

		if (data)
			free(data);
		unlink(tmpfile);
		free(filename);
	}

	list_free(vmu_files);
	apollo_free_var_list();
	stop_loading_screen();

	return ret;
}

static void resignSave(save_entry_t* entry)
{
    LOG("Resigning save '%s'...", entry->name);

    LOG("Applying cheats to '%s'...", entry->name);
    if (!apply_cheat_patches(entry))
        show_message("Error! Cheat codes couldn't be applied");
    else
        show_message("Save %s successfully modified!", entry->title_id);
}

/*
 * Dreamcast VMU saves
 */

// FAT writes are cached by KOS: flush them so the card is safe to pull out
static void sync_storage(const char* path)
{
	if (strncmp(path, "/sd/", 4) == 0)
		fs_fat_sync("/sd");
}

static int vmu_target_has(const char* vmu_path, const char* name)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];

	return vmucard_find(vmu_path, name, raw);
}

// 1 copied, 0 failed, -1 skipped (already on the target and not overwriting)
static int _copy_vmu_save(const char* src, const char* name, const char* dst, int overwrite)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t* data;
	size_t len;
	int ret;

	if (!overwrite && vmu_target_has(dst, name))
		return -1;

	if (!vmucard_read(src, name, raw, &data, &len))
		return 0;

	ret = vmucard_write(dst, raw, data, len);
	free(data);

	return ret;
}

static void copyVmuSave(const save_entry_t* save, const char* dst)
{
	if (vmu_target_has(dst, save->dir_name) &&
		!show_dialog(DIALOG_TYPE_YESNO, "%s already exists on %s\nOverwrite it?", save->dir_name, dst))
		return;

	init_loading_screen("Copying save game...");
	int ret = _copy_vmu_save(save->path, save->dir_name, dst, 1);
	stop_loading_screen();

	if (ret > 0)
		show_message("Save successfully copied to:\n%s%s", dst, save->dir_name);
	else
		show_message("Error! Couldn't copy save to %s\n(not enough free blocks?)", dst);
}

// VMU names can hold characters a FAT file name can't
static void vmu_safe_name(const char* name, char* out, size_t size)
{
	size_t i;

	for (i = 0; name[i] && i < size - 1; i++)
		out[i] = (isalnum((uint8_t)name[i]) || strchr("_-.", name[i])) ? name[i] : '_';
	out[i] = 0;
}

// An 8-character resource name, from the VMU file name, that no .VMS in `out_dir` uses yet
static void get_resource_name(const char* out_dir, const char* filename, char* resource)
{
	char path[256];
	int n;

	for (n = 0; filename[n] && n < VMI_RESOURCE_LEN; n++)
		resource[n] = (isalnum((uint8_t)filename[n]) || filename[n] == '_' || filename[n] == '-') ? filename[n] : '_';
	resource[n] = 0;

	if (!n)
		strcpy(resource, "VMUSAVE");

	for (int i = 1; i < 100; i++)
	{
		snprintf(path, sizeof(path), "%s%s.VMS", out_dir, resource);
		if (file_exists(path) != SUCCESS)
			return;

		snprintf(resource + (n > 6 ? 6 : n), 3, "%02d", i);
	}
}

static int _export_vmu_save(const char* vmu_path, const char* name, int type, const char* out_dir)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t *data, *out;
	char path[256], fname[32];
	vmu_dirent_t ent;
	size_t len, olen;
	int ret = 0;

	if (!vmucard_read(vmu_path, name, raw, &data, &len))
		return 0;

	vmu_dirent_decode(raw, &ent);

	if (type == FILE_TYPE_VMI)
	{
		vms_header_t vms;
		uint8_t vmi[VMI_SIZE];
		char resource[VMI_RESOURCE_LEN + 1];

		get_resource_name(out_dir, ent.filename, resource);
		vmi_encode(&ent, resource, (vms_parse(data, len, vms_header_offset(&ent), &vms) == 0) ? vms.desc_dc : "",
			"Apollo Save Tool (DC)", (uint32_t) len, vmi);

		snprintf(path, sizeof(path), "%s%s.VMS", out_dir, resource);
		if (write_buffer(path, data, len) == SUCCESS)
		{
			snprintf(path, sizeof(path), "%s%s.VMI", out_dir, resource);
			ret = (write_buffer(path, vmi, VMI_SIZE) == SUCCESS);
		}
	}
	else if (dci_encode(&ent, data, len, &out, &olen) == 0)
	{
		vmu_safe_name(ent.filename, fname, sizeof(fname));
		snprintf(path, sizeof(path), "%s%s.DCI", out_dir, fname);
		ret = (write_buffer(path, out, olen) == SUCCESS);
		free(out);
	}

	LOG("Export '%s' -> %s: %s", name, out_dir, ret ? "OK" : "failed");
	free(data);
	return ret;
}

static void exportVmuSave(const save_entry_t* save, int type, int dev)
{
	char out_dir[256];

	_set_dest_path(out_dir, dev, DC_SAVES_PATH);
	if (!check_export_dir(out_dir))
		return;

	init_loading_screen("Exporting save game...");
	int ret = _export_vmu_save(save->path, save->dir_name, type, out_dir);
	sync_storage(out_dir);
	stop_loading_screen();

	if (ret)
		show_message("Save successfully exported to:\n%s", out_dir);
	else
		show_message("Error! Couldn't export save:\n%s", save->dir_name);
}

static void exportAllVmuSaves(const save_entry_t* save, int dev, int all)
{
	char out_dir[256];
	int done = 0, err_count = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	_set_dest_path(out_dir, dev, DC_SAVES_PATH);
	if (!check_export_dir(out_dir))
		return;

	init_progress_bar("Exporting saves...");

	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (item->type != FILE_TYPE_VMS || (!all && !(item->flags & SAVE_FLAG_SELECTED)))
			continue;

		(_export_vmu_save(item->path, item->dir_name, FILE_TYPE_DCI, out_dir) ? done++ : err_count++);
	}

	sync_storage(out_dir);
	end_progress_bar();

	show_message("%d/%d Saves exported to:\n%s", done, done + err_count, out_dir);
}

// .DCI / .VMI file -> VMU or VMU image. 1 imported, 0 failed, -1 skipped (already there, not overwriting)
static int _import_dc_save(const char* file, const char* vmu_path, int overwrite)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t* data;
	size_t len;
	vmu_dirent_t ent;
	int ret;

	if (!dc_save_file_read(file, raw, &data, &len))
		return 0;

	vmu_dirent_decode(raw, &ent);
	if (!overwrite && vmu_target_has(vmu_path, ent.filename))
	{
		free(data);
		return -1;
	}

	ret = vmucard_write(vmu_path, raw, data, len);
	free(data);

	return ret;
}

static int importVmuSave(const char* file, const char* vmu_path)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t* data;
	size_t len;
	vmu_dirent_t ent;
	int ret;

	if (!dc_save_file_read(file, raw, &data, &len))
	{
		show_message("Error! Not a valid Dreamcast save:\n%s", file);
		return 0;
	}

	vmu_dirent_decode(raw, &ent);
	free(data);

	if (vmu_target_has(vmu_path, ent.filename) &&
		!show_dialog(DIALOG_TYPE_YESNO, "%s already exists on %s\nOverwrite it?", ent.filename, vmu_path))
		return 0;

	init_loading_screen("Importing save game...");
	ret = _import_dc_save(file, vmu_path, 1);
	stop_loading_screen();

	if (ret > 0)
		show_message("Save successfully imported to:\n%s%s", vmu_path, ent.filename);
	else
		show_message("Error! Couldn't import save to %s\n(not enough free blocks?)", vmu_path);

	return (ret > 0);
}

// Bulk copy to a VMU: saves on a VMU / VMU image are copied, .DCI / .VMI files
// (External Saves) imported. Saves already on the target are skipped, not overwritten.
static void copyAllSaves(const save_entry_t* save, const char* dst, int all)
{
	int done = 0, err_count = 0, skipped = 0, r;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Copying saves...");

	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (!all && !(item->flags & SAVE_FLAG_SELECTED))
			continue;

		if (item->type == FILE_TYPE_VMS)
			r = _copy_vmu_save(item->path, item->dir_name, dst, 0);
		else if (item->type == FILE_TYPE_DCI || item->type == FILE_TYPE_VMI)
			r = _import_dc_save(item->path, dst, 0);
		else
			continue;

		switch (r)
		{
		case 1:  done++; break;
		case 0:  err_count++; break;
		default: skipped++; break;
		}
	}

	end_progress_bar();

	if (skipped)
		show_message("%d/%d Saves copied to %s\n%d skipped: already on the target VMU", done, done + err_count + skipped, dst, skipped);
	else
		show_message("%d/%d Saves copied to %s", done, done + err_count, dst);
}

static void dump_progress(int done, int total)
{
	update_progress_bar(done, total, "Reading VMU blocks...");
}

// Raw 128 KB image of the whole VMU, every block below the filesystem: a full backup
static void dumpVmuImage(const save_entry_t* save, int dev)
{
	char out_dir[256], path[256];
	const char* id = save->path + strlen(VMU_PATH);     // "a1/"
	vmu_card_t* card;
	uint8_t* image;
	struct tm t;
	time_t now = time(NULL);
	int ok, fmt;

	_set_dest_path(out_dir, dev, DC_VMU_PATH);
	if (!check_export_dir(out_dir))
		return;

	if ((image = malloc(VMUDEV_IMAGE_SIZE)) == NULL || (card = malloc(sizeof(vmu_card_t))) == NULL)
	{
		free(image);
		show_message("Error! Not enough memory to dump the VMU");
		return;
	}

	init_progress_bar("Dumping VMU image...");
	ok = vmudev_dump(save->path, image, &dump_progress);
	end_progress_bar();

	if (!ok)
	{
		show_message("Error! Couldn't read VMU %c%c\n(was it removed?)", toupper(id[0]), id[1]);
		goto end;
	}

	// sanity check: the root block should describe a formatted card
	fmt = vmu_load(card, image, VMUDEV_IMAGE_SIZE);
	if (fmt != VMU_OK)
		LOG("Warning: VMU image root block check failed (%s)", vmu_strerror(fmt));

	localtime_r(&now, &t);
	snprintf(path, sizeof(path), "%sVMU_%c%c_%04d%02d%02d_%02d%02d%02d.BIN", out_dir, toupper(id[0]), id[1],
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

	init_loading_screen("Saving VMU image...");
	ok = (write_buffer(path, image, VMUDEV_IMAGE_SIZE) == SUCCESS);
	sync_storage(path);
	stop_loading_screen();

	if (!ok)
		show_message("Error! Couldn't write the VMU image:\n%s", path);
	else if (fmt != VMU_OK)
		show_message("VMU image saved, but the card doesn't look formatted:\n%s", path);
	else
		show_message("VMU image successfully saved to:\n%s", path);

end:
	free(card);
	free(image);
}

// Remove the files a DB save was unzipped to (only save files: the RAM disk is shared)
static void clean_savedb_tmp(void)
{
	char path[256];
	struct dirent* e;
	DIR* d = opendir(SAVEDB_TMP_PATH);

	while (d && (e = readdir(d)) != NULL)
	{
		if (!wildcard_match_icase(e->d_name, "*.VMI") && !wildcard_match_icase(e->d_name, "*.VMS") &&
			!wildcard_match_icase(e->d_name, "*.DCI"))
			continue;

		snprintf(path, sizeof(path), SAVEDB_TMP_PATH "%s", e->d_name);
		unlink(path);
	}

	if (d)
		closedir(d);
}

// Saves DB entry -> VMU: unzip to RAM, then import the .VMI (+ .VMS) or .DCI inside
static int importDbSave(const save_entry_t* game, const char* zip_name, const char* vmu_path)
{
	char zip[256], save[256] = "";
	struct dirent* e;
	DIR* d;
	int ret = 0;

	snprintf(zip, sizeof(zip), "%s%s", game->path, zip_name);
	if (game->path[0] != '/')
	{
		show_message("Error! Only a local Saves DB (disc, SD, PC) can be imported from");
		return 0;
	}

	clean_savedb_tmp();
	mkdirs(SAVEDB_TMP_PATH);

	if (!extract_zip(zip, SAVEDB_TMP_PATH))
	{
		show_message("Error extracting save game:\n%s", zip);
		return 0;
	}

	if ((d = opendir(SAVEDB_TMP_PATH)) != NULL)
	{
		while ((e = readdir(d)) != NULL && !save[0])
			if (wildcard_match_icase(e->d_name, "*.VMI") || wildcard_match_icase(e->d_name, "*.DCI"))
				snprintf(save, sizeof(save), SAVEDB_TMP_PATH "%s", e->d_name);
		closedir(d);
	}

	if (save[0])
		ret = importVmuSave(save, vmu_path);
	else
		show_message("Error! No .VMI or .DCI save found in:\n%s", zip);

	clean_savedb_tmp();
	return ret;
}

static int deleteSave(const save_entry_t* save)
{
	int ret = 0;

	if (!show_dialog(DIALOG_TYPE_YESNO, "Delete %s\n%s from %s?\n\nThis can't be undone!", save->dir_name, save->name, save->path))
		return 0;

	if (save->type == FILE_TYPE_VMS)
	{
		init_loading_screen("Deleting save...");
		ret = vmu_save_delete(save);
		stop_loading_screen();
	}

	if (ret)
		show_message("Save successfully deleted:\n%s", save->dir_name);
	else
		show_message("Error! Couldn't delete save:\n%s", save->dir_name);

	return ret;
}

static void downloadLink(const char* path)
{
	char url[256] = "http://";
	char out_path[256];

	if (!osk_dialog_get_text("Download URL", url, sizeof(url)))
		return;

	snprintf(out_path, sizeof(out_path), "%s%s", path, strrchr(url, '/')+1);

	if (http_download(url, NULL, out_path, 1))
		show_message("File successfully downloaded to:\n%s", out_path);
	else
		show_message("Error! File couldn't be downloaded");
}

void execCodeCommand(code_entry_t* code, const char* codecmd)
{
	switch (codecmd[0])
	{
		case CMD_DOWNLOAD_USB:
			downloadSave(selected_entry, code->file, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_COPY_SAVE_USB:
			exportVmuSave(selected_entry, code->options[0].id, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_COPY_SAVE_HDD:
			// External Saves: a .DCI / .VMI file; otherwise a save on a VMU (or VMU image)
			if (selected_entry->type == FILE_TYPE_DCI || selected_entry->type == FILE_TYPE_VMI)
				importVmuSave(selected_entry->path, codecmd + 2);
			else
				copyVmuSave(selected_entry, codecmd + 2);
			code->activated = 0;
			break;

		case CMD_COPY_SAVES_USB:
		case CMD_COPY_ALL_SAVES_USB:
			exportAllVmuSaves(selected_entry, codecmd[1], codecmd[0] == CMD_COPY_ALL_SAVES_USB);
			code->activated = 0;
			break;

		case CMD_IMP_SAVE_MC:
			if (importVmuSave(code->file, selected_entry->dir_name))
				selected_entry->flags |= SAVE_FLAG_UPDATED;
			code->activated = 0;
			break;

		case CMD_RESIGN_SAVE:
			resignSave(selected_entry);
			code->activated = 0;
			break;

		case CMD_COPY_SAVES_HDD:
		case CMD_COPY_ALL_SAVES_HDD:
			copyAllSaves(selected_entry, codecmd + 2, codecmd[0] == CMD_COPY_ALL_SAVES_HDD);
			code->activated = 0;
			break;

		case CMD_DOWNLOAD_HDD:
			importDbSave(selected_entry, code->file, codecmd + 2);
			code->activated = 0;
			break;

		case CMD_EXP_VMU_IMAGE:
			dumpVmuImage(selected_entry, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_DELETE_SAVE:
			if (deleteSave(selected_entry))
				selected_entry->flags |= SAVE_FLAG_UPDATED;
			else
				code->activated = 0;
			break;

		case CMD_SAVE_WEBSERVER:
//			enableWebServer(webReqHandler, ((void**)selected_entry->dir_name)[0], 8080);
			code->activated = 0;
			break;

		case CMD_EXTRACT_ARCHIVE:
			extractArchive(code->file);
			code->activated = 0;
			break;

		case CMD_URL_DOWNLOAD:
			downloadLink(selected_entry->path);
			code->activated = 0;
			break;

		case CMD_NET_WEBSERVER:
//			enableWebServer(dbg_simpleWebServerHandler, NULL, 8080);
			code->activated = 0;
			break;

		default:
			break;
	}

	return;
}
