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

#ifndef VIRTUAL_PBP
#define VIRTUAL_PBP

typedef struct _PBPSection {
	u32 lba;
	u32 size;
} PBPSection;

typedef struct _VirtualPBP {
	u32 enabled;
	u32 pbp_total_size;
	u32 iso_total_size;
	u32 file_pointer;
	u32 opnssmp_type;
	char name[128];
	u32 header[10];
	PBPSection sects[8];
	ScePspDateTime ctime, mtime;
} VirtualPBP;

#define NOT_ALLOWED_FLAGS (PSP_O_WRONLY | PSP_O_APPEND | PSP_O_CREAT | PSP_O_TRUNC | PSP_O_EXCL)

#define ISO_ID "@ISOGAME@"
#define CACHE_MAX_SIZE 32
#define MAGIC_VPBP_FD 0x7000
#define MAX_VPBP 128

#define PSP_CACHE_PATH "ms0:/PSP/SYSTEM/isocache3.bin"
#define PSPGO_CACHE_PATH "ef0:/PSP/SYSTEM/isocache3.bin"

int vpbp_init(void);
SceUID vpbp_open(const char * file, int flags, SceMode mode);
SceOff vpbp_lseek(SceUID fd, SceOff offset, int whence);
int vpbp_read(SceUID fd, void * data, SceSize size);
int vpbp_close(SceUID fd);
int vpbp_disable_all_caches(void);
int vpbp_remove(const char * file);
int vpbp_getstat(const char * file, SceIoStat * stat);
int vpbp_is_fd(SceUID fd);
int vpbp_loadexec(char * file, struct SceKernelLoadExecVSHParam * param);
SceUID vpbp_dopen(const char * dirname);
int vpbp_dread(SceUID fd, SceIoDirent * dir);
int vpbp_dclose(SceUID fd);
int vpbp_reset(int cache);
void vpbp_fixisopath(char *path);
void vpbp_fixisomanualpath(char *path);
void vpbp_fixisodocinfopath(char *path);

#endif
