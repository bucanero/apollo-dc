#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
#include <dc/spu.h>

#include "types.h"
#include "menu.h"
#include "saves.h"
#include "common.h"
#include "icons.h"
#include "utils.h"
#include "dcvmu.h"
#include "dcsave.h"
#include "vmu_dev.h"
#include "s3mplay.h"


char *strcasestr(const char *, const char *);
static const char* ext_src[MAX_USB_DEVICES+1] = {USB_PATH, HOST_PATH, "/cd/", NULL};
static const char* sort_opt[] = {"Disabled", "by Name", "by Title ID", NULL};

menu_option_t menu_options[] = {
	{ .name = "\nBackground Music", 
		.options = NULL, 
		.type = APP_OPTION_BOOL, 
		.value = &apollo_config.music, 
		.callback = music_callback 
	},
	{ .name = "Menu Animations", 
		.options = NULL, 
		.type = APP_OPTION_BOOL, 
		.value = &apollo_config.doAni, 
		.callback = ani_callback 
	},
	{ .name = "Sort Saves", 
		.options = (char**) sort_opt,
		.type = APP_OPTION_LIST,
		.value = &apollo_config.doSort, 
		.callback = sort_callback 
	},
	{ .name = "External Saves Source",
		.options = (char**) ext_src,
		.type = APP_OPTION_LIST,
		.value = &apollo_config.storage,
		.callback = owner_callback
	},
/*
	{ .name = "Version Update Check", 
		.options = NULL, 
		.type = APP_OPTION_BOOL, 
		.value = &apollo_config.update, 
		.callback = update_callback 
	},
	{ .name = "Update Application Data", 
		.options = NULL, 
		.type = APP_OPTION_CALL, 
		.value = NULL, 
		.callback = upd_appdata_callback 
	},
*/
	{ .name = "Clear Local Cache", 
		.options = NULL, 
		.type = APP_OPTION_CALL, 
		.value = NULL, 
		.callback = clearcache_callback 
	},
	{ .name = "Enable Debug Log",
		.options = NULL,
		.type = APP_OPTION_CALL,
		.value = NULL,
		.callback = log_callback 
	},
	{ .name = NULL }
};

//Sound
extern const uint8_t binary_data_haiku_s3m_start;
extern const uint8_t binary_data_haiku_s3m_size;

void music_callback(int sel)
{
	apollo_config.music = !sel;

	if(apollo_config.music)
		play_s3m((void*) &binary_data_haiku_s3m_start, (int) &binary_data_haiku_s3m_size);
	else
		spu_disable();
}

void sort_callback(int sel)
{
	apollo_config.doSort = sel;
}

void ani_callback(int sel)
{
	apollo_config.doAni = !sel;
}

void clearcache_callback(int sel)
{
	LOG("Cleaning folder '%s'...", APOLLO_LOCAL_CACHE);
	clean_directory(APOLLO_LOCAL_CACHE);

	show_message("Local cache folder cleaned:\n" APOLLO_LOCAL_CACHE);
}

void upd_appdata_callback(int sel)
{
	int i;

	if (!http_download(ONLINE_PATCH_URL, "apollo-psp-update.zip", APOLLO_LOCAL_CACHE "appdata.zip", 1))
	{
		show_message("Error! Can't download data update pack!");
		return;
	}

	if ((i = extract_zip(APOLLO_LOCAL_CACHE "appdata.zip", APOLLO_DATA_PATH)) > 0)
		show_message("Successfully updated %d data files!", i);
	else
		show_message("Error! Can't extract data update pack!");

	unlink_secure(APOLLO_LOCAL_CACHE "appdata.zip");
}

void update_callback(int sel)
{
	apollo_config.update = !sel;

	if (!apollo_config.update)
		return;

	LOG("checking latest Apollo version at %s", APOLLO_UPDATE_URL);

	if (!http_download(APOLLO_UPDATE_URL, NULL, APOLLO_LOCAL_CACHE "ver.check", 0))
	{
		LOG("http request to %s failed", APOLLO_UPDATE_URL);
		return;
	}

	char *buffer;
	long size = 0;

	buffer = readTextFile(APOLLO_LOCAL_CACHE "ver.check", &size);

	if (!buffer)
		return;

	LOG("received %ld bytes", size);

	static const char find[] = "\"name\":\"Apollo Save Tool v";
	const char* start = strstr(buffer, find);
	if (!start)
	{
		LOG("no name found");
		goto end_update;
	}

	LOG("found name");
	start += sizeof(find) - 1;

	char* end = strchr(start, '"');
	if (!end)
	{
		LOG("no end of name found");
		goto end_update;
	}
	*end = 0;
	LOG("latest version is %s", start);

	if (strcasecmp(APOLLO_VERSION, start) == 0)
	{
		LOG("no need to update");
		goto end_update;
	}

	start = strstr(end+1, "\"browser_download_url\":\"");
	if (!start)
		goto end_update;

	start += 24;
	end = strchr(start, '"');
	if (!end)
	{
		LOG("no download URL found");
		goto end_update;
	}

	*end = 0;
	LOG("download URL is %s", start);

	if (show_dialog(DIALOG_TYPE_YESNO, "New version available! Download update?"))
	{
		if (http_download(start, NULL, "ms0:/APOLLO/apollo-psp.zip", 1))
			show_message("Update downloaded to ms0:/APOLLO/apollo-psp.zip");
		else
			show_message("Download error!");
	}

end_update:
	free(buffer);
	return;
}

void owner_callback(int sel)
{
	apollo_config.storage = sel;
}

void log_callback(int sel)
{
	dbglogger_init_mode(FILE_LOGGER, USB_PATH "apollo.log", 1);
	show_message("Debug Logging Enabled!\n\n" USB_PATH "apollo.log");
}

/*
 * Settings are kept on a VMU as APOLLO__OPTS, a 2-block data file:
 *   0x000  VMS header (descriptions, 1 icon, CRC, palette)
 *   0x080  32x32 icon
 *   0x280  app_config_t
 * At launch every VMU is searched; the settings are written back to the VMU they
 * came from, or to the first VMU found.
 */
#define SETTINGS_FILE           "APOLLO__OPTS"
#define SETTINGS_BLOCKS         2
#define SETTINGS_DATA_OFF       (VMS_HEADER_SIZE + VMS_ICON_SIZE)

_Static_assert(SETTINGS_DATA_OFF + sizeof(app_config_t) <= SETTINGS_BLOCKS * VMU_BLOCK_SIZE, "settings don't fit in 2 blocks");

static char settings_vmu[VMUDEV_PATH_LEN];      // VMU the settings were read from / written to
static int settings_crc = -1;                   // VMS CRC of the file on that VMU, to skip writes when nothing changed

static void put_text(uint8_t* dst, const char* src, size_t len)
{
	memset(dst, ' ', len);
	memcpy(dst, src, strnlen(src, len));
}

static void build_settings_file(const app_config_t* config, uint8_t* buf)
{
	memset(buf, 0, SETTINGS_BLOCKS * VMU_BLOCK_SIZE);

	put_text(buf + 0x00, "APOLLO SETTINGS", 16);
	put_text(buf + 0x10, "Apollo Save Tool Settings", 32);
	memcpy(buf + 0x30, "APOLLO", 6);
	append_le_uint16(buf + 0x40, 1);                            // icons
	append_le_uint32(buf + 0x48, sizeof(app_config_t));         // payload
	for (int i = 0; i < 16; i++)
		append_le_uint16(buf + 0x60 + i * 2, apollo_vmu_icon_pal[i]);

	memcpy(buf + VMS_HEADER_SIZE, apollo_vmu_icon_bmp, VMS_ICON_SIZE);
	memcpy(buf + SETTINGS_DATA_OFF, config, sizeof(app_config_t));

	vms_update_crc(buf, SETTINGS_BLOCKS * VMU_BLOCK_SIZE, 0);
}

int save_app_settings(app_config_t* config)
{
	char paths[MAX_VMU_DEVICES][VMUDEV_PATH_LEN];
	char names[MAX_VMU_DEVICES][VMUDEV_NAME_LEN];
	uint8_t buf[SETTINGS_BLOCKS * VMU_BLOCK_SIZE];
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	vmu_dirent_t ent;

	// the CRC covers header, icon and settings: a new icon or app version also gets written
	build_settings_file(config, buf);
	if (settings_vmu[0] && vmudev_present(settings_vmu) && read_le_uint16(buf + VMS_CRC_OFFSET) == settings_crc)
	{
		LOG("Settings unchanged, not saving");
		return 1;
	}

	// the VMU it was loaded from, if still there; else the first VMU plugged in
	if (!settings_vmu[0] || !vmudev_present(settings_vmu))
	{
		if (vmudev_list(paths, names, MAX_VMU_DEVICES) < 1)
		{
			LOG("No VMU found, settings not saved");
			show_message("No VMU found!\nSettings couldn't be saved");
			return 0;
		}
		strcpy(settings_vmu, paths[0]);
	}

	LOG("Saving Settings to %s%s...", settings_vmu, SETTINGS_FILE);

	memset(&ent, 0, sizeof(ent));
	ent.filetype = VMU_FILE_DATA;
	ent.copyprotect = VMU_COPY_OK;
	strcpy(ent.filename, SETTINGS_FILE);
	ent.filesize = SETTINGS_BLOCKS;
	vmu_timestamp_now(&ent.timestamp);
	vmu_dirent_encode(&ent, raw);

	if (!vmudev_write_file(settings_vmu, raw, buf, sizeof(buf)))
	{
		LOG("Error saving settings!");
		show_message("Error! Settings couldn't be saved to:\n%s%s\n(not enough free blocks?)", settings_vmu, SETTINGS_FILE);
		return 0;
	}

	settings_crc = read_le_uint16(buf + VMS_CRC_OFFSET);
	return 1;
}

static int read_settings_file(const char* path, app_config_t* config)
{
	uint8_t raw[VMUDEV_DIRENT_SIZE];
	uint8_t* data;
	size_t len;
	vmu_dirent_t ent;
	vms_header_t vms;
	const app_config_t* file_cfg;
	int ok = 0;

	if (!vmudev_find(path, SETTINGS_FILE, raw) || !vmudev_read_file(path, SETTINGS_FILE, raw, &data, &len))
		return 0;

	vmu_dirent_decode(raw, &ent);
	file_cfg = (const app_config_t*) (data + SETTINGS_DATA_OFF);

	if (len < SETTINGS_DATA_OFF + sizeof(app_config_t) || vms_parse(data, len, 0, &vms) < 0 ||
		vms.payload != sizeof(app_config_t) || vms_calc_crc(&vms) != vms.crc)
		LOG("Invalid settings file on %s", path);

	else if (strncmp(file_cfg->app_name, config->app_name, sizeof(config->app_name)) != 0)
		LOG("Settings file on %s isn't Apollo's", path);

	else
	{
		uint32_t user_id = config->user_id;

		memcpy(config, file_cfg, sizeof(app_config_t));
		config->user_id = user_id;
		// the running version, not the one that saved the file
		strncpy(config->app_ver, APOLLO_VERSION, sizeof(config->app_ver));
		settings_crc = vms.crc;
		ok = 1;
	}

	free(data);
	return ok;
}

int load_app_settings(app_config_t* config)
{
	char paths[MAX_VMU_DEVICES][VMUDEV_PATH_LEN];
	char names[MAX_VMU_DEVICES][VMUDEV_NAME_LEN];
	int count;

	LOG("Apollo Save Tool v%s - Patch Engine v%s", APOLLO_VERSION, APOLLO_LIB_VERSION);
	LOG("Loading Settings...");
	settings_vmu[0] = 0;
	count = vmudev_list(paths, names, MAX_VMU_DEVICES);

	for (int i = 0; i < count; i++)
	{
		if (!read_settings_file(paths[i], config))
			continue;

		strcpy(settings_vmu, paths[i]);
		LOG("Settings loaded from %s%s", settings_vmu, SETTINGS_FILE);
		return 1;
	}

	// nothing found: keep the defaults, and create the file the first time Settings is left
	LOG("Settings not found, using defaults");
	return 0;
}
