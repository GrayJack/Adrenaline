/*
	Adrenaline
	Copyright (C) 2025, GrayJack
	Copyright (C) 2021, PRO CFW

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

#include "utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pspsdk.h>
#include <pspthreadman_kernel.h>
#include <pspumd.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <vshctrl.h>
#include <common.h>

#include <adrenaline_log.h>

#include "isoreader.h"
#include "virtual_pbp.h"
#include "dirent_track.h"
#include "virtual_sfo.h"

#define BOOT_BIN  "disc0:/PSP_GAME/SYSDIR/BOOT.BIN"
#define EBOOT_BIN "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN"
#define EBOOT_OLD "disc0:/PSP_GAME/SYSDIR/EBOOT.OLD"


typedef struct _PBPEntry {
	u32 enabled;
	char *name;
} PBPEntry;

static PBPEntry pbp_entries[8] = {
	{ 1, "/PSP_GAME/PARAM.SFO" },
	{ 1, "/PSP_GAME/ICON0.PNG" },
	{ 1, "/PSP_GAME/ICON1.PMF" },
	{ 1, "/PSP_GAME/PIC0.PNG"  },
	{ 1, "/PSP_GAME/PIC1.PNG"  },
	{ 1, "/PSP_GAME/SND0.AT3"  },
	{ 0, "DATA.PSP"  }, // placeholder, never enable or delete it
	{ 0, "DATA.PSAR" }, // placeholder, never enable or delete it
};

extern SEConfig config;

VirtualPBP *g_vpbps = NULL;
int g_vpbps_cnt = 0;
static int g_sema = -1;
static VirtualPBP *g_caches = NULL;
static u32 g_caches_cnt;
static u8 g_referenced[32];
static u8 g_need_update = 0;

static inline u32 get_isocache_magic(void) {
	u32 version;

	version = sctrlSEGetVersion() & 0xF;
	version = (version << 16) | sctrlHENGetMinorVersion();
	version += 0xC01DB15D;

	return version;
}

static int is_iso(SceIoDirent * dir) {
	//result
	int result = 0;

	// is it a file?
	if (!FIO_S_ISREG(dir->d_stat.st_mode)) {
		return 0;
	}

	// MacOS hidden file
	if (dir->d_name[0] == '.' && dir->d_stat.st_size < 0x8000) {
		return 0;
	}

	//grab extension
	char * ext = dir->d_name + strlen(dir->d_name) - 4;

	//filename length check
	if (ext > dir->d_name) {
		//check extension
		if (strcasecmp(ext, ".iso") == 0 || strcasecmp(ext, ".img") == 0
			|| strcasecmp(ext, ".cso") == 0 || strcasecmp(ext, ".zso") == 0
			|| strcasecmp(ext, ".dax") == 0 || strcasecmp(ext, ".jso") == 0) {
			result = 1;
		}
	}

	return result;
}

static VirtualPBP* get_vpbp_by_path(const char *path) {
	char *p;
	int isoindex;

	if (g_vpbps == NULL) {
		return NULL;
	}

	p = strstr(path, ISO_ID);

	if (p == NULL) {
		return NULL;
	}

	p = strrchr(path, '@') + 1;
	isoindex = strtol(p, NULL, 16);

	if (isoindex < 0 || isoindex >= g_vpbps_cnt) {
		return NULL;
	}

	return &g_vpbps[isoindex];
}

static inline void lock(void) {
	sceKernelWaitSema(g_sema, 1, 0);
}

static inline void unlock(void) {
	sceKernelSignalSema(g_sema, 1);
}

static VirtualPBP* get_vpbp_by_fd(SceUID fd) {
	fd -= MAGIC_VPBP_FD;

	if (fd < 0 || fd >= g_vpbps_cnt || g_vpbps == NULL) {
		return NULL;
	}

	return &g_vpbps[fd];
}

static int get_sfo_string(const char *sfo, const char *name, char *output, int output_size) {
	SFOHeader *header = (SFOHeader *)sfo;
	SFODir *entries = (SFODir *)(sfo+0x14);
	int i;

	if (header->signature != SFO_MAGIC) {
		return -39;
	}

	for (i=0; i<header->nitems; i++) {
		if (0 == strcmp(sfo+header->fields_table_offs+entries[i].field_offs, name)) {
			if(entries[i].type != 0x02) {
				return -41;
			}

			memset(output, 0, output_size);
			strncpy(output, sfo+header->values_table_offs+entries[i].val_offs, output_size);
			output[output_size-1] = '\0';

			return 0;
		}
	}

	return -40;
}

static int get_sfo_u32(const char *sfo, const char *name, u32 *output) {
	SFOHeader *header = (SFOHeader *)sfo;
	SFODir *entries = (SFODir *)(sfo+0x14);
	int i;

	if (header->signature != SFO_MAGIC) {
		return -39;
	}

	for (i=0; i<header->nitems; i++) {
		if (0 == strcmp(sfo+header->fields_table_offs+entries[i].field_offs, name)) {
			if (entries[i].type != 0x04) {
				return -41;
			}

			*output = *(u32*)(sfo+header->values_table_offs+entries[i].val_offs);

			return 0;
		}
	}

	return -40;
}

static int add_cache(VirtualPBP *vpbp) {
	if (vpbp == NULL || !vpbp->enabled || g_caches == NULL) {
		return SCE_EINVAL;
	}

	for (int i=0; i<g_caches_cnt; ++i) {
		if(!g_caches[i].enabled) {
			memcpy(&g_caches[i], vpbp, sizeof(*vpbp));
			g_referenced[i] = 1;
			g_need_update = 1;

			return 1;
		}
	}

	return 0;
}

static int load_cache(void) {

	if (g_caches == NULL) {
		return -32;
	}

	memset(g_caches, 0, sizeof(g_caches[0]) * g_caches_cnt);

	int fd = -1;
	for (int i=0; i<3; ++i) {
		fd = sceIoOpen(PSP_CACHE_PATH, PSP_O_RDONLY, 0777);

		if (fd >= 0) {
			break;
		}

		logmsg4("%s: [DEBUG]: open %s -> 0x%08X\n", __func__, PSP_CACHE_PATH, fd);
		fd = sceIoOpen(PSPGO_CACHE_PATH, PSP_O_RDONLY, 0777);

		if (fd >= 0) {
			break;
		}
		logmsg4("%s: [DEBUG]: open %s -> 0x%08X\n", __func__, PSPGO_CACHE_PATH, fd);
	}

	if (fd < 0) {
		return fd;
	}

	u32 magic = 0;
	int ret = sceIoRead(fd, &magic, sizeof(magic));

	if (ret != sizeof(magic) && magic != get_isocache_magic()) {
		return SCE_EIO;
	}

	sceIoRead(fd, g_caches, g_caches_cnt*sizeof(g_caches[0]));
	memset(g_referenced, 0, sizeof(g_referenced));
	sceIoClose(fd);

	return 0;
}

static int save_cache(void) {
	u32 magic = get_isocache_magic();

	if (g_caches == NULL) {
		return SCE_EINVAL;
	}

	for (int i = 0; i < g_caches_cnt; ++i) {
		if (g_caches[i].enabled && !g_referenced[i]) {
			g_need_update = 1;
			memset(&g_caches[i], 0, sizeof(g_caches[i]));
		}
	}

	if (!g_need_update) {
		logmsg3("%s: [INFO]: no need to update\n", __func__);
		return 0;
	}

	SceUID fd = -1;
	for (int i = 0; i < 3; ++i) {
		fd = sceIoOpen(PSP_CACHE_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);

		if (fd >= 0) {
			break;
		}
		logmsg4("%s: [DEBUG]: open %s -> 0x%08X\n", __func__, PSP_CACHE_PATH, fd);

		fd = sceIoOpen(PSPGO_CACHE_PATH, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);

		if (fd >= 0) {
		    break;
		}
		logmsg4("%s: [DEBUG]: open %s -> 0x%08X\n", __func__, PSPGO_CACHE_PATH, fd);
	}

	if (fd < 0) {
		return fd;
	}

	sceIoWrite(fd, &magic, sizeof(magic));
	sceIoWrite(fd, g_caches, sizeof(g_caches[0])*g_caches_cnt);
	sceIoClose(fd);

	g_need_update = 0;

	return 0;
}

static int get_iso_file_size(const char *path, u32 *file_size) {

	SceIoStat stat;
	int ret = sceIoGetstat(path, &stat);

	if (ret < 0) {
		return ret;
	}

	*file_size = stat.st_size;

	return 0;
}

static int get_cache(const char *file, ScePspDateTime *mtime, VirtualPBP* pbp) {

	if (g_caches == NULL) {
		return -34;
	}

	u32 file_size = 0;
	int ret = get_iso_file_size(file, &file_size);

	if (ret < 0) {
		return ret;
	}

	for (int i = 0; i < g_caches_cnt; ++i) {
		if (g_caches[i].enabled && 0 == strcmp(g_caches[i].name, file)) {
			if (file_size == g_caches[i].iso_total_size && memcmp(&g_caches[i].mtime, mtime, sizeof(*mtime)) == 0) {
				memcpy(pbp, &g_caches[i], sizeof(*pbp));
				g_referenced[i] = 1;

				return 0;
			}
		}
	}

	return SCE_ERR_NOTFOUND;
}

static int build_vpbp(VirtualPBP *vpbp) {
	logmsg3("[INFO]: Need to build vpbp %s\n", vpbp->name);
	memset(vpbp->header, 0, sizeof(vpbp->header));
	memset(vpbp->sects, 0, sizeof(vpbp->sects));
	vpbp->enabled = 1;
	vpbp->file_pointer = 0;
	vpbp->header[0] = 0x50425000; // PBP magic
	vpbp->header[1] = 0x10000; // version

	// fill vpbp offsets
	u32 off = 0x28;

	int ret = isoOpen(vpbp->name);

	if (ret < 0) {
		#ifdef DEBUG
		logmsg("%s: isoOpen -> %d\n", __func__, ret);
		#endif
		ret = add_cache(vpbp);
		return ret;
	}

	for (int i = 0; i < NELEMS(pbp_entries); ++i) {
		vpbp->header[i+2] = off;

		if (pbp_entries[i].enabled) {
			PBPSection *sec = &vpbp->sects[i];

			ret = isoGetFileInfo(pbp_entries[i].name, &sec->size, &sec->lba);

			if (ret < 0) {
				if (i == 0) {
					// no PARAM.SFO?
					// then it's a bad ISO
					isoClose();

					return -36;
				} else {
					continue;
				}
			}

			if (i == 0) {
				off += virtual_sfo_size();
			} else {
				off += sec->size;
			}
		}
	}

	vpbp->pbp_total_size = vpbp->header[9];
	get_iso_file_size(vpbp->name, &vpbp->iso_total_size);
	ret = add_cache(vpbp);
	logmsg4("%s: [DEBUG]:  add_cache -> %d\n", __func__, ret);
	isoClose();

	return ret;
}

void *oe_realloc(void *ptr, int size) {
	void *p;

	p = user_malloc(size);

	if(p != NULL && ptr != NULL) {
		memcpy(p, ptr, size);
		user_free(ptr);
	}

	return p;
}

static VirtualPBP *vpbp_realloc(VirtualPBP *vpbp, int size) {
	VirtualPBP *v;

	v = (VirtualPBP*) oe_realloc(vpbp, size * sizeof(vpbp[0]));

	return v;
}

static int get_header_section(VirtualPBP *vpbp, u32 remaining, void *data) {
	int re;

	re = MIN(remaining, vpbp->header[2] - vpbp->file_pointer);
	memcpy(data, vpbp->header+vpbp->file_pointer, re);

	return re;
}

static int get_sfo_section(VirtualPBP *vpbp, u32 remaining, void *data) {
	u32 parental_level = 1;

	void *buf = user_malloc(SECTOR_SIZE+64);

	if (buf == NULL) {
		logmsg("%s: [ERROR]: buf cannot allocate\n", __func__);
		return -13;
	}

	void *buf_64 = PTR_ALIGN_64(buf);
	int ret = isoRead(buf_64, vpbp->sects[0].lba, 0, SECTOR_SIZE);

	if (ret < 0) {
		logmsg("%s: [ERROR]: isoRead -> 0x%08X\n", __func__, ret);
		user_free(buf);
		return -37;
	}

	char sfotitle[64];
	ret = get_sfo_string(buf_64, "TITLE", sfotitle, sizeof(sfotitle));

	if (ret < 0) {
		user_free(buf);
		return ret;
	}

	char disc_id[12];
	ret = get_sfo_string(buf_64, "DISC_ID", disc_id, sizeof(disc_id));

	if (ret < 0) {
		user_free(buf);

		return ret;
	}

	get_sfo_u32(buf_64, "PARENTAL_LEVEL", &parental_level);

	get_sfo_u32(buf_64, "HRKGMP_VER", &(vpbp->opnssmp_type));

	user_free(buf);
	sfo_set_string_param("TITLE", sfotitle);
	sfo_set_string_param("DISC_ID", disc_id);
	sfo_set_int_param("PARENTAL_LEVEL", parental_level);
	int re = MIN(remaining, virtual_sfo_size() - (vpbp->file_pointer - vpbp->header[2]));
	memcpy(data, virtual_sfo_get()+vpbp->file_pointer-vpbp->header[2], re);

	return re;
}

static int get_pbp_section(VirtualPBP *vpbp, u32 remaining, int idx, void *data) {
	int buf_size = 8 * SECTOR_SIZE;
	int re = 0;
	int total_re = 0;
	int pos = vpbp->file_pointer;

	if(remaining == 0) {
		goto out;
	}

	if(!pbp_entries[idx].enabled) {
		goto out;
	}

	if (pos < vpbp->header[2]) {
		return get_header_section(vpbp, remaining, data);
	}

	if (pos >= vpbp->header[2] && pos < vpbp->header[3]) {
		return get_sfo_section(vpbp, remaining, data);
	}

	if(!(pos >= vpbp->header[2+idx] && pos < vpbp->header[2+1+idx])) {
		goto out;
	}

	void* buf = user_malloc(buf_size + 64);

	if(buf == NULL) {
		logmsg("%s: [ERROR]: buf(2) cannot allocate\n", __func__);
		return -5;
	}

	int pos_section = pos - vpbp->header[2+idx];
	int rest = MIN(remaining, vpbp->sects[idx].size - pos_section);
	void* buf_64 = PTR_ALIGN_64(buf);

	while (rest > 0) {
		int ret;

		re = MIN(rest, buf_size);
		ret = isoRead(buf_64, vpbp->sects[idx].lba, pos_section, re);

		if (ret < 0) {
			#ifdef DEBUG
			logmsg("%s: isoRead -> 0x%08X\n", __func__, ret);
			#endif
			return -38;
		}

		memcpy(data, buf_64, re);
		rest -= re;
		pos_section += re;
		data += re;
		remaining -= re;
		total_re += re;
		pos += re;
	}

	user_free(buf);

out:
	return total_re;
}

int vpbp_init(void) {
	if (g_sema >= 0) {
		sceKernelDeleteSema(g_sema);
	}

	g_sema = sceKernelCreateSema("VPBPSema", 0, 1, 1, NULL);

	if (g_caches != NULL) {
		user_free(g_caches);
		g_caches_cnt = 0;
	}

	g_caches_cnt = CACHE_MAX_SIZE;
	g_caches = user_malloc(sizeof(g_caches[0]) * g_caches_cnt);

	if (g_caches == NULL) {
		g_caches_cnt = 0;
		logmsg("%s: [ERROR]: g_cache cannot allocate\n", __func__);
		return -27;
	}

	memset(g_caches, 0, sizeof(g_caches[0]) * g_caches_cnt);

	virtual_sfo_init();

	logmsg("Virtual PBP initialized\n");
	return 0;
}

SceUID vpbp_open(const char * file, int flags, SceMode mode) {
	lock();

	if (file == NULL) {
		logmsg("%s: [ERROR]: file name is null\n", __func__);
		unlock();
		return SCE_EINVAL;
	}

	if (strlen(file) > 256) {
		logmsg("%s: [ERROR]: file name too long: %s\n", __func__, file);
		unlock();
		return SCE_ENAMETOOLONG;
	}

	if (flags & NOT_ALLOWED_FLAGS || !(flags & PSP_O_RDONLY)) {
		logmsg("%s: [ERROR]: bad flags 0x%08X\n", __func__, flags);
		unlock();
		return SCE_EFLAG;
	}

	VirtualPBP *vpbp = get_vpbp_by_path(file);

	if (vpbp == NULL) {
		logmsg("%s: [ERROR]: Unknown file %s in vpbp list\n", __func__, file);
		unlock();
		return -12;
	}

	if (vpbp->enabled) {
		vpbp->file_pointer = 0;
		int ret = isoOpen(vpbp->name);

		if (ret < 0) {
			logmsg("%s: [ERROR]: isoOpen -> %d\n", __func__, ret);
			unlock();
			return ret;
		}

		unlock();
		return MAGIC_VPBP_FD+(vpbp-&g_vpbps[0]);
	}

	unlock();
	logmsg("%s: [ERROR]: ISO already open\n", __func__);
	return SCE_EIO;
}

SceOff vpbp_lseek(SceUID fd, SceOff offset, int whence) {
	lock();
	VirtualPBP *vpbp = get_vpbp_by_fd(fd);

	if (vpbp == NULL) {
		logmsg("%s: [ERROR]: unknown fd 0x%08X\n", __func__, fd);
		unlock();
		return SCE_EBADF;
	}

	switch(whence) {
		case PSP_SEEK_SET:
			vpbp->file_pointer = (int)offset;
			break;
		case PSP_SEEK_CUR:
			vpbp->file_pointer += (int)offset;
			break;
		case PSP_SEEK_END:
			vpbp->file_pointer = vpbp->pbp_total_size + (int)offset;
			break;
		default:
			break;
	}

	unlock();
	return vpbp->file_pointer;
}

int vpbp_read(SceUID fd, void * data, SceSize size) {
	lock();
	VirtualPBP *vpbp = get_vpbp_by_fd(fd);

	if (vpbp == NULL) {
		logmsg("%s: [ERROR]: unknown fd 0x%08X\n", __func__, fd);
		unlock();
		return SCE_EBADF;
	}

	u32 remaining = size;

	while (remaining > 0) {
		for(int idx=0; idx < NELEMS(pbp_entries) - 2; ++idx) {
			int ret = get_pbp_section(vpbp, remaining, idx, data);

			if (ret >= 0) {
				data += ret;
				vpbp->file_pointer += ret;
				remaining -= ret;
			} else {
				unlock();
				return ret;
			}
		}

		if (vpbp->file_pointer >= vpbp->pbp_total_size) {
			break;
		}
	}

	unlock();
	return size - remaining;
}

int vpbp_close(SceUID fd) {
	lock();
	VirtualPBP *vpbp = get_vpbp_by_fd(fd);

	if (vpbp == NULL) {
		logmsg("%s: [ERROR]: unknown fd 0x%08X\n", __func__, fd);
		unlock();
		return SCE_EBADF;
	}

	isoClose();
	unlock();
	return 0;
}

int vpbp_disable_all_caches(void) {
	lock();

	if (g_vpbps == NULL) {
		unlock();
		return -35;
	}

	for (VirtualPBP *vpbp=g_vpbps; vpbp != &g_vpbps[g_vpbps_cnt]; vpbp++) {
		vpbp->enabled = 0;
	}

	unlock();
	return 0;
}

int vpbp_remove(const char * file) {
	lock();
	VirtualPBP* vpbp = get_vpbp_by_path(file);

	if (vpbp == NULL) {
		logmsg("%s: [ERROR]: Unknown file %s in vpbp list\n", __func__, file);
		unlock();
		return SCE_EINVAL;
	}

	int ret = sceIoRemove(vpbp->name);
	vpbp->enabled = 0;
	unlock();

	return ret;
}

int vpbp_is_fd(SceUID fd) {
	lock();
	VirtualPBP *vpbp = get_vpbp_by_fd(fd);

	int ret = 0;
	if (vpbp != NULL) {
		ret = 1;
	}

	unlock();
	return ret;
}

int vpbp_getstat(const char * file, SceIoStat * stat) {
	lock();
	VirtualPBP *vpbp = get_vpbp_by_path(file);

	if (vpbp == NULL) {
		logmsg("%s: [ERROR]: Unknown file %s in vpbp list\n", __func__, file);
		unlock();
		return SCE_EINVAL;
	}

	int ret = sceIoGetstat(vpbp->name, stat);
	stat->st_mode = 0x21FF;
	stat->st_attr = 0x20;
	stat->st_size = vpbp->iso_total_size;
	memcpy(&stat->sce_st_ctime, &vpbp->ctime, sizeof(ScePspDateTime));
	memcpy(&stat->sce_st_mtime, &vpbp->ctime, sizeof(ScePspDateTime));
	memcpy(&stat->sce_st_atime, &vpbp->ctime, sizeof(ScePspDateTime));
	unlock();

	return ret;
}

void vpbp_gameid(const char* isopath, char* game_id){
	// game ID is always at offset 0x8373 within the ISO
	int lba = 16;
	int pos = 883;

	int res = isoOpen(isopath);

	if (res < 0) {
		game_id[0] = 0;
		return;
	}

	isoRead(game_id, lba, pos, 10);
	isoClose();

	// remove the dash in the middle: ULUS-01234 -> ULUS01234
	game_id[4] = game_id[5];
	game_id[5] = game_id[6];
	game_id[6] = game_id[7];
	game_id[7] = game_id[8];
	game_id[8] = game_id[9];
	game_id[9] = 0;
}

int has_prometheus_module(const char *isopath) {
	int k1 = pspSdkSetK1(0);
	int ret = isoOpen(isopath);

	if (ret < 0) {
		pspSdkSetK1(k1);
		return 0;
	}

	u32 size, lba;
	ret = isoGetFileInfo("/PSP_GAME/SYSDIR/EBOOT.OLD", &size, &lba);
	ret = (ret >= 0) ? 1 : 0;

	isoClose();
	pspSdkSetK1(k1);
	return ret;
}

int has_update_file(const char* isopath, char* update_file) {
	int k1 = pspSdkSetK1(0);

	char game_id[10];
	vpbp_gameid(isopath, game_id);

	// try to find the update file
	sprintf(update_file, "ms0:/PSP/GAME/%s/PBOOT.PBP", game_id);
	SceIoStat stat;
	int res = sceIoGetstat(update_file, &stat);
	if (res >= 0){
		// found
		pspSdkSetK1(k1);
		return 1;
	}
	// not found
	pspSdkSetK1(k1);
	return 0;
}

void vpbp_fixisomanualpath(char* path) {
	VirtualPBP* vpbp = get_vpbp_by_path(path);
	if (vpbp == NULL) return;

	if (strcmp(strrchr(path,'/')+1, "DOCUMENT.DAT") == 0) {
		sprintf(path, "%s", vpbp->name);
		int len = strlen(path);
		((char*)path)[len-3] = 'D';
		((char*)path)[len-2] = 'A';
		((char*)path)[len-1] = 'T';
	}
}

void vpbp_fixisodocinfopath(char* path) {
	VirtualPBP* vpbp = get_vpbp_by_path(path);
	if (vpbp == NULL) return;

	if (strcmp(strrchr(path,'/')+1, "DOCINFO.EDAT") == 0) {
		sprintf(path, "%sT", vpbp->name);
		int len = strlen(path);
		((char*)path)[len-4] = 'E';
		((char*)path)[len-3] = 'D';
		((char*)path)[len-2] = 'A';
		((char*)path)[len-1] = 'T';
	}
}

void vpbp_fixisopath(char* path) {
	char game_id[10];
	VirtualPBP* vpbp = get_vpbp_by_path(path);
	if (vpbp == NULL) return;

	int k1 = pspSdkSetK1(0);
	vpbp_gameid(vpbp->name, game_id);
	pspSdkSetK1(k1);

	char* tmp = strrchr(path, '/');
	char* filename = tmp+1;
	*tmp = 0;

	tmp = strrchr(path, '/');
	sprintf(tmp+1, "%s/%s", game_id, filename);
}

int vpbp_loadexec(char * file, struct SceKernelLoadExecVSHParam * param) {
	int ret;
	VirtualPBP *vpbp;
	int apitype;
	const char *loadexec_file;

	lock();
	vpbp = get_vpbp_by_path(file);

	if (vpbp == NULL) {
		unlock();
		return -31;
	}

	u32* vshargp = param->vshmain_args;
	int vshargs = param->vshmain_args_size;
	memset(param, 0, sizeof(struct SceKernelLoadExecVSHParam));
	param->size = sizeof(struct SceKernelLoadExecVSHParam);
	if (vshargp){
		memset(vshargp, 0, vshargs);
		vshargp[0] = vshargs;
		vshargp[1] = 0x20;
		vshargp[16] = 1;
		param->vshmain_args = vshargp;
		param->vshmain_args_size = vshargs;
	}

	// fix sfo title
    u32* sfo_field = (u32 *)param + 36;
    u8* sfo = (u8*)(sfo_field[1]);
    if (sfo){
        sfo[0x24] = 0;
    }

	//set iso file for reboot
	sctrlSESetUmdFile(vpbp->name);

	//set iso mode for reboot
	sctrlSESetDiscType(PSP_UMD_TYPE_GAME);

	if (config.umd_mode == MODE_INFERNO) {
		logmsg2("[INFO]: Launching with Inferno Driver\n");
		sctrlSESetBootConfFileIndex(BOOT_INFERNO);
	} else if (config.umd_mode == MODE_MARCH33) {
		logmsg2("[INFO]: Launching with March33 Driver\n");
		sctrlSESetBootConfFileIndex(BOOT_MARCH33);
	} else if (config.umd_mode == MODE_NP9660) {
		logmsg2("[INFO]: Launching with NP9660 Driver\n");
		sctrlSESetBootConfFileIndex(BOOT_NP9660);
	}


	u32 opn_type = vpbp->opnssmp_type;
	SceGameInfo *info = sceKernelGetGameInfo();
	if (opn_type != 0) {
		info->opnssmp_ver = opn_type;
	}
	// u32 *info = (u32 *)sceKernelGetGameInfo();
	// if( opn_type )
	// 	info[216/4] = opn_type;

	param->key = "umdemu";
	apitype = SCE_APITYPE_UMD_EMU_MS1;
	// apitype = SCE_EXEC_APITYPE_DISC;

	static char pboot_path[256];
	int has_pboot = has_update_file(vpbp->name, pboot_path);

	if (has_pboot){
		// configure to use dlc/update
		apitype = SCE_APITYPE_UMD_EMU_MS2;
		param->argp = pboot_path;
		param->args = strlen(pboot_path) + 1;
		loadexec_file = param->argp;

		// if (psp_model == PSP_GO) {
		// 	char devicename[20];
		// 	ret = get_device_name(devicename, sizeof(devicename), pboot_path);
		// 	if(ret == 0 && 0 == stricmp(devicename, "ef0:")) {
		// 		apitype = SCE_APITYPE_UMD_EMU_EF2;
		// 	}
		// }
	} else {
		//reset and configure reboot parameter
		loadexec_file = vpbp->name;

		// if (psp_model == PSP_GO) {
		// 	char devicename[20];
		// 	ret = get_device_name(devicename, sizeof(devicename), vpbp->name);
		// 	if(ret == 0 && 0 == stricmp(devicename, "ef0:")) {
		// 		apitype = SCE_APITYPE_UMD_EMU_EF1;
		// 	}
		// }

		if (has_prometheus_module(vpbp->name)) {
			param->argp = EBOOT_OLD;
		} else if (config.execute_boot_bin) {
			param->argp = BOOT_BIN;
		} else {
			param->argp = EBOOT_BIN;
		}

		loadexec_file = param->argp;
		param->args = strlen(param->argp) + 1;
	}

	logmsg4("[DEBUG] To be exec: %s: %s 0x%04X\n", __func__, loadexec_file, apitype);

	//start game image
	ret = sctrlKernelLoadExecVSHWithApitype(apitype, loadexec_file, param);

	unlock();

	logmsg("%s: %s, 0x%04X -> 0x%08X\n", __func__, loadexec_file, apitype, ret);
	return ret;
}

SceUID vpbp_dopen(const char * dirname) {
	lock();
	SceUID result = sceIoDopen(dirname);

	if (result >= 0 && strlen(dirname) > 4 && 0 == strcasecmp(dirname+4, "/ISO")) {
		load_cache();
	}

	unlock();

	return result;
}

static int add_fake_dirent(SceIoDirent *dir, int vpbp_idx) {
	VirtualPBP *vpbp;

	vpbp = &g_vpbps[vpbp_idx];
	sprintf(dir->d_name, "%s%08X", ISO_ID, vpbp_idx);
	logmsg3("%s: [INFO]: ISO %s -> %s added\n", __func__, vpbp->name, dir->d_name);
	dir->d_stat.st_mode = 0x11FF;
	dir->d_stat.st_attr = 0x10;
	dir->d_stat.st_size = 0;

	memcpy(&dir->d_stat.sce_st_ctime, &vpbp->ctime, sizeof(ScePspDateTime));
	memcpy(&dir->d_stat.sce_st_mtime, &vpbp->ctime, sizeof(ScePspDateTime));
	memcpy(&dir->d_stat.sce_st_atime, &vpbp->ctime, sizeof(ScePspDateTime));

	return 1;
}

int vpbp_dread(SceUID fd, SceIoDirent * dir) {
	lock();

	IoDirentEntry *entry = dirent_search(fd);

	int result = 0;
	if(entry == NULL) {
		result = -44;
		goto exit;
	}

	result = sceIoDread(entry->iso_dfd, dir);

	if(sceKernelFindModuleByName("Game_Categories_Light") == NULL) {
		while(result > 0 && !is_iso(dir)) {
			result = sceIoDread(entry->iso_dfd, dir);
		}
	}

	if (result > 0 && is_iso(dir)) {
		VirtualPBP *vpbp;

		vpbp = vpbp_realloc(g_vpbps, g_vpbps_cnt+1);

		if(vpbp == NULL) {
			result = -42;
			goto exit;
		}

		g_vpbps = vpbp;
		g_vpbps_cnt++;
		int cur_idx = g_vpbps_cnt-1;
		vpbp = &g_vpbps[cur_idx];
		STRCPY_S(vpbp->name, entry->path);
		vpbp->name[4] = '\0';
		STRCAT_S(vpbp->name, "/ISO");
		STRCAT_S(vpbp->name, entry->path + sizeof("xxx:/PSP/GAME") - 1);
		STRCAT_S(vpbp->name, "/");
		STRCAT_S(vpbp->name, dir->d_name);
		memcpy(&vpbp->ctime, &dir->d_stat.sce_st_ctime, sizeof(vpbp->ctime));
		memcpy(&vpbp->mtime, &dir->d_stat.sce_st_mtime, sizeof(vpbp->mtime));

		int ret = get_cache(vpbp->name, &vpbp->mtime, vpbp);

		if (ret < 0) {
			ret = build_vpbp(vpbp);

			if (ret < 0) {
				result = -43;
				goto exit;
			}
		}

		result = add_fake_dirent(dir, cur_idx);
	}

exit:
	unlock();

	return result;
}

int vpbp_dclose(SceUID fd) {
	lock();
	IoDirentEntry *entry = dirent_search(fd);

	if (entry != NULL && strlen(entry->path) > 4 && 0 == strcasecmp(entry->path+4, "/PSP/GAME")) {
		save_cache();
	}

	int result = sceIoDclose(fd);
	unlock();

	return result;
}

int vpbp_reset(int cache) {
	if (g_vpbps != NULL) {
		user_free(g_vpbps);
		g_vpbps = NULL;
		g_vpbps_cnt = 0;
	}

	if(cache == 1) {
		if (g_caches != NULL) {
			user_free(g_caches);
			g_caches = NULL;
			g_caches_cnt = 0;
		}
	}

	logmsg2("[INFO]: Virtual PBP reset\n");

	return 0;
}
