#ifndef PATCH_OFFSET_H
#define PATCH_OFFSET_H

#include <common.h>

#define FW_661 0x06060110

typedef struct PatchOffset {
	u32 fw_version;
	u32 StoreFd;
	u32 Data1;
	u32 Data2;
	u32 Data3;
	u32 Data4;
	u32 Data5;
	u32 InitForKernelCall;
	u32 Func1;
	u32 Func2;
	u32 Func3;
	u32 Func4;
	u32 Func5;
	u32 Func6;
	u32 sceIoClose;
	u32 sceKernelCreateThread;
	u32 sceKernelStartThread;
} PatchOffset;

extern PatchOffset *g_offs;

#endif