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
#include <pspintrman.h>

#include "main.h"
#include "adrenaline.h"

int lzo1x_decompress(void* source, unsigned src_len, void* dest, unsigned* dst_len, void*);
int LZ4_decompress_fast(const char* source, char* dest, int outputSize);


int sctrlKernelSetUserLevel(int level) {
	int k1 = pspSdkSetK1(0);
	int res = sceKernelGetUserLevel();

	SceModule2 *mod = sceKernelFindModuleByName("sceThreadManager");
	u32 text_addr = mod->text_addr;

	u32 high = (((u32)VREAD16(text_addr + 0x358)) << 16);
	u32 low = ((u32)VREAD16(text_addr + 0x35C));

	if (low & 0x8000)
		high -= 0x10000;

	u32 *thstruct = (u32 *)VREAD32(high | low);
	thstruct[0x14/4] = (level ^ 8) << 28;

	pspSdkSetK1(k1);
	return res;
}

int sctrlHENIsSE() {
	return 1;
}

int sctrlHENIsDevhook() {
	return 0;
}

int sctrlHENGetVersion() {
	return 0x00001000;
}

int sctrlSEGetVersion() {
	return ADRENALINE_VERSION;
}

PspIoDrv *sctrlHENFindDriver(char *drvname) {
	int k1 = pspSdkSetK1(0);

	SceModule2 *mod = sceKernelFindModuleByName("sceIOFileManager");
	u32 text_addr = mod->text_addr;

	u32 *(* GetDevice)(char *) = NULL;

	for (int i = 0; i < mod->text_size; i += 4) {
		u32 addr = text_addr + i;
		u32 data = VREAD32(addr);

		if (data == 0xA2200000) {
			GetDevice = (void *)K_EXTRACT_CALL(addr + 4);
			break;
		}
	}

	if (!GetDevice) {
		pspSdkSetK1(k1);
		return 0;
	}

	u32 *u = GetDevice(drvname);
	if (!u) {
		pspSdkSetK1(k1);
		return 0;
	}

	pspSdkSetK1(k1);

	return (PspIoDrv *)u[1];
}

int sctrlKernelExitVSH(struct SceKernelLoadExecVSHParam *param) {
	int k1 = pspSdkSetK1(0);
	int res = sceKernelExitVSHVSH(param);
	pspSdkSetK1(k1);
	return res;
}

int sctrlKernelLoadExecVSHWithApitype(int apitype, const char *file, struct SceKernelLoadExecVSHParam *param) {
	int k1 = pspSdkSetK1(0);

	SceModule2 *mod = sceKernelFindModuleByName("sceLoadExec");
	u32 text_addr = mod->text_addr;

	int (* LoadExecVSH)(int apitype, const char *file, struct SceKernelLoadExecVSHParam *param, int unk2) = (void *)text_addr + 0x23D0;

	int res = LoadExecVSH(apitype, file, param, 0x10000);
	pspSdkSetK1(k1);
	return res;
}

int sctrlKernelLoadExecVSHMs1(const char *file, struct SceKernelLoadExecVSHParam *param) {
	return sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_MS1, file, param);
}

int sctrlKernelLoadExecVSHMs2(const char *file, struct SceKernelLoadExecVSHParam *param) {
	return sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_MS2, file, param);
}

int sctrlKernelLoadExecVSHMs3(const char *file, struct SceKernelLoadExecVSHParam *param) {
	return sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_MS3, file, param);
}

int sctrlKernelLoadExecVSHMs4(const char *file, struct SceKernelLoadExecVSHParam *param) {
	return sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_MS4, file, param);
}

int sctrlKernelLoadExecVSHDisc(const char *file, struct SceKernelLoadExecVSHParam *param) {
	return sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_DISC, file, param);
}

int sctrlKernelLoadExecVSHDiscUpdater(const char *file, struct SceKernelLoadExecVSHParam *param) {
	return sctrlKernelLoadExecVSHWithApitype(PSP_INIT_APITYPE_DISC_UPDATER, file, param);
}

int sctrlKernelQuerySystemCall(void *function) {
	int k1 = pspSdkSetK1(0);
	int res = sceKernelQuerySystemCall(function);
	pspSdkSetK1(k1);
	return res;
}

void sctrlHENPatchSyscall(u32 addr, void *newaddr) {
	void *ptr = NULL;
	asm("cfc0 %0, $12\n" : "=r"(ptr));

	if (NULL == ptr) {
		return;
	}

	u32 *syscalls = (u32 *)(ptr + 0x10);

	for (int i = 0; i < 0x1000; i++) {
		if ((syscalls[i] & 0x0FFFFFFF) == (addr & 0x0FFFFFFF)) {
			syscalls[i] = (u32)newaddr;
		}
	}
}

void SetUmdFile(char *file) __attribute__((alias("sctrlSESetUmdFile")));
void sctrlSESetUmdFile(char *file) {
	strncpy(rebootex_config.umdfilename, file, 255);
}


char *GetUmdFile(void) __attribute__((alias("sctrlSEGetUmdFile")));
char *sctrlSEGetUmdFile() {
	return rebootex_config.umdfilename;
}

int sctrlSEMountUmdFromFile(char *file, int noumd, int isofs) {
	int k1 = pspSdkSetK1(0);

	SetUmdFile(file);

	pspSdkSetK1(k1);
	return 0;
}

int sctrlSEGetBootConfBootFileIndex() {
	return rebootex_config.bootfileindex;
}

void sctrlSESetBootConfFileIndex(int index) {
	rebootex_config.bootfileindex = index;
}

void sctrlSESetDiscType(int type) {
	rebootex_config.iso_disc_type = type;
}

int sctrlSEGetDiscType(void) {
	return rebootex_config.iso_disc_type;
}

void sctrlHENLoadModuleOnReboot(char *module_after, void *buf, int size, int flags) {
	rebootex_config.module_after = module_after;
	rebootex_config.buf = buf;
	rebootex_config.size = size;
	rebootex_config.flags = flags;
}

int sctrlGetUsbState() {
	return SendAdrenalineCmd(ADRENALINE_VITA_CMD_GET_USB_STATE);
}

int sctrlStartUsb() {
	return SendAdrenalineCmd(ADRENALINE_VITA_CMD_START_USB);
}

int sctrlStopUsb() {
	return SendAdrenalineCmd(ADRENALINE_VITA_CMD_STOP_USB);
}

int sctrlRebootDevice() {
	// can't do it separately, because user might have old systemctrl
	// but this is used only by updater, so that's ok
	SendAdrenalineCmd(ADRENALINE_VITA_CMD_UPDATE);
	return SendAdrenalineCmd(ADRENALINE_VITA_CMD_POWER_REBOOT);
}

u32 sctrlKernelRand(void) {
	u32 k1 = pspSdkSetK1(0);

	unsigned char * alloc = oe_malloc(20 + 4);

	// Allocation Error
	if(alloc == NULL) __asm__ volatile ("break");

	// Align Buffer to 4 Bytes
	unsigned char * buffer = (void *)(((u32)alloc & (~(4-1))) + 4);

	// KIRK Random Generator Opcode
	enum {
		KIRK_PRNG_CMD=0xE,
	};

	// Create 20 Random Bytes
	sceUtilsBufferCopyWithRange(buffer, 20, NULL, 0, KIRK_PRNG_CMD);

	u32 random = *(u32 *)buffer;

	oe_free(alloc);
	pspSdkSetK1(k1);

	return random;
}


int sctrlDeflateDecompress(void* dest, void* src, int size){
    u32 k1 = pspSdkSetK1(0);
    int ret = sceKernelDeflateDecompress(dest, size, src, 0);
    pspSdkSetK1(k1);
    return ret;
}

int sctrlGzipDecompress(void* dest, void* src, int size){
	u32 k1 = pspSdkSetK1(0);
	int ret = sceKernelGzipDecompress(dest, size, src, 0);
	pspSdkSetK1(k1);
	return ret;
}

int sctrlLZ4Decompress(void* dest, const void* src, int size) {
	return LZ4_decompress_fast(src, dest, size);
}

int sctrlLzoDecompress(void* dest, unsigned* dst_size, void* src, unsigned src_size) {
	return lzo1x_decompress(src, src_size, dest, dst_size, 0);
}