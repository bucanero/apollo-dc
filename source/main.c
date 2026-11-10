/* 
	Apollo PSP main.c
*/

#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "saves.h"
#include "utils.h"
#include "common.h"
#include "dcpad.h"
#include "vmu_dev.h"

#include <dc/sd.h>
#include <dc/sci.h>
#include <fat/fs_fat.h>

//Font
#include "libfont.h"
#include "ttf_render.h"
#include "font_adonais.h"
#include "font-8x16.h"
#include "zfont.h"

//Menus
#include "menu.h"
#include "menu_gui.h"

//Sound
extern const uint8_t binary_data_haiku_s3m_start;
extern const uint8_t binary_data_haiku_s3m_size;

// Audio handle
//#include "s3mplay.h"

static void *font_ttf = NULL;

#define load_menu_texture(name, type) \
	({ extern const uint8_t binary_data_##name##_##type##_start; \
		extern const uint8_t binary_data_##name##_##type##_size; \
		menu_textures[name##_##type##_index].buffer = &binary_data_##name##_##type##_start; \
		menu_textures[name##_##type##_index].size = (int) &binary_data_##name##_##type##_size; \
	}); \
		if (!LoadMenuTexture(NULL, name##_##type##_index)) return 0;

void update_usb_path(char *p);
void update_hdd_path(char *p);
void update_vmc_path(char *p);
void update_db_path(char *p);

app_config_t apollo_config = {
    .app_name = "APOLLO",
    .app_ver = APOLLO_VERSION,
    .save_db = ONLINE_URL,
    .music = 1,
    .doSort = 1,
    .doAni = 1,
    .update = 0,
    .storage = 0,
    .user_id = 0,
};

int close_app = 0;
int idle_time = 0;                          // Set by readPad

png_texture * menu_textures;                // png_texture array for main menu, initialized in LoadTexture
SDL_Window* window;                         // SDL window
SDL_Renderer* renderer;                     // SDL software renderer
uint32_t* texture_mem;                      // Pointers to texture memory
uint32_t* free_mem;                         // Pointer after last texture


/*
* HDD save list
*/
save_list_t hdd_saves = {
    .icon_id = cat_hdd_png_index,
    .title = "VMU Saves",
    .list = NULL,
    .path = "",
    .ReadList = &ReadUserList,
    .ReadCodes = &ReadVmuCodes,
    .UpdatePath = &update_hdd_path,
};

/*
* USB save list
*/
save_list_t usb_saves = {
    .icon_id = cat_usb_png_index,
    .title = "External Saves",
    .list = NULL,
    .path = "",
    .ReadList = &ReadUsbList,
    .ReadCodes = &ReadExtCodes,
    .UpdatePath = &update_usb_path,
};

/*
* VMU image (opened from External Saves): listed and managed like a VMU
*/
save_list_t vmc_saves = {
	.icon_id = cat_warning_png_index,
	.title = "VMU Image",
    .list = NULL,
    .path = "",
    .ReadList = &ReadUserList,
    .ReadCodes = &ReadVmuCodes,
    .UpdatePath = &update_vmc_path,
};

/*
* Online code list
*/
save_list_t online_saves = {
	.icon_id = cat_db_png_index,
	.title = "Saves Database",
    .list = NULL,
    .path = "",
    .ReadList = &ReadOnlineList,
    .ReadCodes = &ReadOnlineSaves,
    .UpdatePath = &update_db_path,
};

/*
* User Backup code list
*/
save_list_t user_backup = {
    .icon_id = cat_bup_png_index,
    .title = "User Tools",
    .list = NULL,
    .path = "",
    .ReadList = &ReadBackupList,
    .ReadCodes = &ReadBackupCodes,
    .UpdatePath = NULL,
};


static const char* get_button_prompts(char* prompt)
{
	switch (menu_id)
	{
		case MENU_USB_SAVES:
		case MENU_HDD_SAVES:
		case MENU_VMC_SAVES:
			snprintf(prompt, 0xFF, "\x12 %s    \x13 %s    \x11 %s    \x10 %s", "Select", "Back", "Details", "Refresh");
			break;

		case MENU_ONLINE_DB:
		case MENU_USER_BACKUP:
			snprintf(prompt, 0xFF, "\x12 %s    \x13 %s    \x10 %s", "Select", "Back", "Refresh");
			break;

		case MENU_SETTINGS:
		case MENU_CODE_OPTIONS:
			snprintf(prompt, 0xFF, "\x12 %s    \x13 %s", "Select", "Back");
			break;

		case MENU_CREDITS:
		case MENU_PATCH_VIEW:
		case MENU_SAVE_DETAILS:
			snprintf(prompt, 0xFF, "\x13 %s", "Back");
			break;

		case MENU_PATCHES:
			snprintf(prompt, 0xFF, "\x12 %s    \x11 %s    \x13 %s", "Select", "View Code", "Back");
			break;

		case MENU_HEX_EDITOR:
			snprintf(prompt, 0xFF, "\x12 %s    \x10 %s    \x13 %s", "Value Up", "Value Down", "Exit");
			break;

		case MENU_MAIN_SCREEN:
		default:
			prompt[0] = 0;
			break;
	}

	return prompt;
}

static void helpFooter(void)
{
	char footer[256];
	u8 alpha = 0xFF;

	if (dcPadGetConf()->idle > 0x100)
	{
		int dec = (dcPadGetConf()->idle - 0x100) * 4;
		alpha = (dec > alpha) ? 0 : (alpha - dec);
	}

	SetFontSize(APP_FONT_SIZE_DESCRIPTION);
	SetCurrentFont(font_adonais_regular);
	SetFontAlign(FONT_ALIGN_SCREEN_CENTER);
	SetFontColor(APP_FONT_COLOR | alpha, 0);
	DrawString(0, SCREEN_HEIGHT - 40, get_button_prompts(footer));
	SetFontAlign(FONT_ALIGN_LEFT);
}

static int initPad(void)
{
    // Set sampling mode
    if (dcPadInit() < 0)
    {
        LOG("[ERROR] Failed to open pad!");
        return 0;
    }

    return 1;
}

// Used only in initialization. Allocates 64 mb for textures and loads the font
static int LoadTextures_Menu(void)
{
	font_ttf = malloc(size_font_ttf);
	texture_mem = malloc(256 * 8 * 4);
	menu_textures = (png_texture *)calloc(TOTAL_MENU_TEXTURES, sizeof(png_texture));

	if(!texture_mem || !menu_textures || !font_ttf)
		return 0; // fail!

	ResetFont();
	free_mem = (u32 *) AddFontFromBitmapArray((u8 *) data_font_Adonais, (u8 *) texture_mem, 0x20, 0x7e, 32, 31, 1, BIT7_FIRST_PIXEL);
	free_mem = (u32 *) AddFontFromBitmapArray((u8 *) console_font_8x16, (u8 *) free_mem, 0, 0xFF, 8, 16, 1, BIT7_FIRST_PIXEL);

	uncompress(font_ttf, &size_font_ttf, (const Bytef *) zfont, sizeof(zfont));
	if (TTFLoadFont(0, NULL, font_ttf, size_font_ttf) != SUCCESS)
		return 0;

	free_mem = (u32*) init_ttf_table((u8*) free_mem);
	set_ttf_window(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WIN_SKIP_LF);

	//Init Main Menu textures
	load_menu_texture(bgimg, png);
	load_menu_texture(cheat, png);
//	load_menu_texture(leon, jpg);

	load_menu_texture(circle_loading_bg, png);
	load_menu_texture(circle_loading_seek, png);
	load_menu_texture(edit_shadow, png);

	load_menu_texture(footer_ico_a, png);
	load_menu_texture(footer_ico_b, png);
	load_menu_texture(footer_ico_x, png);
	load_menu_texture(footer_ico_y, png);
	load_menu_texture(header_dot, png);
	load_menu_texture(header_line, png);

	load_menu_texture(mark_arrow, png);
	load_menu_texture(mark_line, png);
	load_menu_texture(opt_off, png);
	load_menu_texture(opt_on, png);
	load_menu_texture(scroll_bg, png);
	load_menu_texture(scroll_lock, png);
	load_menu_texture(help, png);
	load_menu_texture(buk_scr, png);
	load_menu_texture(cat_about, png);
	load_menu_texture(cat_cheats, png);
	load_menu_texture(cat_opt, png);
	load_menu_texture(cat_usb, png);
	load_menu_texture(cat_bup, png);
	load_menu_texture(cat_db, png);
	load_menu_texture(cat_hdd, png);
	load_menu_texture(cat_sav, png);
	load_menu_texture(cat_warning, png);
	load_menu_texture(column_1, png);
	load_menu_texture(column_2, png);
	load_menu_texture(column_3, png);
	load_menu_texture(column_4, png);
	load_menu_texture(column_5, png);
	load_menu_texture(column_6, png);
	load_menu_texture(column_7, png);
	load_menu_texture(jar_about, png);
	load_menu_texture(jar_about_hover, png);
	load_menu_texture(jar_bup, png);
	load_menu_texture(jar_bup_hover, png);
	load_menu_texture(jar_db, png);
	load_menu_texture(jar_db_hover, png);
	load_menu_texture(jar_empty, png);
	load_menu_texture(jar_hdd, png);
	load_menu_texture(jar_hdd_hover, png);
	load_menu_texture(jar_opt, png);
	load_menu_texture(jar_opt_hover, png);
	load_menu_texture(jar_usb, png);
	load_menu_texture(jar_usb_hover, png);
	load_menu_texture(logo, png);
	load_menu_texture(logo_text, png);
	load_menu_texture(tag_lock, png);
	load_menu_texture(tag_own, png);
	load_menu_texture(tag_pack, png);
	load_menu_texture(tag_ps1, png);
	load_menu_texture(tag_dc, png);
	load_menu_texture(tag_warning, png);
	load_menu_texture(tag_zip, png);
	load_menu_texture(tag_net, png);
	load_menu_texture(tag_apply, png);
	load_menu_texture(tag_transfer, png);

	load_menu_texture(trp_sync, png);
	load_menu_texture(trp_gold, png);

	menu_textures[icon_png_file_index].texture = NULL;

	u32 tBytes = free_mem - texture_mem;
	LOG("LoadTextures_Menu() :: Allocated %db (%.02fkb, %.02fmb) for textures", tBytes, tBytes / (float)1024, tBytes / (float)(1024 * 1024));
	return 1;
}

static void LoadSounds(void)
{
//	AHX_Init();
//	AHX_LoadSongBuffer((void*) &_binary_data_inside_ahx_start, (int) &_binary_data_inside_ahx_size);
//	AHX_SubSong(0);
}

// External Saves: the SD card and the PC (dcload) are both scanned; path is set when either is there
void update_usb_path(char* path)
{
	const char* roots[] = {USB_PATH, HOST_PATH, NULL};

	for (int i = 0; roots[i]; i++)
		if (dir_exists(roots[i]) == SUCCESS)
		{
			strcpy(path, roots[i]);
			return;
		}

	path[0] = 0;
}

// Pick which VMU to browse: scan the maple bus, then ask only if more than one is plugged in
void update_hdd_path(char* path)
{
	int sel, count;
	char vmu_paths[MAX_VMU_DEVICES][VMUDEV_PATH_LEN];
	char vmu_names[MAX_VMU_DEVICES][VMUDEV_NAME_LEN];
	const char* vmu_opt[MAX_VMU_DEVICES + 1];

	path[0] = 0;
	count = vmudev_list(vmu_paths, vmu_names, MAX_VMU_DEVICES);

	if (count <= 0)
	{
		show_message("No VMU detected!\n\nPlease insert a Visual Memory Unit\nin a controller slot");
		return;
	}

	if (count == 1)
	{
		strcpy(path, vmu_paths[0]);
		return;
	}

	for (int i = 0; i < count; i++)
		vmu_opt[i] = vmu_names[i];
	vmu_opt[count] = NULL;

	if ((sel = show_multi_dialog(vmu_opt, "Select VMU")) < 0)
		return;

	strcpy(path, vmu_paths[sel]);
}

void update_vmc_path(char* path)
{
	if (file_exists(path) == SUCCESS)
		return;

	path[0] = 0;
}

// Saves DB: the copy on the disc, else one on the SD card or the PC (dcload), else the online one
void update_db_path(char* path)
{
	const char* local[] = {"/cd/", USB_PATH, HOST_PATH, NULL};

	for (int i = 0; local[i]; i++)
	{
		snprintf(path, 128, "%s" SAVEDB_PATH "games.txt", local[i]);
		if (file_exists(path) == SUCCESS)
		{
			snprintf(path, 128, "%s" SAVEDB_PATH, local[i]);
			LOG("Saves DB found at %s", path);
			return;
		}
	}

	strcpy(path, apollo_config.save_db);
}

static void registerSpecialChars(void)
{
	// Register save tags
	RegisterSpecialCharacter(CHAR_TAG_PS1, 0, 1.5, &menu_textures[tag_ps1_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_DC, 0, 1.5, &menu_textures[tag_dc_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_PACK, 0, 0.8, &menu_textures[tag_pack_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_LOCKED, 0, 1.3, &menu_textures[tag_lock_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_OWNER, 0, 1.3, &menu_textures[tag_own_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_WARNING, 0, 1.3, &menu_textures[tag_warning_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_APPLY, 2, 1.0, &menu_textures[tag_apply_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_ZIP, 0, 1.0, &menu_textures[tag_zip_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_NET, 0, 1.0, &menu_textures[tag_net_png_index]);
	RegisterSpecialCharacter(CHAR_TAG_TRANSFER, 0, 1.0, &menu_textures[tag_transfer_png_index]);

	// Register button icons
	RegisterSpecialCharacter(CHAR_BTN_X, 0, 1.2, &menu_textures[footer_ico_x_png_index]);
	RegisterSpecialCharacter(CHAR_BTN_Y, 0, 1.2, &menu_textures[footer_ico_y_png_index]);
	RegisterSpecialCharacter(CHAR_BTN_A, 0, 1.2, &menu_textures[footer_ico_a_png_index]);
	RegisterSpecialCharacter(CHAR_BTN_B, 0, 1.2, &menu_textures[footer_ico_b_png_index]);

	// Register trophy icons
	RegisterSpecialCharacter(CHAR_TRP_GOLD, 2, 0.9f, &menu_textures[trp_gold_png_index]);
	RegisterSpecialCharacter(CHAR_TRP_SYNC, 0, 1.2f, &menu_textures[trp_sync_png_index]);
}

static void terminate(void)
{
	LOG("Exiting...");

//	AHX_Quit();
}

static int is_fat_partition(uint8_t type)
{
	switch (type)
	{
	case 0x01:  // FAT12
	case 0x04:  // FAT16 < 32 MB
	case 0x06:  // FAT16
	case 0x0B:  // FAT32 (CHS)
	case 0x0C:  // FAT32 (LBA)
	case 0x0E:  // FAT16 (LBA)
		return 1;
	}

	return 0;
}

// Mount the first FAT partition (of the 4 in the MBR) on the SD card at /sd
static int mount_sd_card(void)
{
	static kos_blockdev_t sd_dev;
	uint8_t type;

	if (fs_fat_init() != 0)
		return 0;

	for (int part = 0; part < 4; part++)
	{
		if (sd_blockdev_for_partition(part, &sd_dev, &type) != 0)
			continue;

		if (!is_fat_partition(type))
		{
			LOG("SD partition %d: not FAT (type %02X)", part, type);
			sd_dev.shutdown(&sd_dev);
			continue;
		}

		if (fs_fat_mount("/sd", &sd_dev, FS_FAT_MOUNT_READWRITE) == 0)
		{
			LOG("SD partition %d (type %02X) mounted at /sd", part, type);
			return 1;
		}

		LOG("SD partition %d (type %02X): can't mount", part, type);
		sd_dev.shutdown(&sd_dev);
	}

	return 0;
}

/*
 * KOS waits for each SCI SPI byte with no timeout. A real SH4 clocks the byte out
 * itself, connected or not, but emulators without SCI (Flycast) never finish it:
 * send one idle byte with a timeout first, and skip SCI if it doesn't come back.
 */
#define SCI_SCSCR1  (*(volatile uint8_t *)0xFFE00008)
#define SCI_SCTDR1  (*(volatile uint8_t *)0xFFE0000C)
#define SCI_SCSSR1  (*(volatile uint8_t *)0xFFE00010)
#define SCI_SCRDR1  (*(volatile uint8_t *)0xFFE00014)

static int sci_spi_works(void)
{
	int ok = 0;

	if (sci_init(SCI_SPI_BAUD_INIT, SCI_MODE_SPI, SCI_CLK_INT, 512) != SCI_OK)
		return 0;

	SCI_SCSCR1 |= 0x30;                     // TE | RE
	for (int t = 0; t < 100000 && !ok; t++)
		ok = (SCI_SCSSR1 & 0x80);           // TDRE

	if (ok)
	{
		SCI_SCTDR1 = 0xFF;
		SCI_SCSSR1 &= ~0x80;
		ok = 0;
		for (int t = 0; t < 100000 && !ok; t++)
			ok = (SCI_SCSSR1 & 0x40);       // RDRF

		if (ok)
		{
			(void) SCI_SCRDR1;
			SCI_SCSSR1 &= ~0x40;
		}
	}

	SCI_SCSCR1 &= ~0x30;
	sci_shutdown();

	if (!ok)
		LOG("SCI not responding: skipping SCI SD card adapter");

	return ok;
}

// Find an SD card adapter (serial port, then SCI) and mount it at /sd, for save exports and imports.
// CRC checks are off, as in DreamShell: some adapters and cards fail with them.
static int initInternal(void)
{
	const sd_interface_t ifaces[] = {SD_IF_SCIF, SD_IF_SCI};
	sd_init_params_t params = { .check_crc = false };

	for (int i = 0; i < 2; i++)
	{
		params.interface = ifaces[i];
		if ((ifaces[i] == SD_IF_SCI && !sci_spi_works()) || sd_init_ex(&params) != 0)
			continue;

		LOG("SD card found on %s: %d MB", (ifaces[i] == SD_IF_SCIF) ? "SCIF" : "SCI", (int)(sd_get_size() / (1024 * 1024)));
		if (mount_sd_card())
			return 1;

		LOG("No FAT partition found on the SD card");
		sd_shutdown();
	}

	LOG("No SD card mounted");
	return 1;
}

static int initialize_vitashell_modules()
{
	/*
	SceUID kern_modid, user_modid;
	char module_path[60] = {0};
	int search_unk[2];
	int res = 0;

	// Load kernel module
	if (_vshKernelSearchModuleByName("VitaShellKernel2", search_unk) < 0)
	{
		snprintf(module_path, sizeof(module_path), "ux0:VitaShell/module/kernel.skprx");
		if (file_exists(module_path) != SUCCESS)
		{
			snprintf(module_path, sizeof(module_path), APOLLO_APP_PATH "sce_module/kernel.skprx");
			if (file_exists(module_path) != SUCCESS)
			{
				LOG("Kernel module not found!");
				return 0;
			}
		}

		kern_modid = taiLoadKernelModule(module_path, 0, NULL);
		if (kern_modid >= 0)
		{
			res = taiStartKernelModule(kern_modid, 0, NULL, 0, NULL, NULL);
			if (res < 0)
				taiStopUnloadKernelModule(kern_modid, 0, NULL, 0, NULL, NULL);
		}

		if (kern_modid < 0 || res < 0)
		{
			LOG("Kernel module load error: %x\nPlease reboot.", kern_modid);
			return 0;
		}
	}

	// Load user module
	snprintf(module_path, sizeof(module_path), "ux0:VitaShell/module/user.suprx");
	if (file_exists(module_path) != SUCCESS)
	{
		snprintf(module_path, sizeof(module_path), APOLLO_APP_PATH "sce_module/user.suprx");
		if (file_exists(module_path) != SUCCESS)
		{
			LOG("User module not found!");
			return 0;
		}
	}

	user_modid = sceKernelLoadStartModule(module_path, 0, NULL, 0, NULL, NULL);
	if (user_modid < 0)
	{
		LOG("User module load %s error: %x\nPlease reboot.", module_path, user_modid);
		return 0;
	}

	// Allow writing to ux0:app/NP0APOLLO
	sceAppMgrUmount("app0:");
	sceAppMgrUmount("savedata0:");
*/
    return 1;
}

/*
	Program start
*/
int main(int argc, char *argv[])
{
#ifdef APOLLO_ENABLE_LOGGING
	// Frame tracking info for debugging
	uint32_t lastFrameTicks  = 0;
	uint32_t startFrameTicks = 0;
	uint32_t deltaFrameTicks = 0;

	dbglogger_init_mode(TTY_LOGGER, NULL, 0);
#endif
	dbglogger_init_mode(TTY_LOGGER, NULL, 0);

	// Initialize SDL functions
	LOG("Initializing SDL");
	// the Dreamcast SDL2 port defaults to a plain framebuffer (DMA) video mode with no OpenGL:
	// ask for the GLdc-backed mode that the OpenGL renderer needs
#ifdef SDL_HINT_DC_VIDEO_MODE
	SDL_SetHint(SDL_HINT_DC_VIDEO_MODE, "SDL_DC_OPENGL_VIDEO");
#endif

	if (SDL_Init(SDL_INIT_VIDEO) != SUCCESS)
	{
		LOG("Failed to initialize SDL: %s", SDL_GetError());
		return (-1);
	}

	initInternal();
//	http_init();
	initPad();

	// Open a handle to audio output device
	if (0)
	{
		LOG("[ERROR] Failed to load module: AHX");
		return (-1);
	}

	// Create a window context
	LOG("Creating a window");
	window = SDL_CreateWindow("main", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_FULLSCREEN);
	if (!window) {
		LOG("SDL_CreateWindow: %s", SDL_GetError());
		return (-1);
	}

	// Create a renderer (OpenGL ES2)
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		LOG("SDL_CreateRenderer: %s", SDL_GetError());
		return (-1);
	}

	// Initialize jailbreak
	if (!initialize_vitashell_modules())
		LOG("Error loading VitaShell modules!");

	mkdirs(APOLLO_DATA_PATH);
	mkdirs(APOLLO_LOCAL_CACHE);

	// Load texture
	if (!LoadTextures_Menu())
	{
		LOG("Failed to load menu textures!");
		return (-1);
	}
	registerSpecialChars();
	initMenuOptions();
	LoadSounds();

	// Load application settings
	load_app_settings(&apollo_config);

	// Unpack application data on first run
	if (file_exists(APOLLO_LOCAL_CACHE "appdata.zip") == SUCCESS)
	{
//		clean_directory(APOLLO_DATA_PATH);
		if (extract_zip(APOLLO_LOCAL_CACHE "appdata.zip", APOLLO_DATA_PATH))
			show_message("Successfully installed local application data");

		unlink_secure(APOLLO_LOCAL_CACHE "appdata.zip");
	}

	// dedicated to Luna ~ in loving memory (2011 - 2023)
	// Splash screen logo (fade-in)
	drawSplashLogo(1);
	sleep(2);
 
	// Setup font
	SetExtraSpace(-10);
	SetCurrentFont(font_adonais_regular);

	// Splash screen logo (fade-out)
	drawSplashLogo(-1);
	SDL_DestroyTexture(menu_textures[buk_scr_png_index].texture);
	
	//Set options
	update_callback(!apollo_config.update);

	// Start BGM audio thread
	music_callback(!apollo_config.music);
	Draw_MainMenu_Ani();

	while (!close_app)
	{
#ifdef APOLLO_ENABLE_LOGGING
        startFrameTicks = SDL_GetTicks();
        deltaFrameTicks = startFrameTicks - lastFrameTicks;
        lastFrameTicks  = startFrameTicks;
#endif
		// Clear the canvas
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(renderer);

		dcPadUpdate();
		drawScene();

		//Draw help
		helpFooter();

#ifdef APOLLO_ENABLE_LOGGING
		// Calculate FPS and ms/frame
		SetFontColor(APP_FONT_COLOR | 0xFF, 0);
		DrawFormatString(50, 500, "FPS: %d", (1000 / deltaFrameTicks));
#endif
		// Propagate the updated window to the screen
		SDL_RenderPresent(renderer);
	}

	drawEndLogo();

	// Cleanup resources
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	// Stop all SDL sub-systems
	SDL_Quit();
//	http_end();
	dcPadFinish();
	terminate();

	return 0;
}
