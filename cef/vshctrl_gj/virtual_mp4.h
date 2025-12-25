/*
	Adrenaline VSH Control
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

#ifndef VIRTUAL_MP4
#define VIRTUAL_MP4

#include <systemctrl.h>

SceUID videoIoDopen(const char *dir);
int videoIoDread(SceUID fd, SceIoDirent *dir);
int videoIoDclose(SceUID fd);

SceUID videoIoOpen(const char *file, u32 flags, u32 mode);
int videoIoClose(SceUID fd);
int videoIoGetstat(const char *path, SceIoStat *stat);
int videoIoRead(SceUID fd, void *buf, u32 size);
SceOff videoIoLseek(SceUID fd, SceOff offset, int whence);
int videoIoRemove(const char *file);

int is_video_path(const char *path);
int is_video_file(SceUID fd);
int is_video_folder(SceUID dd);
#endif