#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspthreadman_kernel.h>
#include <psputilsforkernel.h>
#include <stdio.h>
#include <string.h>

#include <systemctrl.h>
#include <systemctrl_se.h>

#include <common.h>

#define _ADRENALINE_LOG_IMPL_
#include <adrenaline_log.h>

#include "galaxy.h"
// #include "systemctrl_private.h"
// #include "utils.h"
// #include "printk.h"
#include "patch_offset.h"
// #include "lz4.h"

// jal addr
#define JAL(f) (0x0C000000 | (((u32)(f) >> 2) & 0x03ffffff))
// j addr
#define JUMP(f) (0x08000000 | (((u32)(f) >> 2) & 0x03ffffff))

#define CISO_IDX_BUFFER_SIZE 0x200
#define CISO_DEC_BUFFER_SIZE 0x2000

PSP_MODULE_INFO("PROGalaxyController", 0x1006, 1, 1);

extern int get_total_block(void);
extern int sceKernelSetQTGP3(void *unk0);
extern int sceKernelApplicationType(void);

u32 psp_fw_version;
u32 psp_model;

// 0x00000F24
SceUID g_SceNpUmdMount_thid = -1;

// 0x00000F28
SceUID g_iso_fd = -1;

// 0x00000F2C
int g_total_blocks = -1;

// 0x00000F40
int g_iso_opened = 0;

// 0x00000F44
int g_is_ciso = 0;

// 0x00000F80
void *g_ciso_block_buf = NULL;

// 0x00000F84, size CISO_DEC_BUFFER_SIZE + (1 << g_CISO_hdr.align), align 64
void *g_ciso_dec_buf = NULL;

// 0x00000FC0
u32 g_CISO_idx_cache[CISO_IDX_BUFFER_SIZE/4] __attribute__((aligned(64)));

// 0x000011C0
static u32 g_ciso_dec_buf_offset = (u32)-1;

static int g_ciso_dec_buf_size = 0;

// 0x000011C4
int g_CISO_cur_idx = 0;

struct CISO_header {
	u8 magic[4];  // 0
	u32 header_size;  // 4
	u64 total_bytes; // 8
	u32 block_size; // 16
	u8 ver; // 20
	u8 align;  // 21
	u8 rsv_06[2];  // 22
};

// 0x000011C8
static struct CISO_header g_CISO_hdr __attribute__((aligned(64)));

// 0x000011E0
u32 ciso_total_block = 0;

// 0x0000120C
u32 g_sceNp9660_driver_text_addr = 0;

// 0x00001200
u32 g_func_1200 = 0;

// 0x00001204
u32 g_data_1204 = 0;

// 0x00001208
u32 g_func_1208 = 0;

// 0x0000121C
u32 g_func_121C = 0;

// 0x00001220
char *g_iso_fn = NULL;

// 0x00000e10
u8 g_umddata[16] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static int lz4_compressed = 0;

// 0x00000000
SceUID myKernelCreateThread(const char *name, SceKernelThreadEntry entry, int initPriority, int stackSize, SceUInt attr, SceKernelThreadOptParam *option) {
	SceUID thid;

	thid = sceKernelCreateThread(name, entry, initPriority,
			stackSize, attr, option);

	if (!strncmp(name, "SceNpUmdMount", 13)) {
		g_SceNpUmdMount_thid = thid;
		logmsg("g_SceNpUmdMount_thid at 0x%08x\n", thid);
	}

	return thid;
}

// 670
int cso_open(SceUID fd) {
	int ret;
	u32 *magic;

	g_CISO_hdr.magic[0] = '\0';
	g_ciso_dec_buf_offset = (u32)-1;
	g_ciso_dec_buf_size = 0;

	sceIoLseek(fd, 0, PSP_SEEK_SET);
	ret = sceIoRead(fd, &g_CISO_hdr, sizeof(g_CISO_hdr));

	if (ret != sizeof(g_CISO_hdr)) {
		ret = -1;
		logmsg("%s: -> %d\n", __func__, ret);
		goto exit;
	}

	magic = (u32*)g_CISO_hdr.magic;

	if (*magic == 0x4F534943 || *magic == 0x4F53495A) { // CISO or ZISO
		lz4_compressed = (*magic == 0x4F53495A) ? 1 : 0;
		g_CISO_cur_idx = -1;
		ciso_total_block = g_CISO_hdr.total_bytes / g_CISO_hdr.block_size;
		logmsg("%s: total block %d\n", __func__, (int)ciso_total_block);

		if (g_ciso_dec_buf == NULL) {
			g_ciso_dec_buf = oe_malloc(CISO_DEC_BUFFER_SIZE + (1 << g_CISO_hdr.align) + 64);

			if (g_ciso_dec_buf == NULL) {
				ret = -2;
				logmsg("%s: -> %d\n", __func__, ret);
				goto exit;
			}

			if ((u32)g_ciso_dec_buf & 63)
				g_ciso_dec_buf = (void*)(((u32)g_ciso_dec_buf & (~63)) + 64);
		}

		if (g_ciso_block_buf == NULL) {
			g_ciso_block_buf = oe_malloc(ISO_SECTOR_SIZE);

			if (g_ciso_block_buf == NULL) {
				ret = -3;
				logmsg("%s: -> %d\n", __func__, ret);
				goto exit;
			}
		}

		ret = 0;
	} else {
		ret = 0x8002012F;
	}

exit:
	return ret;
}

// 184
int open_iso(void) {
	int ret;

	g_iso_opened = 0;
	sceIoClose(g_iso_fd);

	while ( 1 ) {
		g_iso_fd = sceIoOpen(g_iso_fn, 0x000F0000 | PSP_O_RDONLY, 0);

		if (g_iso_fd >= 0) {
			break;
		}

		logmsg("%s: ioopen %s -> 0x%08X\n", __func__, g_iso_fn, g_iso_fd);
		sceKernelDelayThread(10000);
	}

	// 6.30: 0x00004D50
	// 6.20: 0x00004C70 in sub_00004BB4
	// see 0x00004618 in sub_000045CC
	// see 0x00003428 in sub_000033F4, memset(0x00005BA4, 0, 60)
	_sw(g_iso_fd, g_sceNp9660_driver_text_addr + g_offs->StoreFd);
	g_is_ciso = 0;
	ret = cso_open(g_iso_fd);

	if (ret >= 0) {
		g_is_ciso = 1;
		logmsg("%s: g_is_ciso = 1\n", __func__);
	}

	g_total_blocks = get_total_block();
	g_iso_opened = 1;

	return 0;
}

int sub_00000588(void) {
	int intr;
	void (*ptr)(u32) = (void*)g_func_1200;

	(*ptr)(0);
	open_iso();
	intr = sceKernelCpuSuspendIntr();

	/* sceUmdManGetUmdDiscInfo patch */
	_sw(0xE0000800, g_sceNp9660_driver_text_addr + g_offs->Data1);
	_sw(0x00000009, g_sceNp9660_driver_text_addr + g_offs->Data2);
	_sw(g_total_blocks, g_sceNp9660_driver_text_addr + g_offs->Data3);
	_sw(g_total_blocks, g_sceNp9660_driver_text_addr + g_offs->Data4);
	_sw(0x00000000, g_sceNp9660_driver_text_addr + g_offs->Data5);

	sceKernelCpuResumeIntr(intr);

	if (g_data_1204 == 0) {
		g_data_1204 = 1;
		sceKernelDelayThread(800000);
	}

	sctrlFlushCache();
	sceKernelSetQTGP3(g_umddata);

	return 0;
}

// d8
int get_total_block(void) {
	SceOff offset;
	int ret;

	if (g_is_ciso) {
		ret = ciso_total_block;
	} else {
		offset = sceIoLseek(g_iso_fd, 0, PSP_SEEK_END);

		if (offset < 0) {
			logmsg("%s: lseek 0x%08X\n", __func__, (int)offset);
			return (int)offset;
		}

		ret = offset / ISO_SECTOR_SIZE;
	}

	logmsg("%s: returns %d blocks\n", __func__, ret);

	return ret;
}

// 244
int read_raw_data(u8* addr, u32 size, u32 offset) {
	int ret;
	int i = 0;
	SceOff ofs;

	do {
		i++;
		ofs = sceIoLseek(g_iso_fd, offset, PSP_SEEK_SET);

		if (ofs >= 0) {
			i = 0;
			break;
		} else {
			open_iso();
		}
	} while (i < 16);

	if (i == 16) {
		ret = 0x80010013;
		goto exit;
	}

	for (i = 0; i < 16; ++i) {
		ret = sceIoRead(g_iso_fd, addr, size);

		if (ret >= 0) {
			i = 0;
			break;
		} else {
			open_iso();
		}
	}

	if (i == 16) {
		ret = 0x80010013;
		goto exit;
	}

exit:
	return ret;
}

// 790
int read_cso_sector(u8 *addr, int sector) {
	int ret;
	int n_sector;
	u32 offset, next_offset;
	int size;

	n_sector = sector - g_CISO_cur_idx;

	// not within sector idx cache?
	if (g_CISO_cur_idx == -1 || n_sector < 0 || n_sector >= NELEMS(g_CISO_idx_cache)) {
		ret = read_raw_data((u8*)g_CISO_idx_cache, sizeof(g_CISO_idx_cache), (sector << 2) + sizeof(struct CISO_header));

		if (ret < 0) {
			ret = -4;
			logmsg("%s: -> %d\n", __func__, ret);

			return ret;
		}

		g_CISO_cur_idx = sector;
		n_sector = 0;
	}

	// loc_804
	offset = (g_CISO_idx_cache[n_sector] & 0x7FFFFFFF) << g_CISO_hdr.align;

	// is plain?
	if (g_CISO_idx_cache[n_sector] & 0x80000000) {
		// loc_968
		return read_raw_data(addr, ISO_SECTOR_SIZE, offset);
	}

	sector++;
	n_sector = sector - g_CISO_cur_idx;

	if (g_CISO_cur_idx == -1 || n_sector < 0 || n_sector >= NELEMS(g_CISO_idx_cache)) {
		ret = read_raw_data((u8*)g_CISO_idx_cache, sizeof(g_CISO_idx_cache), (sector << 2) + sizeof(struct CISO_header));

		if (ret < 0) {
			ret = -5;
			logmsg("%s: -> %d\n", __func__, ret);

			return ret;
		}

		g_CISO_cur_idx = sector;
		n_sector = 0;
	}

	// loc_858
	next_offset = (g_CISO_idx_cache[n_sector] & 0x7FFFFFFF) << g_CISO_hdr.align;
	size = next_offset - offset;

	if (g_CISO_hdr.align)
		size += 1 << g_CISO_hdr.align;

	if (size <= ISO_SECTOR_SIZE)
		size = ISO_SECTOR_SIZE;

	if (g_ciso_dec_buf_offset == (u32)-1 || offset < g_ciso_dec_buf_offset || offset + size >= g_ciso_dec_buf_offset + g_ciso_dec_buf_size) {
		// loc_93C
		ret = read_raw_data(g_ciso_dec_buf, size, offset);

		/* May not reach CISO_DEC_BUFFER_SIZE */
		if (ret < 0) {
			// loc_95C
			g_ciso_dec_buf_offset = 0xFFF00000;
			ret = -6;
			logmsg("%s: -> %d\n", __func__, ret);

			return ret;
		}

		g_ciso_dec_buf_offset = offset;
		g_ciso_dec_buf_size = ret;
	}

	// loc_8B8
	if (!lz4_compressed) {
		ret = sceKernelDeflateDecompress(addr, ISO_SECTOR_SIZE, g_ciso_dec_buf + offset - g_ciso_dec_buf_offset, 0);
	} else {
		ret = sctrlLZ4Decompress(g_ciso_dec_buf + offset - g_ciso_dec_buf_offset, addr, ISO_SECTOR_SIZE);
		if (ret < 0) {
			ret = -20;
			logmsg("%s: -> %d\n", __func__, ret);
		}
	}

	return ret < 0 ? ret : ISO_SECTOR_SIZE;
}

int read_cso_data(u8* addr, u32 size, u32 offset) {
	u32 cur_block;
	int pos, ret, read_bytes;
	u32 o_offset = offset;

	while (size > 0) {
		cur_block = offset / ISO_SECTOR_SIZE;
		pos = offset & (ISO_SECTOR_SIZE - 1);

		if (cur_block >= g_total_blocks) {
			// EOF reached
			break;
		}

		ret = read_cso_sector(g_ciso_block_buf, cur_block);

		if (ret != ISO_SECTOR_SIZE) {
			ret = -7;
			logmsg("%s: -> %d\n", __func__, ret);

			return ret;
		}

		read_bytes = MIN(size, (ISO_SECTOR_SIZE - pos));
		memcpy(addr, g_ciso_block_buf + pos, read_bytes);
		size -= read_bytes;
		addr += read_bytes;
		offset += read_bytes;
	}

	return offset - o_offset;
}

// 0x00001210~0x0000121C
struct IoReadArg args;

// 30C
int (*iso_reader)(struct IoReadArg *args) = &iso_read;
int iso_read(struct IoReadArg *args) {
	if (g_is_ciso != 0) {
		// ciso decompess
		return read_cso_data(args->address, args->size, args->offset);
	} else {
		return read_raw_data(args->address, args->size, args->offset);
	}
}

int sub_00000054(u32 a0, u8 *a1, u32 a2) {
	int (*ptr)(u32, u8*, u32) = (void*)g_func_1208;
	int (*ptr2)(void) = (void*)g_func_121C;
	int ret;

	ret = (*ptr)(a0, a1, a2);

	args.offset = a0;
	args.address = a1;
	args.size = a2;

	ret = sceKernelExtendKernelStack(0x2000, (void*)&iso_cache_read, &args);

	(*ptr2)();

	return ret;
}

int sub_00000514(int fd) {
	int ret;

	ret = sceIoClose(fd);

	if (fd == g_iso_fd) {
		g_iso_fd = -1;
		_sw(-1, g_sceNp9660_driver_text_addr + g_offs->StoreFd);
		sctrlFlushCache();

		return ret;
	} else {
		return ret;
	}
}

// 0x000003D8
int myKernelStartThread(SceUID thid, SceSize arglen, void *argp) {
	if (g_SceNpUmdMount_thid == thid) {
		SceModule2 *pMod;

		pMod = (SceModule2*) sceKernelFindModuleByName("sceNp9660_driver");
		g_sceNp9660_driver_text_addr = pMod->text_addr;

		// 6.30: 0x00003C34
		// 6.20: move to 0x00003BD8: jal InitForKernel_29DAA63F
		_sw(0x3C028000, g_sceNp9660_driver_text_addr + g_offs->InitForKernelCall); // jal InitForKernel_23458221 to lui $v0, 0x00008000
		// 6.30: 0x00003C4C
		// 6.20: move to 0x00003BF0: jal sub_00000000
		_sw(JAL(sub_00000588), g_sceNp9660_driver_text_addr + g_offs->Func1);
		// 6.30: 0x000043B4
		// 6.20: move to 0x00004358: jal sub_00004388
		_sw(JAL(sub_00000054), g_sceNp9660_driver_text_addr + g_offs->Func2); // jal sub_3948 to jal sub_00000054
		// 6.30: 0x0000590C
		// 6.20: move to 0x0000582C: jal sub_00004388
		_sw(JAL(sub_00000054), g_sceNp9660_driver_text_addr + g_offs->Func3); // jal sub_3948 to jal sub_00000054
		// 6.30: 0x00007D08
		// 6.20: move to 0x00007C28
		_sw(JUMP(sub_00000514), g_sceNp9660_driver_text_addr + g_offs->sceIoClose); // hook sceIoClose import

		// 6.30: 0x00003680
		// 6.20: move to 0x00003624
		g_func_1200 = pMod->text_addr + g_offs->Func4;
		logmsg("g_func_1200 0x%08X\n", (uint)g_func_1200); // sub_2f30
		// 6.30: 0x00004F8C
		// 6.20: move to 0x00004EAC
		g_func_1208 = pMod->text_addr + g_offs->Func5;
		logmsg("g_func_1208 0x%08X\n", (uint)g_func_1208); // sub_4494
		// 6.30: 0x00004FFC
		// 6.20: move to 0x00004F1C
		g_func_121C = pMod->text_addr + g_offs->Func6;
		logmsg("g_func_121C 0x%08X\n", (uint)g_func_121C); // sub_44ec

		sctrlFlushCache();
	}

	return sceKernelStartThread(thid, arglen, argp);
}

// 0x00000340
int module_start(SceSize args, void* argp) {
	logInit("ms0:/log_galaxy.txt");
	logmsg("PROGalaxyController started: 0x%08X\n", (uint)psp_fw_version);

	SEConfig config;

	psp_model = sceKernelGetModel();
	psp_fw_version = FW_661;
	// psp_fw_version = sceKernelDevkitVersion();
	// setup_patch_offset_table(psp_fw_version);


	int key_config = sceKernelApplicationType();
	sctrlSEGetConfig(&config);

	// if (config.iso_cache && psp_model != PSP_1000 && key_config == PSP_INIT_KEYCONFIG_GAME) {
	// 	int bufsize;

	// 	bufsize = config.iso_cache_total_size * 1024 * 1024 / config.iso_cache_num;

	// 	if ((bufsize % 512) != 0) {
	// 		bufsize &= ~(512-1);
	// 	}

	// 	if (bufsize == 0) {
	// 		bufsize = 512;
	// 	}

	// 	infernoCacheSetPolicy(config.iso_cache_policy);
	// 	infernoCacheInit(bufsize, config.iso_cache_num);
	// }

	memset(g_iso_fn, 0, sizeof(g_iso_fn));
    strncpy(g_iso_fn, GetUmdFile(), sizeof(g_iso_fn));

	SceModule2 *pMod = (SceModule2*)sceKernelFindModuleByName("sceThreadManager");

	if (pMod != NULL) {
		// sceKernelCreateThread export
		_sw((u32)&myKernelCreateThread, pMod->text_addr + g_offs->sceKernelCreateThread);

		// sceKernelStartThread export
		_sw((u32)&myKernelStartThread, pMod->text_addr + g_offs->sceKernelStartThread);
	} else {
		logmsg("sceThreadManager cannot be found?!\n");
	}

	sctrlFlushCache();

	SceUID fd;
	while ( 1 ) {
		fd = sceIoOpen(g_iso_fn, PSP_O_RDONLY, 0);

		if (fd >= 0) {
			break;
		}

		sceKernelDelayThread(10000);
	}

	sceIoClose(fd);
	logmsg("%s: finished\n", __func__);

	return 0;
}
