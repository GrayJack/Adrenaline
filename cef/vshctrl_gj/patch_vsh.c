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

#include <pspsdk.h>
#include <pspreg.h>
#include <pspumd.h>

#include <cfwmacros.h>
#include <systemctrl.h>

#include <adrenaline_log.h>

#include "utils.h"
#include "externs.h"
#include "patch_io.h"
#include "virtual_pbp.h"

////////////////////////////////////////////////////////////////////////////////
// HELPERS
////////////////////////////////////////////////////////////////////////////////

// Credits: ARK-4
static inline void ascii2utf16(char *dest, const char *src) {
	while(*src != '\0') {
		*dest++ = *src;
		*dest++ = '\0';
		src++;
	}
	*dest++ = '\0';
	*dest++ = '\0';
}

////////////////////////////////////////////////////////////////////////////////
// PATCHED IMPLEMENTATIONS
////////////////////////////////////////////////////////////////////////////////

int sceCtrlReadBufferPositivePatched(SceCtrlData *pad_data, int count) {
	static SceUID modid = -1;
	int res = sceCtrlReadBufferPositive(pad_data, count);
	int k1 = pspSdkSetK1(0);

	g_last_control_data = pad_data;

	if (!g_set && g_cfw_config.vsh_cpu_speed != 0) {
		u32 curtick = sceKernelGetSystemTimeLow();
		curtick -= g_firsttick;

		u32 t = (u32)curtick;
		if (t >= (10 * 1000 * 1000)) {
			g_set = 1;
			SetSpeed(g_cpu_list[g_cfw_config.vsh_cpu_speed % N_CPU], g_bus_list[g_cfw_config.vsh_cpu_speed % N_CPU]);
		}
	}

	if (!sceKernelFindModuleByName("EPI-VshCtrlSatelite")) {
		if (pad_data->Buttons & PSP_CTRL_SELECT) {
			int should_load = !sceKernelFindModuleByName("htmlviewer_plugin_module")
				&& !sceKernelFindModuleByName("sceVshOSK_Module")
				&& !sceKernelFindModuleByName("camera_plugin_module")
				&& !sceKernelFindModuleByName("Skyhost")
				&& !sceKernelFindModuleByName("sceUSB_Stor_Driver");

			if (should_load) {
				modid = sceKernelLoadModule("flash0:/vsh/module/satelite.prx", 0, NULL);
				if (modid >= 0) {
					sceKernelDelayThread(5000);
					sceKernelStartModule(modid, 0, NULL, NULL, NULL);
					pad_data->Buttons &= ~PSP_CTRL_SELECT;
				}
			}
		}
	} else {
		if (g_vshmenu_ctrl) {
			g_vshmenu_ctrl(pad_data, count);
		} else if (modid >= 0) {
			if (sceKernelStopModule(modid, 0, NULL, NULL, NULL) >= 0) {
				sceKernelUnloadModule(modid);
			}
		}
	}

	pspSdkSetK1(k1);
	return res;
}

int InitUsbPatched() {
	return sctrlStartUsb();
}

int ShutdownUsbPatched() {
	return sctrlStopUsb();
}

int GetUsbStatusPatched() {
	int state = sctrlGetUsbState();

	if (state & 0x20)
		return 1; // Connected

	return 2; // Not connected
}

int SetDefaultNicknamePatched() {
	int k1 = pspSdkSetK1(0);

	struct RegParam reg;
	REGHANDLE h;
	memset(&reg, 0, sizeof(reg));
	reg.regtype = 1;
	reg.namelen = strlen("flash1:/registry/system");
	reg.unk2 = 1;
	reg.unk3 = 1;
	strcpy(reg.name, "flash1:/registry/system");

	if (sceRegOpenRegistry(&reg, 2, &h) == 0) {
		REGHANDLE hd;
		if (!sceRegOpenCategory(h, "/CONFIG/SYSTEM", 2, &hd)) {
			char* name = (char*)0xa83ff050;
			if (sceRegSetKeyValue(hd, "owner_name", name, strlen(name)) == 0) {
				printf("Set registry value\n");
				sceRegFlushCategory(hd);
			}
			sceRegCloseCategory(hd);
		}
		sceRegFlushRegistry(h);
		sceRegCloseRegistry(h);
	}

	pspSdkSetK1(k1);
	return 0;
}

int sceUpdateDownloadSetVersionPatched(int version) {
	int k1 = pspSdkSetK1(0);

	int (* sceUpdateDownloadSetVersion)(int version) = (void *)FindProc("SceUpdateDL_Library", "sceLibUpdateDL", 0xC1AF1076);
	int (* sceUpdateDownloadSetUrl)(const char *url) = (void *)FindProc("SceUpdateDL_Library", "sceLibUpdateDL", 0xF7E66CB4);

	sceUpdateDownloadSetUrl("http://adrenaline.grayjack.me/psp-updatelist.txt");
	int res = sceUpdateDownloadSetVersion(sctrlSEGetVersion());

	pspSdkSetK1(k1);
	return res;
}

int fakeParamInexistance(void) {
	return 0x80120005;
}

int LoadExecVSHCommonPatched(int apitype, char *file, struct SceKernelLoadExecVSHParam *param, int unk2) {
	logmsg4("Executing %s(0x%04X, %s)\n", __func__, apitype, file);

	// ISO
	if (is_iso_eboot(file) || is_iso_dlc(file) || is_iso_update(file)) {
		u32 k1 = pspSdkSetK1(0);
		int result = vpbp_loadexec(file, param);
		pspSdkSetK1(k1);
		return result;
	}

	int k1 = pspSdkSetK1(0);

	sctrlSESetUmdFile("");

	// Enable 1.50 homebrews boot
	char *perc = strchr(param->argp, '%');
	if (perc) {
		strcpy(perc, perc + 1);
		file = param->argp;
		param->args = strlen(param->argp) + 1; //Update length
	}

	pspSdkSetK1(k1);

	return sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
}

int homebrewloadexec(char * file, struct SceKernelLoadExecVSHParam * param) {
	logmsg4("Executing %s: %s\n", __func__, file);
	sctrlSESetBootConfFileIndex(BOOT_NORMAL);
	sctrlSESetUmdFile("");

	// fix 1.50 homebrew
	char *perc = strchr(param->argp, '%');
	if (perc) {
		strcpy(perc, perc + 1);
		file = param->argp;
	}

	int res = 0;
	//forward to ms0 handler
	if(strncmp(file, "ms", 2) == 0) {
		res = sctrlKernelLoadExecVSHMs2(file, param);
	} else {
		res = sctrlKernelLoadExecVSHEf2(file, param);
	}

	return res;
}

int umdemuloadexec(char * file, struct SceKernelLoadExecVSHParam * param) {
	logmsg4("Executing %s: %s\n", __func__, file);

	//result
	int result = -1;

	//virtual iso eboot detected
	if (is_iso_eboot(file)) {
		u32 k1 = pspSdkSetK1(0);
		result = vpbp_loadexec(file, param);
		pspSdkSetK1(k1);
		return result;
	}

	sctrlSESetBootConfFileIndex(BOOT_NORMAL);
	sctrlSESetUmdFile("");

	static int apitypes[2][2] = {
		{SCE_APITYPE_UMD_EMU_MS1, SCE_APITYPE_UMD_EMU_MS2},
		{SCE_APITYPE_UMD_EMU_EF1, SCE_APITYPE_UMD_EMU_EF2}
	};

	int apitype = apitypes
		[ (strncmp(file, "ms", 2) == 0)? 0:1 ]
		[ (strstr(param->argp, "/PBOOT.PBP") == NULL)? 0:1 ];

	//forward
	logmsg4("%s: [DEBUG]: Fowarded to LoadExec(0x%04X, %s)\n", __func__, apitype, file);
	return sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
}

int umdLoadExec(char * file, struct SceKernelLoadExecVSHParam * param) {
	logmsg4("Executing %s: %s\n", __func__, file);

	//result
	int ret = 0;

	sctrlSESetDiscType(PSP_UMD_TYPE_GAME);

	if(g_psp_model == PSP_GO) {
		char devicename[20];
		int apitype = SCE_APITYPE_UMD_EMU_MS1;

		file = g_mounted_iso;
		ret = get_device_name(devicename, sizeof(devicename), file);

		if (ret == 0 && 0 == strcasecmp(devicename, "ef0:")) {
			apitype = SCE_APITYPE_UMD_EMU_EF1;
		}

		param->key = "umdemu";
		// Set umd_mode
		if (g_cfw_config.umd_mode == MODE_INFERNO) {
			logmsg2("[INFO]: Launching with Inferno Driver\n");
			sctrlSESetBootConfFileIndex(BOOT_INFERNO);
		} else if (g_cfw_config.umd_mode == MODE_MARCH33) {
			logmsg2("[INFO]: Launching with March33 Driver\n");
			sctrlSESetBootConfFileIndex(BOOT_MARCH33);
		} else if (g_cfw_config.umd_mode == MODE_NP9660) {
			logmsg2("[INFO]: Launching with NP9660 Driver\n");
			sctrlSESetBootConfFileIndex(BOOT_NP9660);
		}

		ret = sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
	} else {
		sctrlSESetBootConfFileIndex(BOOT_NORMAL);
		sctrlSESetUmdFile("");
		int apitype = (strstr(param->argp, "/PBOOT.PBP")==NULL)? SCE_APITYPE_UMD:SCE_APITYPE_UMD2;
		ret = sctrlKernelLoadExecVSHWithApitype(apitype, file, param);
	}


	return ret;
}

int umdLoadExecUpdater(char * file, struct SceKernelLoadExecVSHParam * param) {
	logmsg4("Executing %s: %s\n", __func__, file);

	//result
	int ret = 0;
	sctrlSESetBootConfFileIndex(BOOT_UPDATERUMD);
	sctrlSESetDiscType(PSP_UMD_TYPE_GAME);
	ret = sceKernelLoadExecVSHDiscUpdater(file, param);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////
// MODULE PATCHERS
////////////////////////////////////////////////////////////////////////////////

void PatchVshMain(SceModule *mod) {
	u32 text_addr = mod->text_addr;

	// Allow old sfo's
	MAKE_NOP(text_addr + 0x122B0);
	MAKE_NOP(text_addr + 0x12058); // DISC_ID
	MAKE_NOP(text_addr + 0x12060); // DISC_ID

	u32 fakeparam_syscall = SYSCALL(sctrlKernelQuerySystemCall(fakeParamInexistance));
	MAKE_INSTRUCTION(text_addr + 0x119C0, fakeparam_syscall);
	MAKE_INSTRUCTION(text_addr + 0x121A4, fakeparam_syscall);
	MAKE_INSTRUCTION(text_addr + 0x12BA4, fakeparam_syscall);
	MAKE_INSTRUCTION(text_addr + 0x13288, fakeparam_syscall);

	sctrlHENHookImportByNID(mod, "sceVshBridge", 0x21D4D038, homebrewloadexec, 0);
	sctrlHENHookImportByNID(mod, "sceVshBridge", 0xE533E98C, homebrewloadexec, 0);

	sctrlHENHookImportByNID(mod, "sceVshBridge", 0xB8B07CAF, umdemuloadexec, 0);
	sctrlHENHookImportByNID(mod, "sceVshBridge", 0x791FCD43, umdemuloadexec, 0);
	sctrlHENHookImportByNID(mod, "sceVshBridge", 0x01730088, umdemuloadexec, 0);
	sctrlHENHookImportByNID(mod, "sceVshBridge", 0x5B7F3339, umdemuloadexec, 0);

	sctrlHENHookImportByNID(mod, "sceVshBridge", 0x63E69956, umdLoadExec, 0);
	sctrlHENHookImportByNID(mod, "sceVshBridge", 0x0C0D5913, umdLoadExec, 0);
	// sctrlHENHookImportByNID(mod, "sceVshBridge", 0x81682A40, umdLoadExecUpdater, 0);

	if (g_cfw_config.skip_game_boot_logo) {
		// Disable sceDisplaySetHoldMode
		MAKE_NOP(text_addr + 0xCA88);
	}

	if (g_cfw_config.extended_colors == 1) {
		VWRITE16(text_addr + 0x3174A, 0x1000);
	}

	sctrlFlushCache();
}

void PatchSysconfPlugin(SceModule* mod) {
	static wchar_t macinfo[] = L"00:00:00:00:00:00";

	u32 text_addr = mod->text_addr;

	int version = sctrlSEGetVersion();
	int version_major = version >> 24;
	int version_minor = version >> 16 & 0xFF;
	int version_micro = version >> 8 & 0xFF;

	char verinfo[50] = {0};
	sprintf(verinfo, "6.61 Adrenaline-%d.%d.%d", version_major, version_minor, version_micro );

	ascii2utf16( (char*)((void *)text_addr + 0x2A62C), verinfo);

	MAKE_INSTRUCTION(text_addr + 0x192E0, 0x3C020000 | ((u32)(text_addr + 0x2A62C) >> 16));
	MAKE_INSTRUCTION(text_addr + 0x192E4, 0x34420000 | ((u32)(text_addr + 0x2A62C) & 0xFFFF));

	if (g_cfw_config.hide_mac_addr) {
		memcpy((void *)text_addr + 0x2E9A0, macinfo, sizeof(macinfo));
	}

	// Allow slim colors
	if (g_cfw_config.extended_colors != 0) {
		MAKE_INSTRUCTION(text_addr + 0x76EC, VREAD32(text_addr + 0x76F0));
		MAKE_INSTRUCTION(text_addr + 0x76F0, LI_V0(1));
	}

	// Dummy all vshbridge usbstor functions
	MAKE_INSTRUCTION(text_addr + 0xCD78, LI_V0(1));   // sceVshBridge_ED978848 - vshUsbstorMsSetWorkBuf
	MAKE_INSTRUCTION(text_addr + 0xCDAC, MOVE_V0_ZR); // sceVshBridge_EE59B2B7
	MAKE_INSTRUCTION(text_addr + 0xCF0C, MOVE_V0_ZR); // sceVshBridge_6032E5EE - vshUsbstorMsSetProductInfo
	MAKE_INSTRUCTION(text_addr + 0xD218, MOVE_V0_ZR); // sceVshBridge_360752BF - vshUsbstorMsSetVSHInfo

	// Dummy LoadUsbModules, UnloadUsbModules
	MAKE_DUMMY_FUNCTION(text_addr + 0xCC70, 0);
	MAKE_DUMMY_FUNCTION(text_addr + 0xD2C4, 0);

	// Redirect USB functions
	REDIRECT_FUNCTION(text_addr + 0xAE9C, sctrlHENMakeSyscallStub(InitUsbPatched));
	REDIRECT_FUNCTION(text_addr + 0xAFF4, sctrlHENMakeSyscallStub(ShutdownUsbPatched));
	REDIRECT_FUNCTION(text_addr + 0xB4A0, sctrlHENMakeSyscallStub(GetUsbStatusPatched));

	// Ignore wait thread end failure
	MAKE_NOP(text_addr + 0xB264);

	// Do not set nickname to PXXX on initial setup/reset
	REDIRECT_FUNCTION(text_addr + 0x1520, sctrlHENMakeSyscallStub(SetDefaultNicknamePatched));

	sctrlFlushCache();
}

void PatchGamePlugin(SceModule* mod) {
	u32 text_addr = mod->text_addr;

	// Allow homebrew launch
	MAKE_DUMMY_FUNCTION(text_addr + 0x20528, 0);

	// Allow PSX launch
	MAKE_DUMMY_FUNCTION(text_addr + 0x20E6C, 0);

	// Allow custom multi-disc PSX
	MAKE_NOP(text_addr + 0x14850);

	// if check patch
	MAKE_INSTRUCTION(text_addr + 0x20620, MOVE_V0_ZR);

	if (g_cfw_config.hide_pic0pic1 != PICS_OPT_DISABLED) {
		if (g_cfw_config.hide_pic0pic1 == PICS_OPT_BOTH || g_cfw_config.hide_pic0pic1 == PICS_OPT_PIC0_ONLY) {
			MAKE_INSTRUCTION(text_addr + 0x1D858, 0x00601021);
		}

		if (g_cfw_config.hide_pic0pic1 == PICS_OPT_BOTH || g_cfw_config.hide_pic0pic1 == PICS_OPT_PIC1_ONLY) {
			MAKE_INSTRUCTION(text_addr + 0x1D864, 0x00601021);
		}
	}

	if (g_cfw_config.skip_game_boot_logo) {
		MAKE_CALL(text_addr + 0x19130, text_addr + 0x194B0);
		MAKE_INSTRUCTION(text_addr + 0x19134, 0x24040002);
	}

	sctrlFlushCache();
}

void PatchUpdatePlugin(SceModule* mod) {
	u32 text_addr = mod->text_addr;

	MAKE_CALL(text_addr + 0x82A8, sctrlHENMakeSyscallStub(sceUpdateDownloadSetVersionPatched));
	sctrlFlushCache();
}

void PatchLoadExec() {
	SceModule *mod = sceKernelFindModuleByName("sceLoadExec");
	u32 text_addr = mod->text_addr;

	MAKE_CALL(text_addr + 0x1DC0, LoadExecVSHCommonPatched); //sceKernelLoadExecVSHMs2
	MAKE_CALL(text_addr + 0x1BE0, LoadExecVSHCommonPatched); //sceKernelLoadExecVSHMsPboot
	MAKE_CALL(text_addr + 0x1BB8, LoadExecVSHCommonPatched); //sceKernelLoadExecVSHUMDEMUPboot

	sctrlFlushCache();
}

void PatchMsVideoMainPlugin(SceModule* mod) {
	u32 text_addr = mod->text_addr;
	u32 top_addr = text_addr + mod->text_size;
	int patches = 10;

	for (u32 addr=text_addr; addr<top_addr && patches; addr+=4){
		u32 data = VREAD32(addr);
		if ((data & 0xFF00FFFF) == 0x34002C00){
			/* Patch resolution limit to (130560) pixels (480x272) */
			VWRITE16(addr, 0xFE00);
			patches--;
		}
		else if (data == 0x2C420303 || data == 0x2C420FA1){
			/* Patch bitrate limit (increase to 16384+2) */
			VWRITE16(addr, 0x4003);
			patches--;
		}
	}

	sctrlFlushCache();
}

void PatchReadBufferPositive() {
	SceModule *mod = sceKernelFindModuleByName("sceVshBridge_Driver");
	MAKE_CALL(mod->text_addr + 0x25C, sceCtrlReadBufferPositivePatched);
	sctrlHENPatchSyscall(K_EXTRACT_IMPORT(&sceCtrlReadBufferPositive), sceCtrlReadBufferPositivePatched);
	sctrlFlushCache();
}
