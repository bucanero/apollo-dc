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

#include <ctype.h>
#include <fat/fs_fat.h>

static char host_buf[256];

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

static void downloadSave(const save_entry_t* entry, const char* file, int dst)
{
	char path[256];

	_set_dest_path(path, dst, PS2_SAVES_PATH_USB);
	if (dst == STORAGE_MC0)
		snprintf(path, sizeof(path), PSP_SAVES_PATH_HDD);

	if (mkdirs(path) != SUCCESS)
	{
		show_message("Error! Export folder is not available:\n%s", path);
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

static uint32_t get_filename_id(const char* dir)
{
	char path[128];
	uint32_t tid = 0;

	do
	{
		tid++;
		snprintf(path, sizeof(path), "%s%08d.zip", dir, tid);
	}
	while (file_exists(path) == SUCCESS);

	return tid;
}

static void zipSave(const save_entry_t* entry, int dst)
{
	char exp_path[256];
	char* export_file;
	char* tmp;
	uint32_t fid;

	_set_dest_path(exp_path, dst, EXPORT_PATH);
	if (mkdirs(exp_path) != SUCCESS)
	{
		show_message("Error! Export folder is not available:\n%s", exp_path);
		return;
	}

	init_loading_screen("Exporting save game...");

	fid = get_filename_id(exp_path);
	asprintf(&export_file, "%s%08d.zip", exp_path, fid);

	tmp = strdup(entry->path);
	*strrchr(tmp, '/') = 0;
	*strrchr(tmp, '/') = 0;

	zip_directory(tmp, entry->path, export_file);

	sprintf(export_file, "%s%08x.txt", exp_path, apollo_config.user_id);
	FILE* f = fopen(export_file, "a");
	if (f)
	{
		fprintf(f, "%08d.zip=[%s] %s\n", fid, entry->title_id, entry->name);
		fclose(f);
	}

	sprintf(export_file, "%s%s", exp_path, OWNER_XML_FILE);
//	save_xml_owner(export_file);

	free(export_file);
	free(tmp);

	stop_loading_screen();
	show_message("Zip file successfully saved to:\n%s%08d.zip", exp_path, fid);
}

static void copySave(const save_entry_t* save, int dev)
{
	char* copy_path;
	char exp_path[256];

	_set_dest_path(exp_path, dev, PS2_SAVES_PATH_USB);
	if (strncmp(save->path, exp_path, strlen(exp_path)) == 0)
	{
		show_message("Copy operation cancelled!\nSame source and destination.");
		return;
	}

	if (mkdirs(exp_path) != SUCCESS)
	{
		show_message("Error! Export folder is not available:\n%s", exp_path);
		return;
	}

	init_loading_screen("Copying files...");

	asprintf(&copy_path, "%s%s/", exp_path, save->dir_name);

	LOG("Copying <%s> to %s...", save->path, copy_path);
	copy_directory(save->path, save->path, copy_path);

	free(copy_path);

	stop_loading_screen();
	show_message("Files successfully copied to:\n%s", exp_path);
}

static int _copy_save_memcard(const save_entry_t* save, int dev)
{
	char copy_path[256];

	_set_dest_path(copy_path, dev, save->dir_name);
	strcat(copy_path, "/");

	LOG("Copying <%s> to %s...", save->path, copy_path);
	return (copy_directory(save->path, save->path, copy_path) == SUCCESS);
}

static void copySaveMC(const save_entry_t* save, int dev)
{
	//source save is already on HDD
	if (save->flags & SAVE_FLAG_MEMCARD)
	{
		show_message("Copy operation cancelled!\nSame source and destination.");
		return;
	}

	init_loading_screen("Copying save game...");
	int ret = _copy_save_memcard(save, dev);
	stop_loading_screen();

	if (ret)
		show_message("Files successfully copied to:\nmc%d:/%s", dev, save->dir_name);
	else
		show_message("Error! Can't copy Save-game folder:\n%s", save->path);
}

static void copyAllSavesMC(const save_entry_t* save, int dev, int all)
{
	char mc_path[32];
	int done = 0, err_count = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Copying all saves...");
	_set_dest_path(mc_path, dev, "");

	LOG("Copying all saves from '%s' to mc%d:/...", save->path, dev);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (!all && !(item->flags & SAVE_FLAG_SELECTED))
			continue;

		if ((item->flags & SAVE_FLAG_PS1) || item->type == FILE_TYPE_PS2)
			(_copy_save_memcard(item, dev) ? done++ : err_count++);

		switch (item->type)
		{
		case FILE_TYPE_CBS:
//			(importCBS(item->path, mc_path) ? done++ : err_count++);
			break;

		case FILE_TYPE_MAX:
//			(importMAX(item->path, mc_path) ? done++ : err_count++);
			break;

		case FILE_TYPE_XPS:
//			(importXPS(item->path, mc_path) ? done++ : err_count++);
			break;

		case FILE_TYPE_PSU:
//			(importPSU(item->path, mc_path) ? done++ : err_count++);
			break;

		case FILE_TYPE_PSV:
//			(importPSV(item->path, mc_path) ? done++ : err_count++);
			break;

		default:
			break;
		}
	}

	end_progress_bar();

	show_message("%d/%d Saves copied to Memory Card %d", done, done+err_count, dev+1);
}

static void copyAllSavesVMC(const save_entry_t* save, int dev, int all)
{
	char mc_path[32];
	int done = 0, err_count = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Copying all saves...");
	_set_dest_path(mc_path, dev, "");

	LOG("Copying all saves from '%s' to mc%d:/...", save->path, dev);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (!all && !(item->flags & SAVE_FLAG_SELECTED))
			continue;

//		(importVMC(item->dir_name, mc_path) ? done++ : err_count++);
done++;
	}

	end_progress_bar();

	show_message("%d/%d Saves copied to Memory Card %d", done, done+err_count, dev+1);
}

static void exportAllSavesVMC(const save_entry_t* save, int dev, int all)
{
	char outPath[256];
	int done = 0, err_count = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Exporting all VMC saves...");
	_set_dest_path(outPath, dev, PS3_SAVES_PATH_USB);
	mkdirs(outPath);

	LOG("Exporting all saves from '%s' to mc%d:/...", save->path, dev);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (!all && !(item->flags & SAVE_FLAG_SELECTED))
			continue;

//		(vmc_export_psv(item->dir_name, outPath) ? done++ : err_count++);
done++;
	}

	end_progress_bar();

	show_message("%d/%d Saves exported to\n%s", done, done+err_count, outPath);
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
static void copySavePFS(const save_entry_t* save)
{
	char src_path[256];
	char hdd_path[256];
	char mount[32];
	sfo_patch_t patch = {
		.user_id = apollo_config.user_id,
		.account_id = apollo_config.account_id,
	};

	if (!vita_SaveMount(save, mount))
	{
		LOG("[!] Error: can't create/mount save!");
		return;
	}
	vita_SaveUmount(mount);

	snprintf(src_path, sizeof(src_path), "%s%s", save->path, save->dir_name);
	snprintf(hdd_path, sizeof(hdd_path), "/user/home/%08x/savedata/%s/sdimg_%s", apollo_config.user_id, save->title_id, save->dir_name);
	LOG("Copying <%s> to %s...", src_path, hdd_path);
	if (copy_file(src_path, hdd_path) != SUCCESS)
	{
		LOG("[!] Error: can't copy %s", hdd_path);
		return;
	}

	snprintf(src_path, sizeof(src_path), "%s%s.bin", save->path, save->dir_name);
	snprintf(hdd_path, sizeof(hdd_path), "/user/home/%08x/savedata/%s/%s.bin", apollo_config.user_id, save->title_id, save->dir_name);
	LOG("Copying <%s> to %s...", src_path, hdd_path);
	if (copy_file(src_path, hdd_path) != SUCCESS)
	{
		LOG("[!] Error: can't copy %s", hdd_path);
		return;
	}

	if (!vita_SaveMount(save, mount))
	{
		LOG("[!] Error: can't remount save");
		show_message("Error! Can't mount encrypted save.\n(incompatible save-game firmware version)");
		return;
	}

	snprintf(hdd_path, sizeof(hdd_path), APOLLO_SANDBOX_PATH "sce_sys/param.sfo", mount);
	if (show_dialog(1, "Resign save %s/%s?", save->title_id, save->dir_name))
		patch_sfo(hdd_path, &patch);

//	*strrchr(hdd_path, 'p') = 0;
//	_update_save_details(hdd_path, mount);
	vita_SaveUmount(mount);

	show_message("Encrypted save copied successfully!\n%s/%s", save->title_id, save->dir_name);
	return;
}
*/
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

static void importSave(const save_entry_t* save, const char* mc_path)
{
	int ret = 0;

	LOG("Importing save game '%s'... {%s}", save->path, mc_path);
	init_progress_bar("Importing save...");

	switch (save->type)
	{
	case FILE_TYPE_CBS:
//		ret = importCBS(save->path, mc_path);
		break;

	case FILE_TYPE_MAX:
//		ret = importMAX(save->path, mc_path);
		break;

	case FILE_TYPE_XPS:
//		ret = importXPS(save->path, mc_path);
		break;

	case FILE_TYPE_PSU:
//		ret = importPSU(save->path, mc_path);
		break;

	case FILE_TYPE_PSV:
//		ret = importPSV(save->path, mc_path);
		break;

	default:
		break;
	}

	end_progress_bar();

	if (ret)
		show_message("Save successfully imported to %s", mc_path);
	else
		show_message("Error: save couldn't be imported");
}

static void copyAllSavesUSB(const save_entry_t* save, int dev, int all)
{
	char dst_path[256];
	char copy_path[256];
	int done = 0, err_count = 0;
	uint64_t progress = 0;
	list_node_t *node;
	save_entry_t *item;
	list_t *list = ((void**)save->dir_name)[0];

	_set_dest_path(dst_path, dev, PS2_SAVES_PATH_USB);
	if (!list || mkdirs(dst_path) != SUCCESS)
	{
		show_message("Error! Folder is not available:\n%s", dst_path);
		return;
	}

	init_progress_bar("Copying all saves...");

	LOG("Copying all saves to '%s'...", dst_path);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (!all && !(item->flags & SAVE_FLAG_SELECTED))
			continue;

		snprintf(copy_path, sizeof(copy_path), "%s%s/", dst_path, item->dir_name);
		LOG("Copying <%s> to %s...", item->path, copy_path);

		if ((item->flags & SAVE_FLAG_PS1) || item->type == FILE_TYPE_PS2)
			(copy_directory(item->path, item->path, copy_path) == SUCCESS) ? done++ : err_count++;
	}

	end_progress_bar();
	show_message("%d/%d Saves copied to:\n%s", done, done+err_count, dst_path);
}

static void* ps2_host_callback(int id, int* size)
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

static int psp_is_decrypted(list_t* list, const char* fname)
{
	list_node_t *node;

	for (node = list_head(list); node; node = list_next(node))
		if (strcmp(list_get(node), fname) == 0)
			return 1;

	return 0;
}

static int apply_cheat_patches(const save_entry_t* entry)
{
	int ret = 1;
	char tmpfile[256];
	char* filename;
	code_entry_t* code;
	list_node_t* node;
	list_t* decrypted_files = list_alloc();
	uint8_t key[16];

	init_loading_screen("Applying changes...");

	for (node = list_head(entry->codes); (code = list_get(node)); node = list_next(node))
	{
		if (!code->activated || (code->type != PATCH_GAMEGENIE && code->type != PATCH_BSD))
			continue;

		LOG("Active code: [%s]", code->name);

		if (strrchr(code->file, '\\'))
			filename = strrchr(code->file, '\\')+1;
		else
			filename = code->file;

		if (strchr(filename, '*'))
		{
			option_value_t* optval = list_get_item(code->options[0].opts, code->options[0].sel);
			filename = optval->name;
		}

		if (strstr(code->file, "~extracted\\"))
			snprintf(tmpfile, sizeof(tmpfile), "%s[%s]%s", APOLLO_LOCAL_CACHE, entry->title_id, filename);
		else
		{
			snprintf(tmpfile, sizeof(tmpfile), "%s%s", entry->path, filename);
			LOG("Decrypting file '%s'", tmpfile);

			if (entry->flags & SAVE_FLAG_DC && !psp_is_decrypted(decrypted_files, filename))
			{
if(0)//				if (get_psp_save_key(entry, key) && psp_DecryptSavedata(entry->path, tmpfile, key))
				{
					LOG("Decrypted PSP file '%s'", filename);
					list_append(decrypted_files, strdup(filename));
				}
				else
				{
					LOG("Error: failed to decrypt (%s)", filename);
					ret = 0;
					continue;
				}
			}
		}

		if (!apply_cheat_patch_code(tmpfile, entry->title_id, code, &ps2_host_callback))
		{
			LOG("Error: failed to apply (%s)", code->name);
			ret = 0;
		}

		code->activated = 0;
	}

	for (node = list_head(decrypted_files); (filename = list_get(node)); node = list_next(node))
	{
		LOG("Encrypting '%s'...", filename);
if(0)//		if (!get_psp_save_key(entry, key) || !psp_EncryptSavedata(entry->path, filename, key))
		{
			LOG("Error: failed to encrypt (%s)", filename);
			ret = 0;
		}

		free(filename);
	}

	list_free(decrypted_files);
	free_patch_var_list();
	stop_loading_screen();

	return ret;
}

static void resignSave(save_entry_t* entry)
{
    LOG("Resigning save '%s'...", entry->name);

    LOG("Applying cheats to '%s'...", entry->name);
    if (!apply_cheat_patches(entry))
        show_message("Error! Cheat codes couldn't be applied");

    show_message("Save %s successfully modified!", entry->title_id);
}

static void exportAllSaves(const save_entry_t* save, int dev, int all)
{
	struct tm t;
	char outPath[256];
	int done = 0, err_count = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Exporting all saves...");
	_set_dest_path(outPath, dev, PS2_SAVES_PATH_USB);
	mkdirs(outPath);

	LOG("Exporting all saves from '%s' to %s...", save->path, outPath);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (item->type != FILE_TYPE_PS2 || !(all || item->flags & SAVE_FLAG_SELECTED))
			continue;

		gmtime_r(&(time_t){time(NULL)}, &t);
		sprintf(strrchr(outPath, '/'), "/%s_%d-%02d-%02d_%02d%02d%02d.psu", item->title_id,
			t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

//		(exportPSU(item->path, outPath) ? done++ : err_count++);
done++;
	}

	end_progress_bar();

	*strrchr(outPath, '/') = 0;
	show_message("%d/%d Saves exported to:\n%s", done, done+err_count, outPath);
}

static void export_ps2save(const save_entry_t* save, int type, int dst_id)
{
	int ret = 0;
	char outPath[256];
	struct tm t;

	_set_dest_path(outPath, dst_id, (type == FILE_TYPE_PSV) ? PS3_SAVES_PATH_USB : USER_PATH_USB);
	mkdirs(outPath);
	if (type != FILE_TYPE_PSV)
	{
		// build file path
		gmtime_r(&(time_t){time(NULL)}, &t);
		sprintf(strrchr(outPath, '/'), "/%s_%d-%02d-%02d_%02d%02d%02d.", save->title_id,
			t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
	}
	init_progress_bar("Exporting save...");

	switch (type)
	{
	case FILE_TYPE_CBS:
		strcat(outPath, "cbs");
//		ret = exportCBS(save->path, outPath, save->name);
		break;

	case FILE_TYPE_PSV:
//		ret = exportPSV(save->path, outPath);
		break;

	case FILE_TYPE_PSU:
		strcat(outPath, "psu");
//		ret = exportPSU(save->path, outPath);
		break;

	default:
		break;
	}
	end_progress_bar();

	if (ret)
		show_message("Save successfully exported to:\n%s", outPath);
	else
		show_message("Error exporting save:\n%s", save->path);
}

static void export_vmcsave(const save_entry_t* save, int type, int dst_id)
{
	int ret = 0;
	char outPath[256];
	struct tm t;

	_set_dest_path(outPath, dst_id, (type == FILE_TYPE_PSV) ? PS3_SAVES_PATH_USB : USER_PATH_USB);
	mkdirs(outPath);
	if (type != FILE_TYPE_PSV)
	{
		// build file path
		gmtime_r(&(time_t){time(NULL)}, &t);
		sprintf(strrchr(outPath, '/'), "/%s_%d-%02d-%02d_%02d%02d%02d.psu", save->title_id,
			t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
	}

	init_progress_bar("Exporting save ...");
	switch (type)
	{
	case FILE_TYPE_PSV:
//		ret = vmc_export_psv(save->dir_name, outPath);
		break;

	case FILE_TYPE_PSU:
//		ret = vmc_export_psu(save->dir_name, outPath);
		break;

	default:
		break;
	}
	end_progress_bar();

	if (ret)
		show_message("Save successfully exported to:\n%s", outPath);
	else
		show_message("Error exporting save:\n%s", save->path);
}

static void import_save2vmc(const char* src, int type)
{
	int ret = 0;

	init_progress_bar("Importing save ...");
	switch (type)
	{
	case FILE_TYPE_PSV:
//		ret = vmc_import_psv(src);
		break;

	case FILE_TYPE_PSU:
//		ret = vmc_import_psu(src);
		break;

	default:
		break;
	}
	end_progress_bar();

	if (ret)
		show_message("Successfully imported to VMC:\n%s", src);
	else
		show_message("Error importing save:\n%s", src);
}

static void copyVmcSave(const save_entry_t* save, const char* mc_path)
{
	init_progress_bar("Copying save game...");
	int ret = (dir_exists(mc_path) == SUCCESS) && 0; //importVMC(save->dir_name, mc_path);
	end_progress_bar();

	if (ret)
		show_message("Files successfully copied to:\n%s%s", mc_path, save->dir_name);
	else
		show_message("Error! Can't copy Save-game folder:\n%s", save->path);
}

static int _copy_save_file(const char* src_path, const char* dst_path, const char* filename)
{
	char src[256], dst[256];

	snprintf(src, sizeof(src), "%s%s", src_path, filename);
	snprintf(dst, sizeof(dst), "%s%s", dst_path, filename);

	return (copy_file(src, dst) == SUCCESS);
}

static void exportSaveFile(const save_entry_t* entry, const char* filename)
{
	char path[256];
	uint8_t key[16];

if(0)//	if (entry->flags & SAVE_FLAG_DC && !get_psp_save_key(entry, key))
	{
		show_message("Error! No game decryption key available for %s", entry->title_id);
		return;
	}

	snprintf(path, sizeof(path), APOLLO_USER_PATH "%s/", entry->title_id);
	mkdirs(path);

	LOG("Decrypt '%s%s' to '%s'...", entry->path, filename, path);

	if (_copy_save_file(entry->path, path, filename))
	{
		strlcat(path, filename, sizeof(path));
if(0)//		if (entry->flags & SAVE_FLAG_DC && !psp_DecryptSavedata(entry->path, path, key))
			show_message("Error! File %s couldn't be exported", filename);

		show_message("File successfully exported to:\n%s", path);
	}
	else
		show_message("Error! File %s couldn't be exported", filename);
}

static void importSaveFile(const save_entry_t* entry, const char* filename)
{
	char path[256];
	uint8_t key[16];

if(0)//	if (entry->flags & SAVE_FLAG_DC && !get_psp_save_key(entry, key))
	{
		show_message("Error! No game decryption key available for %s", entry->title_id);
		return;
	}

	snprintf(path, sizeof(path), APOLLO_USER_PATH "%s/%s", entry->title_id, filename);
	if (file_exists(path) != SUCCESS)
	{
		show_message("Error! Can't find decrypted save-game file:\n%s", path);
		return;
	}

	snprintf(path, sizeof(path), APOLLO_USER_PATH "%s/", entry->title_id);
	LOG("Encrypt '%s%s' to '%s'...", path, filename, entry->path);

	if (_copy_save_file(path, entry->path, filename))
	{
if(0)//		if (entry->flags & SAVE_FLAG_DC && !psp_EncryptSavedata(entry->path, filename, key))
			show_message("Error! File %s couldn't be imported", filename);

		show_message("File successfully imported to:\n%s%s", entry->path, filename);
	}
	else
		show_message("Error! File %s couldn't be imported", filename);
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

	return vmudev_find(vmu_path, name, raw);
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

	if (!vmudev_read_file(src, name, raw, &data, &len))
		return 0;

	ret = vmudev_write_file(dst, raw, data, len);
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

static void copyAllVmuSaves(const save_entry_t* save, const char* dst, int all)
{
	int done = 0, err_count = 0, skipped = 0;
	list_node_t *node;
	save_entry_t *item;
	uint64_t progress = 0;
	list_t *list = ((void**)save->dir_name)[0];

	init_progress_bar("Copying saves...");

	LOG("Copying saves from '%s' to '%s'...", save->path, dst);
	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		update_progress_bar(progress++, list_count(list), item->name);
		if (item->type != FILE_TYPE_VMS || (!all && !(item->flags & SAVE_FLAG_SELECTED)))
			continue;

		switch (_copy_vmu_save(item->path, item->dir_name, dst, 0))
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

	if (!vmudev_read_file(vmu_path, name, raw, &data, &len))
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
	if (mkdirs(out_dir) != SUCCESS)
	{
		show_message("Error! Export folder is not available:\n%s", out_dir);
		return;
	}

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
	if (mkdirs(out_dir) != SUCCESS)
	{
		show_message("Error! Export folder is not available:\n%s", out_dir);
		return;
	}

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

static int importVmuSave(const char* file, const char* vmu_path)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t* data;
	size_t len;
	vmu_dirent_t ent;
	int ret;

	if (!read_dc_save_file(file, &ent, &data, &len))
	{
		show_message("Error! Not a valid Dreamcast save:\n%s", file);
		return 0;
	}

	if (vmu_target_has(vmu_path, ent.filename) &&
		!show_dialog(DIALOG_TYPE_YESNO, "%s already exists on %s\nOverwrite it?", ent.filename, vmu_path))
	{
		free(data);
		return 0;
	}

	vmu_dirent_encode(&ent, raw);
	init_loading_screen("Importing save game...");
	ret = vmudev_write_file(vmu_path, raw, data, len);
	stop_loading_screen();
	free(data);

	if (ret)
		show_message("Save successfully imported to:\n%s%s", vmu_path, ent.filename);
	else
		show_message("Error! Couldn't import save to %s\n(not enough free blocks?)", vmu_path);

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
	option_value_t* optval;

	switch (codecmd[0])
	{
		case CMD_EXPORT_DATA_FILE:
			optval = list_get_item(code->options[0].opts, code->options[0].sel);
			exportSaveFile(selected_entry, optval->name);
			code->activated = 0;
			break;

		case CMD_DOWNLOAD_USB:
			downloadSave(selected_entry, code->file, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_EXPORT_ZIP_USB:
			zipSave(selected_entry, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_COPY_SAVE_USB:
			copySave(selected_entry, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_COPY_SAVE_HDD:
			copySaveMC(selected_entry, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_EXP_PS2SAVE:
			export_ps2save(selected_entry, code->options[0].id, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_EXP_VMCSAVE:
			export_vmcsave(selected_entry, code->options[0].id, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_IMP_VMCSAVE:
			import_save2vmc(code->file, codecmd[1]);
			code->activated = 0;
			break;
/*
		case CMD_RESIGN_VMP:
			resignVMP(selected_entry, code->options[0].name[code->options[0].sel]);
			code->activated = 0;
			break;
*/
		case CMD_COPY_SAVES_USB:
		case CMD_COPY_ALL_SAVES_USB:
			copyAllSavesUSB(selected_entry, codecmd[1], codecmd[0] == CMD_COPY_ALL_SAVES_USB);
			code->activated = 0;
			break;

		case CMD_IMP_SAVE_MC:
			importSave(selected_entry, codecmd[1] ? MC1_PATH : MC0_PATH);
			code->activated = 0;
			break;

		case CMD_RESIGN_SAVE:
			resignSave(selected_entry);
			code->activated = 0;
			break;

		case CMD_COPY_SAVE_VMC:
			copyVmcSave(selected_entry, codecmd[1] ? MC1_PATH : MC0_PATH);
			code->activated = 0;
			break;

		case CMD_EXPORT_SAVES:
		case CMD_EXPORT_ALL_SAVES:
			exportAllSaves(selected_entry, codecmd[1], codecmd[0] == CMD_EXPORT_ALL_SAVES);
			code->activated = 0;
			break;

		case CMD_COPY_SAVES_HDD:
		case CMD_COPY_ALL_SAVES_HDD:
			copyAllSavesMC(selected_entry, codecmd[1], codecmd[0] == CMD_COPY_ALL_SAVES_HDD);
			code->activated = 0;
			break;

		case CMD_COPY_SAVES_VMC:
		case CMD_COPY_ALL_SAVES_VMC:
			copyAllSavesVMC(selected_entry, codecmd[1], codecmd[0] == CMD_COPY_ALL_SAVES_VMC);
			code->activated = 0;
			break;

		case CMD_EXP_SAVES_VMC:
		case CMD_EXP_ALL_SAVES_VMC:
			exportAllSavesVMC(selected_entry, codecmd[1], codecmd[0] == CMD_COPY_ALL_SAVES_VMC);
			code->activated = 0;
			break;

		case CMD_COPY_SAVE_VMU:
			copyVmuSave(selected_entry, codecmd + 2);
			code->activated = 0;
			break;

		case CMD_COPY_SAVES_VMU:
		case CMD_COPY_ALL_SAVES_VMU:
			copyAllVmuSaves(selected_entry, codecmd + 2, codecmd[0] == CMD_COPY_ALL_SAVES_VMU);
			code->activated = 0;
			break;

		case CMD_EXP_VMUSAVE:
			exportVmuSave(selected_entry, code->options[0].id, codecmd[1]);
			code->activated = 0;
			break;

		case CMD_EXP_SAVES_VMU:
		case CMD_EXP_ALL_SAVES_VMU:
			exportAllVmuSaves(selected_entry, codecmd[1], codecmd[0] == CMD_EXP_ALL_SAVES_VMU);
			code->activated = 0;
			break;

		case CMD_IMP_VMUSAVE:
			if (importVmuSave(code->file, selected_entry->dir_name))
				selected_entry->flags |= SAVE_FLAG_UPDATED;
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

		case CMD_IMPORT_DATA_FILE:
			optval = list_get_item(code->options[0].opts, code->options[0].sel);
			importSaveFile(selected_entry, optval->name);
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
