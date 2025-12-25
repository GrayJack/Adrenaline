/*
	Adrenaline VSH Control
	Copyright (C) 2016-2018, TheFloW

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

#include <stddef.h>
#include <string.h>
#include <strings.h>

#include <pspctrl.h>
#include <pspsysmem_kernel.h>
#include <systemctrl.h>
#include <systemctrl_se.h>

#define _ADRENALINE_LOG_IMPL_
#include <adrenaline_log.h>

#include "patch_vsh.h"
#include "patch_io.h"
#include "virtual_pbp.h"

PSP_MODULE_INFO("VshControl", 0x1007, 2, 0);

static STMOD_HANDLER previous = NULL;

SceCtrlData *g_last_control_data = NULL;
char g_mounted_iso[64];
int g_has_umd_iso = 0;
u32 g_psp_model = 0;

AdrenalineConfig g_cfw_config;

///////////////////////////////////////////////////////

u32 g_firsttick;

int OnModuleStart(SceModule *mod) {
	char* modname = mod->modname;

	if (strcmp(modname, "vsh_module") == 0) {
		PatchIo();
		PatchVshMain(mod);
		PatchReadBufferPositive();
	} else if (strcmp(modname, "sysconf_plugin_module") == 0) {
		PatchSysconfPlugin(mod);
	} else if (strcmp(modname, "game_plugin_module") == 0) {
		PatchGamePlugin(mod);
	} else if (strcmp(modname, "update_plugin_module") == 0) {
		PatchUpdatePlugin(mod);
	} else if (strcmp(modname, "msvideo_main_plugin_module") == 0) {
		PatchMsVideoMainPlugin(mod);
	}

	if (!previous) {
		return 0;
	}

	return previous(mod);
}

int module_start(SceSize args, void *argp) {
	logInit("ms0:/log_vshctrl.txt");
	logmsg("VshCtrl started\n");

	g_psp_model = sceKernelGetModel();
	sctrlSEGetConfig(&g_cfw_config);

	if (g_cfw_config.vsh_cpu_speed != 0) {
		g_firsttick = sceKernelGetSystemTimeLow();
	}

	vpbp_init();
	previous = sctrlHENSetStartModuleHandler(OnModuleStart);

	// always reset to NORMAL mode in VSH
	// to avoid ISO mode is used in homebrews in next reboot
	char* umdfile = sctrlSEGetUmdFile();
	g_has_umd_iso = (umdfile[0] != 0 && sctrlSEGetBootConfFileIndex() == BOOT_VSHUMD);
	if (g_has_umd_iso) strcpy(g_mounted_iso, umdfile);
	sctrlSESetUmdFile("");
	sctrlSESetBootConfFileIndex(BOOT_NORMAL);

	return 0;
}