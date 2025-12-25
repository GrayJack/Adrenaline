/*
	Adrenaline VSH Control
	Copyright (C) 2016-2018, TheFloW
	Copyright (C) 2024-2025, isage
	Copyright (C) 2025, GrayJack

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>

#include <pspctrl.h>

#include <cfwmacros.h>

#include <adrenaline_log.h>

#include "utils.h"
#include "dirent_track.h"
#include "virtual_pbp.h"
#include "virtual_mp4.h"
#include "patch_io.h"
#include "externs.h"

#define MAGIC_DFD_FOR_DELETE 0x9000
#define MAGIC_DFD_FOR_DELETE_2 0x9001

static char g_iso_dir[128];
static char g_temp_delete_dir[128];
static int g_delete_eboot_injected = 0;

static const char *g_cfw_dirs[] = {
	"/seplugins",
	"/ISO",
	"/ISO/VIDEO",
};

static const char *g_game_list[] = {
	"ms0:/PSP/GAME/",
	"ef0:/PSP/GAME/",
};

////////////////////////////////////////////////////////////////////////////////
// HELPERS
////////////////////////////////////////////////////////////////////////////////

static int is_iso_dir(const char *path) {
	if (path == NULL) {
		return 0;
	}

	const char *p = strchr(path, '/');

	if (p == NULL) {
		return 0;
	}

	if (p <= path + 1 || p[-1] != ':') {
		return 0;
	}

	p = strstr(p, ISO_ID);

	if (NULL == p) {
		return 0;
	}

	p = strrchr(path, '@') + 1;
	p += 8;

	while(*p != '\0' && *p == '/') {
		p++;
	}

	if (*p != '\0') {
		return 0;
	}

	return 1;
}

int is_iso_file(const char* path, const char* file) {
	if (path == NULL) {
		return 0;
	}

	const char *p = strchr(path, '/');

	if (p == NULL) {
		return 0;
	}

	if (p <= path + 1 || p[-1] != ':') {
		return 0;
	}

	p = strstr(p, ISO_ID);

	if (NULL == p) {
		return 0;
	}

	p = strrchr(path, '@') + 9;

	if (p == NULL || 0 != strcmp(p, file)) {
		return 0;
	}

	return 1;
}

static inline int is_game_dir(const char *dirname) {
	char path[256];
	SceIoStat stat;

	const char *p = strchr(dirname, '/');

	if (p == NULL) {
		return 0;
	}

	if (0 != strncasecmp(p, "/PSP/GAME", sizeof("/PSP/GAME")-1)) {
		return 0;
	}

	if (0 == strncasecmp(p, "/PSP/GAME/_DEL_", sizeof("/PSP/GAME/_DEL_")-1)) {
		return 0;
	}

	static const char* game_files[] = {"/EBOOT.PBP", "/PBOOT.PBP", "/PARAM.PBP"};

	for (int i=0; i<NELEMS(game_files); i++) {
		STRCPY_S(path, dirname);
		STRCAT_S(path, game_files[i]);

		if(0 == sceIoGetstat(path, &stat)) {
			return 0;
		}
	}

	return 1;
}

int get_device_name(char *device, int size, const char* path) {
	if (path == NULL || device == NULL) {
		return -1;
	}

	const char *p = strchr(path, '/');

	if (p == NULL) {
		return -2;
	}

	strncpy(device, path, MIN(size, p-path+1));
	device[MIN(size-1, p-path)] = '\0';

	return 0;
}

static int CorruptIconPatch(char *name) {
	// Hide ARK bubble launchers
	if (strcasecmp(name, "SCPS10084") == 0 || strcasecmp(name, "NPUZ01234") == 0) {
		strcpy(name, "__SCE"); // hide icon
		return 1;
	}

	char path[256];
	for (int i=0; i<NELEMS(g_game_list); i++) {
		const char *hidden_path = g_game_list[i];
		strcpy(path, hidden_path);
		strcat(path, name);
		strcat(path, "%/EBOOT.PBP");

		SceIoStat stat;
		memset(&stat, 0, sizeof(stat));
		if (sceIoGetstat(path, &stat) >= 0) {
			strcpy(name, "__SCE"); // hide icon
			return 1;
		}
	}

	return 0;
}

static int HideDlc(char *name) {
	static char* dlc_files[] = {"PARAM.PBP", "PBOOT.PBP", "DOCUMENT.DAT"};

	char path[256];
	SceIoStat stat;

	for (int i=0; i<NELEMS(g_game_list); i++) {
		const char *hidden_path = g_game_list[i];

		for (int j=0; j<NELEMS(dlc_files); j++) {
			sprintf(path, "%s%s/%s", hidden_path, name, dlc_files[j]);
			memset(&stat, 0, sizeof(stat));
			if (sceIoGetstat(path, &stat) >= 0) {
				sprintf(path, "%s%s/EBOOT.PBP", hidden_path, name);

				memset(&stat, 0, sizeof(stat));
				if (sceIoGetstat(path, &stat) < 0) {
					strcpy(name, "__SCE"); // hide icon
					return 1;
				}
			}
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
// PATCHED IMPLEMENTATIONS
////////////////////////////////////////////////////////////////////////////////

SceUID sceIoDopenPatched(const char *dirname) {
	u32 k1 = pspSdkSetK1(0);

	SceUID res;
	if (is_iso_dir(dirname)) {
		res = MAGIC_DFD_FOR_DELETE;
		g_delete_eboot_injected = 0;
		strncpy(g_iso_dir, dirname, sizeof(g_iso_dir));
		g_iso_dir[sizeof(g_iso_dir)-1] = '\0';

	} else if (is_video_path(dirname)) {
		res = videoIoDopen(dirname);
		goto exit;
	}

	if (strcmp(dirname, g_temp_delete_dir) == 0) {
		res = MAGIC_DFD_FOR_DELETE_2;
		goto exit;
	}

	res = sceIoDopen(dirname);

	if (is_game_dir(dirname)) {
		char path[256] = {0};
		get_device_name(path, sizeof(path), dirname);
		STRCAT_S(path, "/ISO");

		const char *p = strstr(dirname, "/PSP/GAME");

		if (NULL != p) {
			p += sizeof("/PSP/GAME") - 1;
			STRCAT_S(path, p);
		}

		int iso_dfd = vpbp_dopen(path);

		if(iso_dfd < 0) {
			goto exit;
		}

		if(res < 0) {
			res = iso_dfd;
		}

		int ret = dirent_add(res, iso_dfd, dirname);

		if (ret < 0) {
			logmsg("%s: [ERROR]: dirent_add -> %d\n", __func__, ret);
			res = -1;
			goto exit;
		}
	}

exit:
	pspSdkSetK1(k1);
	logmsg3("%s: dirname=%s -> 0x%08X\n", __func__, dirname, res);
	return res;
}

int sceIoDreadPatched(SceUID fd, SceIoDirent *dir) {
	u32 k1 = pspSdkSetK1(0);

	int res;
	if (is_video_folder(fd)) {
		res = videoIoDread(fd, dir);
		goto exit;
	}

	if (fd == MAGIC_DFD_FOR_DELETE || fd == MAGIC_DFD_FOR_DELETE_2) {
		res = 0;

		if (g_delete_eboot_injected == 0) {
			memset(dir, 0, sizeof(*dir));
			res = vpbp_getstat(g_iso_dir, &dir->d_stat);

			if(fd == MAGIC_DFD_FOR_DELETE) {
				strcpy(dir->d_name, "EBOOT.PBP");
			} else {
				strcpy(dir->d_name, "_EBOOT.PBP");
			}

			g_delete_eboot_injected = 1;
			res = 1;
		}

		goto exit;
	}

	res = sceIoDread(fd, dir);

	if (res <= 0) {
		IoDirentEntry* entry = dirent_search(fd);
		if(entry != NULL) {
			res = vpbp_dread(fd, dir);
		}
	} else {

		if (g_cfw_config.hide_corrupt) {
			CorruptIconPatch(dir->d_name);
		}

		if (g_cfw_config.hide_dlcs) {
			HideDlc(dir->d_name);
		}
	}

exit:
	pspSdkSetK1(k1);
	logmsg3("%s: fd=0x%08lX, dir=%s -> 0x%08X\n", __func__, fd, dir->d_name, res);
	return res;
}

int sceIoDclosePatched(SceUID fd) {
	u32 k1 = pspSdkSetK1(0);

	int res;
	if (is_video_folder(fd)) {
		res = videoIoDclose(fd);
		goto exit;
	}

	if (fd == MAGIC_DFD_FOR_DELETE || fd == MAGIC_DFD_FOR_DELETE_2) {
		g_delete_eboot_injected = 0;
		res = 0;
		goto exit;
	}

	IoDirentEntry* entry = dirent_search(fd);

	if (entry != NULL) {
		if (entry->iso_dfd == fd) {
			vpbp_dclose(fd);

			res = 0;
		} else if (entry->dfd == fd) {
			u32 k1 = pspSdkSetK1(0);
			res = vpbp_dclose(fd);
			pspSdkSetK1(k1);
		} else {
			res = sceIoDclose(fd);
		}

		dirent_remove(entry);
	} else {
		res = sceIoDclose(fd);
	}

exit:
	pspSdkSetK1(k1);
	logmsg3("%s: fd=0x%08lX -> 0x%08X\n", __func__, fd, res);
	return res;
}

SceUID sceIoOpenPatched(const char *file, int flags, SceMode mode) {
	u32 k1 = pspSdkSetK1(0);

	SceUID res;
	if (is_iso_eboot(file)) {
		res = vpbp_open(file, flags, mode);
		goto clean_exit;
	} else if (is_video_path(file)) {
		res = videoIoOpen(file, flags, mode);
		goto clean_exit;
	} else if (is_iso_manual(file)) {
		char patched_file[MAX_INPUT] = {0};
		strcpy(patched_file, file);

		// Attempt <ISO-name>.DAT first
		vpbp_fixisomanualpath(patched_file);

		SceIoStat stat;
		int ret = sceIoGetstat(patched_file, &stat);

		// <ISO-name>.DAT exist
		if (ret == 0) {
			res = sceIoOpen(patched_file, flags, mode);
		} else {
			// Reset patched file
			memset(patched_file, 0, MAX_INPUT);
			strcpy(patched_file, file);
			// Attempt `/PSP/<GAMEID>/DOCUMENT.DAT`
			vpbp_fixisopath(patched_file);
			res = sceIoOpen(patched_file, flags, mode);
		}
		goto clean_exit;
	} else if (is_iso_docinfo(file)) {
		char patched_file[MAX_INPUT] = {0};
		strcpy(patched_file, file);

		// Attempt <ISO-name>.EDAT first
		vpbp_fixisodocinfopath(patched_file);

		SceIoStat stat;
		int ret = sceIoGetstat(patched_file, &stat);

		// <ISO-name>.EDAT exist
		if (ret == 0) {
			res = sceIoOpen(patched_file, flags, mode);
		} else {
			memset(patched_file, 0, MAX_INPUT);
			strcpy(patched_file, file);
			// Attempt `/PSP/<GAMEID>/DOCINFO.EDAT`
			vpbp_fixisopath(patched_file);
			res = sceIoOpen(patched_file, flags, mode);
		}
		goto clean_exit;
	} else if (is_iso_update(file) || is_iso_dlc(file)) {
		char patched_file[MAX_INPUT] = {0};
		strcpy(patched_file, file);
		vpbp_fixisopath(patched_file);
		res = sceIoOpen(patched_file, flags, mode);
		goto clean_exit;
	}

	pspSdkSetK1(k1);
	res = sceIoOpen(file, flags, mode);
	goto exit;

clean_exit:
	pspSdkSetK1(k1);
exit:
	logmsg3("%s: file=%s, flags=0x%08lX -> 0x%08X\n", __func__, file, flags, res);
	return res;
}

int sceIoReadPatched(SceUID fd, void *data, SceSize size) {
	u32 k1 = pspSdkSetK1(0);

	int res;
	if (vpbp_is_fd(fd)) {
		res = vpbp_read(fd, data, size);
	} else if (is_video_file(fd)) {
		res = videoIoRead(fd, data, size);
	} else {
		res = sceIoRead(fd, data, size);
	}

	pspSdkSetK1(k1);
	logmsg3("%s: fd=0x%08lX, data=0x%p, size=0x%08lX -> 0x%08X\n", __func__, fd, data, size, res);
	return res;
}

int sceIoClosePatched(SceUID fd) {
	u32 k1 = pspSdkSetK1(0);

	int res;
	if (vpbp_is_fd(fd)) {
		res = vpbp_close(fd);
	} else if (is_video_file(fd)) {
		res = videoIoClose(fd);
	} else {
		res = sceIoClose(fd);
	}

	pspSdkSetK1(k1);
	logmsg3("%s: fd=0x%08lX -> 0x%08X\n", __func__, fd, res);
	return res;
}

SceOff sceIoLseekPatched(SceUID fd, SceOff offset, int whence) {
	u32 k1 = pspSdkSetK1(0);

	SceOff res = 0;
	if (vpbp_is_fd(fd)) {
		res = vpbp_lseek(fd, offset, whence);
	} else if (is_video_file(fd)) {
		res = videoIoLseek(fd, offset, whence);
	} else {
		res = sceIoLseek(fd, offset, whence);
	}

	pspSdkSetK1(k1);
	logmsg3("%s: fd=0x%08lX, offset=0x%08lX, whence=%d -> 0x%08X\n", __func__, fd, offset, whence, res);
	return res;
}

int sceIoLseek32Patched(SceUID fd, int offset, int whence) {
	int res = 0;
	u32 k1 = pspSdkSetK1(0);

	if (vpbp_is_fd(fd)) {
		res = (int) vpbp_lseek(fd, offset, whence);
	} else if (is_video_file(fd)) {
		res = (int) videoIoLseek(fd, offset, whence);
	} else {
		res = sceIoLseek32(fd, offset, whence);
	}

	pspSdkSetK1(k1);
	logmsg3("%s: fd=0x%08lX, offset=0x%08X, whence=%d -> 0x%08X\n", __func__, fd, offset, whence, res);
	return res;
}

int sceIoGetstatPatched(const char *file, SceIoStat *stat) {
	u32 k1 = pspSdkSetK1(0);

	int res;
	if (is_iso_eboot(file)) {
		res = vpbp_getstat(file, stat);
	} else if (is_video_path(file)) {
		res = videoIoGetstat(file, stat);
	} else if (is_iso_docinfo(file)) {
		char patched_file[MAX_INPUT] = {0};
		strcpy(patched_file, file);

		// Attempt <ISO-name>.EDAT first
		vpbp_fixisodocinfopath(patched_file);
		res = sceIoGetstat(patched_file, stat);

		// <ISO-name>.EDAT don't exist
		if (res != 0) {
			memset(patched_file, 0, MAX_INPUT);
			strcpy(patched_file, file);
			// Attempt `/PSP/<GAMEID>/DOCINFO.EDAT`
			vpbp_fixisopath(patched_file);
			res = sceIoGetstat(patched_file, stat);
		}
	} else if (is_iso_manual(file)) {
		char patched_file[MAX_INPUT] = {0};
		strcpy(patched_file, file);

		// Attempt <ISO-name>.DAT first
		vpbp_fixisomanualpath(patched_file);
		res = sceIoGetstat(patched_file, stat);

		// <ISO-name>.DAT don't exist
		if (res != 0) {
			memset(patched_file, 0, MAX_INPUT);
			strcpy(patched_file, file);
			// Attempt `/PSP/<GAMEID>/DOCUMENT.DAT`
			vpbp_fixisopath(patched_file);
			res = sceIoGetstat(patched_file, stat);
		}
	} else {
		char patched_file[MAX_INPUT] = {0};
		strcpy(patched_file, file);

		if (is_iso_update(file) || is_iso_dlc(file)) {
			vpbp_fixisopath(patched_file);
		}
		res = sceIoGetstat(patched_file, stat);
	}

	pspSdkSetK1(k1);
	logmsg3("%s: file=%s -> 0x%08X\n", __func__, file, res);
	return res;
}

int sceIoRemovePatched(const char *file) {
	u32 k1 = pspSdkSetK1(0);
	int res;
	if (is_video_path(file)) {
		res = videoIoRemove(file);
		logmsg3("%s: [INFO]: <virtual-mp4> %s -> 0x%08X\n", __func__, file, res);
		return res;
	}

	if (g_temp_delete_dir[0] != '\0' && strncmp(file, g_temp_delete_dir, strlen(g_temp_delete_dir)) == 0) {
		res = 0;
		logmsg3("%s: [INFO]: <virtual> %s -> 0x%08X\n", __func__, file, res);
		return res;
	}

	res = sceIoRemove(file);

	pspSdkSetK1(k1);
	logmsg3("%s: file=%s -> 0x%08X\n", __func__, file, res);
	return res;
}

int sceIoRmdirPatched(const char *path) {
	u32 k1 = pspSdkSetK1(0);

	int res;
	if (strcmp(path, g_temp_delete_dir) == 0) {
		strcat(g_iso_dir, "/EBOOT.PBP");

		u32 k1 = pspSdkSetK1(0);
		res = vpbp_remove(g_iso_dir);
		pspSdkSetK1(k1);
		logmsg3("%s: [INFO]: <virtual> %s -> 0x%08X\n", __func__, path, res);
		g_iso_dir[0] = '\0';
		g_temp_delete_dir[0] = '\0';

		return res;
	}

	res = sceIoRmdir(path);

	pspSdkSetK1(k1);
	logmsg3("%s: file=%s -> 0x%08X\n", __func__, path, res);
	return res;
}

int sceIoMkdirPatched(const char *dir, SceMode mode) {
	u32 k1 = pspSdkSetK1(0);

	if(strcmp(dir, "ms0:/PSP/GAME") == 0 || strcmp(dir, "ef0:/PSP/GAME") == 0) {

		for(int i = 0; i < NELEMS(g_cfw_dirs); ++i) {
			char path[40];
			get_device_name(path, sizeof(path), dir);
			STRCAT_S(path, g_cfw_dirs[i]);
			sceIoMkdir(path, mode);
		}
	}

	int ret = sceIoMkdir(dir, mode);

	pspSdkSetK1(k1);
    return ret;
}

int sceIoRenamePatched(const char *oldname, const char *newname) {
	u32 k1 = pspSdkSetK1(0);

	int res;
	if (is_iso_dir(oldname)) {
		res = 0;
		strncpy(g_iso_dir, oldname, sizeof(g_iso_dir));
		g_iso_dir[sizeof(g_iso_dir)-1] = '\0';
		strncpy(g_temp_delete_dir, newname, sizeof(g_temp_delete_dir));
		logmsg3("%s: [INFO]: <virtual> %s %s -> 0x%08X\n", __func__, oldname, newname, res);
		return 0;
	}

	if(g_temp_delete_dir[0] != '\0' && strncmp(oldname, g_temp_delete_dir, strlen(g_temp_delete_dir)) == 0) {
		res = 0;
		logmsg3("%s: [INFO]: <virtual2> %s %s -> 0x%08X\n", __func__, oldname, newname, res);
		pspSdkSetK1(k1);
		return res;
	}

	res = sceIoRename(oldname, newname);

	pspSdkSetK1(k1);
	logmsg3("%s: oldname=%s newname=%s -> 0x%08X\n", __func__, oldname, newname, res);
	return res;
}

int sceIoChstatPatched(const char *file, SceIoStat *stat, int bits) {
	u32 k1 = pspSdkSetK1(0);

	int res;
	if(g_temp_delete_dir[0] != '\0' && strncmp(file, g_temp_delete_dir, strlen(g_temp_delete_dir)) == 0) {
		res = 0;
		logmsg3("%s: [INFO]: <virtual> %s -> 0x%08X\n", __func__, file, res);
		pspSdkSetK1(k1);
		return res;
	}

	res = sceIoChstat(file, stat, bits);

	pspSdkSetK1(k1);
	logmsg3("%s: file=%s -> 0x%08X\n", __func__, file, res);
	return res;
}

////////////////////////////////////////////////////////////////////////////////
// MODULE PATCHERS
////////////////////////////////////////////////////////////////////////////////

typedef struct {
	void *import;
	void *patched;
} ImportPatch;

ImportPatch import_patch[] = {
	// Directory functions
	{ &sceIoDopen, sceIoDopenPatched },
	{ &sceIoDread, sceIoDreadPatched },
	{ &sceIoDclose, sceIoDclosePatched },

	// File functions
	{ &sceIoOpen, sceIoOpenPatched },
	{ &sceIoClose, sceIoClosePatched },
	{ &sceIoRead, sceIoReadPatched },
	{ &sceIoLseek, sceIoLseekPatched },
	{ &sceIoLseek32, sceIoLseek32Patched },
	{ &sceIoGetstat, sceIoGetstatPatched },
	{ &sceIoChstat, sceIoChstatPatched },
	{ &sceIoRemove, sceIoRemovePatched },
	{ &sceIoRmdir, sceIoRmdirPatched },
	{ &sceIoMkdir, sceIoMkdirPatched },
};

void PatchIo() {
	for (int i = 0; i < (sizeof(import_patch) / sizeof(ImportPatch)); i++) {
		sctrlHENPatchSyscall(K_EXTRACT_IMPORT(import_patch[i].import), import_patch[i].patched);
	}
}
