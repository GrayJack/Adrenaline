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

#ifndef DIRENT_TRACK_H
#define DIRENT_TRACK_H

#include <systemctrl.h>

typedef struct IoDirentEntry {
	char *path;
	SceUID dfd, iso_dfd;
	struct IoDirentEntry *next;
} IoDirentEntry;

int dirent_add(SceUID dfd, SceUID iso_dfd, const char *path);
int dirent_remove(IoDirentEntry *p);
IoDirentEntry *dirent_search(SceUID magic);

#endif
