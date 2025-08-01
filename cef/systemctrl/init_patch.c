/*
	Adrenaline
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

#include <common.h>
#include <pspmodulemgr.h>

#include "main.h"
#include "init_patch.h"

SceUID (* sceKernelLoadModuleMs2Handler)(const char *path, int flags, SceKernelLMOption *option);
SceUID (* LoadModuleBufferAnchorInBtcnf)(void *buf, int a1);

u32 init_addr;
int leda_apitype;

int plugindone = 0;

SceUID sceKernelLoadModuleMs2Leda(const char *path, int flags, SceKernelLMOption *option) {
	return sceKernelLoadModuleMs2(leda_apitype, path, flags, option);
}

SceUID sceKernelLoadModuleMs2Init(int apitype, const char *path, int flags, SceKernelLMOption *option) {
	leda_apitype = apitype;
	return sceKernelLoadModuleMs2Handler(path, flags, option);
}

int sctrlHENRegisterHomebrewLoader(int (* handler)(const char *path, int flags, SceKernelLMOption *option)) {
	sceKernelLoadModuleMs2Handler = handler;
	u32 text_addr = ((u32)handler) - 0xCE8;

	// Remove version check
	MAKE_NOP(text_addr + 0xC58);

	// Remove patch of sceKernelGetUserLevel on sceLFatFs_Driver
	MAKE_NOP(text_addr + 0x1140);

	// Fix sceKernelLoadModuleMs2 call
	MAKE_JUMP(text_addr + 0x2E28, sceKernelLoadModuleMs2Leda);
	MAKE_JUMP(init_addr + 0x1C84, sceKernelLoadModuleMs2Init);

	sctrlFlushCache();

	return 0;
}

void trim(char *str) {
	for (int i = strlen(str) - 1; i >= 0; i--) {
		if (str[i] == 0x20 || str[i] == '\t') {
			str[i] = 0;
		} else {
			break;
		}
	}
}

int GetPlugin(char *buf, int size, char *str, int *activated) {
	int n = 0, i = 0;
	char *s = str;

	while (1) {
		if (i >= size) {
			break;
		}

		char ch = buf[i];

		if (ch < 0x20 && ch != '\t') {
			if (n != 0) {
				i++;
				break;
			}
		} else {
			*str++ = ch;
			n++;
		}

		i++;
	}

	trim(s);

	*activated = 0;

	if (i > 0) {
		char *p = strpbrk(s, " \t");
		if (p) {
			char *q = p + 1;

			while (*q < 0) {
				q++;
			}

			if (strcmp(q, "1") == 0) {
				*activated = 1;
			}

			*p = 0;
		}
	}

	return i;
}

int LoadStartModule(char *file) {
	SceUID mod = sceKernelLoadModule(file, 0, NULL);
	if (mod < 0)
		return mod;

	return sceKernelStartModule(mod, strlen(file) + 1, file, NULL, NULL);
}

SceUID sceKernelLoadModuleBufferBootInitBtcnfPatched(SceLoadCoreBootModuleInfo *info, void *buf, int flags, SceKernelLMOption *option) {
	if (config.usesonypsposk) {
		if (strcmp(info->name, "/kd/kermit_utility.prx") == 0) {
			info->name = "/kd/utility.prx";
		}
	}

	char path[64];
	sprintf(path, "ms0:/__ADRENALINE__/flash0%s", info->name); //not use flash0 cause of cxmb

	SceUID mod = sceKernelLoadModule(path, 0, NULL);
	if (mod >= 0)
		return mod;

	return sceKernelLoadModuleBufferBootInitBtcnf(info->size, buf, flags, option);
}

static void loadXmbControl(){
	int apitype = sceKernelInitApitype();
	if (apitype == 0x200 || apitype ==  0x210 || apitype ==  0x220 || apitype == 0x300){
		// load XMB Control Module
		int modid = sceKernelLoadModule("ms0:/__ADRENALINE__/flash0/vsh/module/xmbctrl.prx", 0, NULL);
		if (modid < 0) {
		  	modid = sceKernelLoadModule("flash0:/vsh/module/xmbctrl.prx", 0, NULL);
		}
		sceKernelStartModule(modid, 0, NULL, NULL, NULL);
	}
}

SceUID LoadModuleBufferAnchorInBtcnfPatched(void *buf, SceLoadCoreBootModuleInfo *info) {
	char path[64];
	sprintf(path, "ms0:/__ADRENALINE__/flash0%s", info->name);

	SceUID mod = sceKernelLoadModule(path, 0, NULL);
	if (mod >= 0)
		return mod;

	return LoadModuleBufferAnchorInBtcnf(buf, (info->attr >> 8) & 1);
}

int sceKernelStartModulePatched(SceUID modid, SceSize argsize, void *argp, int *status, SceKernelSMOption *option) {
	SceModule2 *mod = sceKernelFindModuleByUID(modid);
	SceUID fpl = -1;
	char *plug_buf = NULL;
	char *p;
	int res;

	if (mod != NULL) {
		if (strcmp(mod->modname, "vsh_module") == 0) {
			if (config.skiplogo || config.startupprog) {
				static u32 vshmain_args[0x100];
				memset(vshmain_args, 0, sizeof(vshmain_args));

				if (argp != NULL && argsize != 0) {
					memcpy(vshmain_args, argp, argsize);
				}

				// Init param
				vshmain_args[0] = sizeof(vshmain_args);
				vshmain_args[1] = 0x20;
				vshmain_args[16] = 1;

				if(config.startupprog && argsize == 0) {
					LoadExecForKernel_AA2029EC();

					struct SceKernelLoadExecVSHParam param;
					memset(&param, 0, sizeof(param));
					param.size = sizeof(param);
					param.args = sizeof("ms0:/PSP/GAME/BOOT/EBOOT.PBP");
					param.argp = "ms0:/PSP/GAME/BOOT/EBOOT.PBP";
					param.key = "game";

					sctrlKernelLoadExecVSHMs2("ms0:/PSP/GAME/BOOT/EBOOT.PBP", &param);
				}

				argsize = sizeof(vshmain_args);
				argp = vshmain_args;

			}
		} else if (!plugindone) {
			char *waitmodule;

			if (sceKernelFindModuleByName("sceNp9660_driver")) {
				waitmodule = "sceMeCodecWrapper";
			} else {
				waitmodule = "sceMediaSync";
			}

			if (sceKernelFindModuleByName(waitmodule)) {
				char plugin[64];
				int i, size;
				SceUID fd;

				plugindone = 1;

				int type = sceKernelInitKeyConfig();

				if (type == PSP_INIT_KEYCONFIG_VSH && config.enablexmbctrl) {
					loadXmbControl();
				}

				if (type == PSP_INIT_KEYCONFIG_VSH && !sceKernelFindModuleByName("scePspNpDrm_Driver")) {
					goto START_MODULE;
				}

				char *file = NULL;
				if (type == PSP_INIT_KEYCONFIG_VSH && !config.notusexmbplugins) {
					file = "ms0:/seplugins/vsh.txt";
				} else if (type == PSP_INIT_KEYCONFIG_GAME && !config.notusegameplugins) {
					file = "ms0:/seplugins/game.txt";
				} else if (type == PSP_INIT_KEYCONFIG_POPS && !config.notusepopsplugins) {
					file = "ms0:/seplugins/pops.txt";
				}

				if (!file) {
					goto START_MODULE;
				}

				for (i = 0; i < 0x10; i++) {
					fd = sceIoOpen(file, PSP_O_RDONLY, 0);

					if (fd >= 0) {
						break;
					}

					sceKernelDelayThread(20000);
				}

				if (fd < 0) {
					goto START_MODULE;
				}

				fpl = sceKernelCreateFpl("", PSP_MEMORY_PARTITION_KERNEL, 0, 1024, 1, NULL);
				if (fpl < 0)
					goto START_MODULE;

				sceKernelAllocateFpl(fpl, (void *)&plug_buf, NULL);

				size = sceIoRead(fd, plug_buf, 1024);
				p = plug_buf;

				do {
					int activated = 0;
					memset(plugin, 0, sizeof(plugin));

					res = GetPlugin(p, size, plugin, &activated);

					if (res > 0) {
						if (activated) {
							LoadStartModule(plugin);
						}

						size -= res;
						p += res;
					}
				} while (res > 0);

				sceIoClose(fd);
			}
		}
	}

START_MODULE:
	res = sceKernelStartModule(modid, argsize, argp, status, option);

	if (plug_buf) {
		sceKernelFreeFpl(fpl, plug_buf);
		sceKernelDeleteFpl(fpl);
	}

	return res;
}

int PatchInit(int (* module_bootstart)(SceSize, void *), void *argp) {
	init_addr = ((u32)module_bootstart) - 0x1A54;

	// Ignore StopInit
	MAKE_NOP(init_addr + 0x18EC);

	// Redirect load functions to load from MS
	LoadModuleBufferAnchorInBtcnf = (void *)init_addr + 0x1038;
	MAKE_CALL(init_addr + 0x17E4, LoadModuleBufferAnchorInBtcnfPatched);
	MAKE_INSTRUCTION(init_addr + 0x17E8, 0x02402821); // move $a1, $s2

	MAKE_INSTRUCTION(init_addr + 0x1868, 0x02402021); // move $a0, $s2
	MAKE_CALL(init_addr + 0x1878, sceKernelLoadModuleBufferBootInitBtcnfPatched);

	MAKE_JUMP(init_addr + 0x1C5C, sceKernelStartModulePatched);

	sctrlFlushCache();

	return module_bootstart(4, argp);
}