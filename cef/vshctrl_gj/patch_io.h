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

#ifndef PATCHED_IO
#define PATCHED_IO

#define is_iso_eboot(path) is_iso_file(path, "/EBOOT.PBP")
#define is_iso_manual(path) is_iso_file(path, "/DOCUMENT.DAT")
#define is_iso_docinfo(path) is_iso_file(path, "/DOCINFO.EDAT")
#define is_iso_update(path) is_iso_file(path, "/PBOOT.PBP")
#define is_iso_dlc(path) is_iso_file(path, "/PARAM.PBP")
int is_iso_file(const char* path, const char* file);
int get_device_name(char *device, int size, const char* path);

SceUID sceIoDopenPatched(const char *dirname);
int sceIoDreadPatched(SceUID fd, SceIoDirent *dir);
int sceIoDclosePatched(SceUID fd);

SceUID sceIoOpenPatched(const char *file, int flags, SceMode mode);
int sceIoReadPatched(SceUID fd, void *data, SceSize size);
int sceIoClosePatched(SceUID fd);
SceOff sceIoLseekPatched(SceUID fd, SceOff offset, int whence);
int sceIoLseek32Patched(SceUID fd, int offset, int whence);
int sceIoGetstatPatched(const char *file, SceIoStat *stat);
int sceIoRemovePatched(const char *file);
int sceIoRmdirPatched(const char *path);
int sceIoMkdirPatched(const char *dir, SceMode mode);
int sceIoRenamePatched(const char *oldname, const char *name);
int sceIoChstatPatched(const char *file, SceIoStat *stat, int bits);

void PatchIo();


#endif