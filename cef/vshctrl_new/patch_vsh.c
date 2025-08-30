
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include <common.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <pspctrl.h>
#include <pspsysmem_kernel.h>
#include "utils.h"


#include "virtual_pbp.h"
#include "patched_io.h"

extern AdrenalineConfig config;
extern u32 psp_model;
extern SceUID modid;
extern u32 firsttick;
extern u8 set;
extern SceCtrlData *last_control_data;
static STMOD_HANDLER previous;

int (* vshmenu_ctrl)(SceCtrlData *pad_data, int count);

//wchar_t verinfo[] = L"6.61 Adrenaline-     ";
wchar_t macinfo[] = L"00:00:00:00:00:00";

int sceKernelQuerySystemCall(void *function);

u32 MakeSyscallStub(void *function) {
	SceUID block_id = sceKernelAllocPartitionMemory(PSP_MEMORY_PARTITION_USER, "", PSP_SMEM_High, 2 * sizeof(u32), NULL);
	u32 stub = (u32)sceKernelGetBlockHeadAddr(block_id);
	MAKE_SYSCALL_FUNCTION(stub, sceKernelQuerySystemCall(function));
	return stub;
}

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


void PatchSysconfPlugin(SceModule2 *mod) {
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

	if (config.hide_mac_addr) {
		memcpy((void *)text_addr + 0x2E9A0, macinfo, sizeof(macinfo));
	}

	// Allow slim colors
	if (config.extended_colors != 0) {
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
	REDIRECT_FUNCTION(text_addr + 0xAE9C, MakeSyscallStub(InitUsbPatched));
	REDIRECT_FUNCTION(text_addr + 0xAFF4, MakeSyscallStub(ShutdownUsbPatched));
	REDIRECT_FUNCTION(text_addr + 0xB4A0, MakeSyscallStub(GetUsbStatusPatched));

	// Ignore wait thread end failure
	MAKE_NOP(text_addr + 0xB264);

	// Do not set nickname to PXXX on initial setup/reset
	REDIRECT_FUNCTION(text_addr + 0x1520, MakeSyscallStub(SetDefaultNicknamePatched));
}

void PatchGamePlugin(SceModule2 *mod) {
	u32 text_addr = mod->text_addr;

	// Allow homebrew launch
	MAKE_DUMMY_FUNCTION(text_addr + 0x20528, 0);

	// Allow PSX launch
	MAKE_DUMMY_FUNCTION(text_addr + 0x20E6C, 0);

	// Allow custom multi-disc PSX
	MAKE_NOP(text_addr + 0x14850);

	// if check patch
	MAKE_INSTRUCTION(text_addr + 0x20620, MOVE_V0_ZR);

	if (config.hide_pic0pic1) {
		MAKE_INSTRUCTION(text_addr + 0x1D858, 0x00601021);
		MAKE_INSTRUCTION(text_addr + 0x1D864, 0x00601021);
	}

	if (config.skip_game_boot_logo) {
		MAKE_CALL(text_addr + 0x19130, text_addr + 0x194B0);
		MAKE_INSTRUCTION(text_addr + 0x19134, 0x24040002);
	}
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

static void PatchUpdatePlugin(SceModule2 *mod) {
	u32 text_addr = mod->text_addr;
	MAKE_CALL(text_addr + 0x82A8, MakeSyscallStub(sceUpdateDownloadSetVersionPatched));
}

static void PatchMsVideoMainPlugin(SceModule2* mod) {
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
}

void PatchVshMain(SceModule2 *mod) {
	u32 text_addr = mod->text_addr;

	// Allow old sfo's
	MAKE_NOP(text_addr + 0x122B0);
	MAKE_NOP(text_addr + 0x12058); // DISC_ID
	MAKE_NOP(text_addr + 0x12060); // DISC_ID

	int use_static_patch = 0;
	int res;
	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0x21D4D038, homebrewloadexec, 0);
	if (res < 0) use_static_patch = 1;
	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0xE533E98C, homebrewloadexec, 0);
	if (res < 0) use_static_patch = 1;

	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0xB8B07CAF, umdemuloadexec, 0);
	if (res < 0) use_static_patch = 1;
	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0x791FCD43, umdemuloadexec, 0);
	if (res < 0) use_static_patch = 1;
	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0x01730088, umdemuloadexec, 0);
	if (res < 0) use_static_patch = 1;
	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0x5B7F3339, umdemuloadexec, 0);
	if (res < 0) use_static_patch = 1;

	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0x63E69956, umdLoadExec, 0);
	if (res < 0) use_static_patch = 1;
	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0x0C0D5913, umdLoadExec, 0);
	if (res < 0) use_static_patch = 1;
	res = sctrlHENHookImportByNID(mod, "sceVshBridge", 0x81682A40, umdLoadExecUpdater, 0);
	if (res < 0) use_static_patch = 1;

	if (use_static_patch) {
		SceModule2 *mod = sceKernelFindModuleByName("sceLoadExec");
		u32 text_addr = mod->text_addr;

		MAKE_CALL(text_addr + 0x1DC0, homebrewLoadExec); //sceKernelLoadExecVSHMs2
		MAKE_CALL(text_addr + 0x1BE0, umdLoadExec2); //sceKernelLoadExecVSHMsPboot
		MAKE_CALL(text_addr + 0x1BB8, umdemuLoadExec); //sceKernelLoadExecVSHUMDEMUPboot
	}

	if (config.skip_game_boot_logo) {
		// Disable sceDisplaySetHoldMode
		MAKE_NOP(text_addr + 0xCA88);
	}

	if (config.extended_colors == 1) {
		VWRITE16(text_addr + 0x3174A, 0x1000);
	}
}

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

static void hookPatchedIO() {
	for (int i = 0; i < (sizeof(import_patch) / sizeof(ImportPatch)); i++) {
		sctrlHENPatchSyscall(K_EXTRACT_IMPORT(import_patch[i].import), import_patch[i].patched);
	}
}

int cpu_list[] = { 0, 20, 75, 100, 133, 222, 266, 300, 333 };
int bus_list[] = { 0, 10, 37,  50,  66, 111, 133, 150, 166 };

#define N_CPU (sizeof(cpu_list) / sizeof(int))

int sceCtrlReadBufferPositivePatched(SceCtrlData *pad_data, int count) {
	int res = sceCtrlReadBufferPositive(pad_data, count);
	int k1 = pspSdkSetK1(0);

	last_control_data = pad_data;

	if (!set && config.vsh_cpu_speed != 0) {
		u32 curtick = sceKernelGetSystemTimeLow();
		curtick -= firsttick;

		u32 t = (u32)curtick;
		if (t >= (10 * 1000 * 1000)) {
			set = 1;
			SetSpeed(cpu_list[config.vsh_cpu_speed % N_CPU], bus_list[config.vsh_cpu_speed % N_CPU]);
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
					sceKernelDelayThread(4000);
					sceKernelStartModule(modid, 0, NULL, NULL, NULL);
					pad_data->Buttons &= ~PSP_CTRL_SELECT;
				}
			}
		}
	} else {
		if (vshmenu_ctrl) {
			vshmenu_ctrl(pad_data, count);
		} else if (modid >= 0) {
			if (sceKernelStopModule(modid, 0, NULL, NULL, NULL) >= 0) {
				sceKernelUnloadModule(modid);
			}
		}
	}

	pspSdkSetK1(k1);
	return res;
}

int vctrlVSHRegisterVshMenu(int (* ctrl)(SceCtrlData *, int)) {
	int k1 = pspSdkSetK1(0);
	vshmenu_ctrl = (void *)((u32)ctrl | 0x80000000);
	pspSdkSetK1(k1);
	return 0;
}

int vctrlVSHExitVSHMenu(AdrenalineConfig *conf) {
	int k1 = pspSdkSetK1(0);
	int oldspeed = config.vsh_cpu_speed;

	vshmenu_ctrl = NULL;
	memcpy(&config, conf, sizeof(AdrenalineConfig));
	sctrlSEApplyConfig(&config);

	if (set) {
		if (config.vsh_cpu_speed != oldspeed) {
			if (config.vsh_cpu_speed) {
				SetSpeed(cpu_list[config.vsh_cpu_speed % N_CPU], bus_list[config.vsh_cpu_speed % N_CPU]);
			} else {
				SetSpeed(222, 111);
			}
		}
	}

	pspSdkSetK1(k1);
	return 0;
}

static void patchReadBufferPositive() {
	SceModule2 *mod = sceKernelFindModuleByName("sceVshBridge_Driver");
	MAKE_CALL(mod->text_addr + 0x25C, sceCtrlReadBufferPositivePatched);
	sctrlHENPatchSyscall(K_EXTRACT_IMPORT(&sceCtrlReadBufferPositive), sceCtrlReadBufferPositivePatched);
}

int OnModuleStart(SceModule2 *mod) {
	char* modname = mod->modname;

	if (strcmp(modname, "vsh_module") == 0) {
		PatchVshMain(mod);
		patchReadBufferPositive();
	} else if (strcmp(modname, "sysconf_plugin_module") == 0) {
		PatchSysconfPlugin(mod);
	} else if (strcmp(modname, "game_plugin_module") == 0) {
		PatchGamePlugin(mod);
	} else if (strcmp(modname, "update_plugin_module") == 0) {
		PatchUpdatePlugin(mod);
	} else if (0 == strcmp(modname, "msvideo_main_plugin_module")) {
		PatchMsVideoMainPlugin(mod);
	}

	sctrlFlushCache();

	if (!previous)
		return 0;

	return previous(mod);
}

int initPatch() {
	previous = sctrlHENSetStartModuleHandler(OnModuleStart);
	vpbp_init();
	hookPatchedIO();

	return 0;
}