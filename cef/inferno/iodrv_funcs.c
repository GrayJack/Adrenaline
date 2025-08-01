/*
 * This file is part of PRO CFW.

 * PRO CFW is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * PRO CFW is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PRO CFW. If not, see <http://www.gnu.org/licenses/ .
 */

#include <pspkernel.h>
#include <pspreg.h>
#include <stdio.h>
#include <string.h>
#include <systemctrl.h>
#include <systemctrl_se.h>
#include <pspsysmem.h>
#include <psprtc.h>
#include <pspumd.h>
#include <psputilsforkernel.h>
#include <pspthreadman_kernel.h>
#include <macros.h>

#include <adrenaline_log.h>

#include "utils.h"
#include "inferno.h"

/*
	UMD access RAW routine

	lba_param[0] = 0 , unknown
	lba_param[1] = cmd,3 = ctrl-area , 0 = data-read
	lba_param[2] = top of LBA
	lba_param[3] = total LBA size
	lba_param[4] = total byte size
	lba_param[5] = byte size of center LBA
	lba_param[6] = byte size of start  LBA
	lba_param[7] = byte size of last   LBA
 */

struct LbaParams {
	int unknown1; // 0
	int cmd; // 4
	int lba_top; // 8
	int lba_size; // 12
	int byte_size_total;  // 16
	int byte_size_centre; // 20
	int byte_size_start; // 24
	int byte_size_last;  // 28
};

struct IsoOpenSlot {
	int enabled;
	u32 offset;
};

struct IoIoctlSeekCmd {
	SceOff offset;
	u32 unk;
	u32 whence;
};

// 0x00002740
SceUID g_umd9660_sema_id = -1;

// 0x00002744
static struct IsoOpenSlot g_open_slot[MAX_FILES_NR];

void* g_sector_buf = NULL;

// 0x000023D8
// it's the serial of Coded arms
static const char *g_umd_ids[] = {
	"ULES-00124",
	"ULUS-10019",
	"ULJM-05024",
	"ULAS-42009",
};

// 0x00002480
int g_game_fix_type = 0;

int inferno_mount(SceSize args, void *arg) {

	while (0 == g_iso_opened) {
		iso_open();
		sceKernelDelayThread(20000);
	}

	g_read_arg.offset = 0x8000;
	g_read_arg.address = g_sector_buf;
	g_read_arg.size = ISO_SECTOR_SIZE;
	iso_read(&g_read_arg);

	for (int i = 0; i < NELEMS(g_umd_ids); ++i) {
		if (0 == memcmp(g_read_arg.address + 0x00000373, g_umd_ids[i], 10)) {
			g_game_fix_type = 1;
			goto out;
		}
	}

	if (g_game_fix_type) {
		goto out;
	}

	// NPUG-80086: FLOW -Life Could be Simple-
	if (0 == memcmp(g_read_arg.address + 0x00000373, "NPUG-80086", 10)) {
		g_game_fix_type = 2;
	}

out:
	sceUmdSetDriveStatus(PSP_UMD_PRESENT | PSP_UMD_INITED);
	sceKernelExitDeleteThread(0);
	return 0;
}

// 0x00000CB0
static int IoInit(PspIoDrvArg* arg) {
	void *p;
	if (g_sector_buf == NULL){
		p = oe_malloc(ISO_SECTOR_SIZE);

		if (p == NULL) {
			return -1;
		}

		g_sector_buf = p;
	}

	if (g_umd9660_sema_id < 0) {
		g_umd9660_sema_id = sceKernelCreateSema("EcsUmd9660DeviceFile", 0, 1, 1, 0);

		if (g_umd9660_sema_id < 0) {
			return g_umd9660_sema_id;
		}
	}

	memset(g_open_slot, 0, sizeof(g_open_slot));

	SceUID thid = sceKernelCreateThread("infernoMount", &inferno_mount, 0x18, 0x800, 0, NULL);

	if (thid < 0) {
		return thid;
	}

	sceKernelStartThread(thid, 0, NULL);

	return 0;
}

// 0x000002E8
static int IoExit(PspIoDrvArg* arg) {
	SceUInt timeout = 500000;

	sceKernelWaitSema(g_umd9660_sema_id, 1, &timeout);
	SAFE_FREE(g_sector_buf);
	sceKernelDeleteSema(g_umd9660_sema_id);
	g_umd9660_sema_id = -1;

	return 0;
}

// 0x00000A78
static int IoOpen(PspIoDrvFileArg *arg, char *file, int flags, SceMode mode) {
	int ret;

	int i = 0;
	do {
		i++;
		ret = sceIoLseek32(g_iso_fd, 0, PSP_SEEK_SET);

		if (ret >= 0) {
			i = 0;
			break;
		} else {
			iso_open();
		}
	} while (i < 16);

	if (i == 16) {
		ret = 0x80010013;
		goto exit;
	}

	ret = sceKernelWaitSema(g_umd9660_sema_id, 1, NULL);

	if (ret < 0) {
		return -1;
	}

	for (i = 0; i < NELEMS(g_open_slot); ++i) {
		if (!g_open_slot[i].enabled) {
			break;
		}
	}

	if (i == NELEMS(g_open_slot)) {
		ret = sceKernelSignalSema(g_umd9660_sema_id, 1);

		if (ret < 0) {
			return -1;
		}

		return 0x80010018;
	}

	arg->arg = (void*)i;
	g_open_slot[i].enabled = 1;
	g_open_slot[i].offset = 0;

	ret = sceKernelSignalSema(g_umd9660_sema_id, 1);

	if (ret < 0) {
		return -1;
	}

	ret = 0;

exit:
	return ret;
}

// 0x00000250
static int IoClose(PspIoDrvFileArg *arg) {
	int ret = sceKernelWaitSema(g_umd9660_sema_id, 1, 0);

	if (ret < 0) {
		return -1;
	}

	int offset = (int)arg->arg;

	int retv;
	if (!g_open_slot[offset].enabled) {
		retv = 0x80010016;
	} else {
		g_open_slot[offset].enabled = 0;
		retv = 0;
	}

	ret = sceKernelSignalSema(g_umd9660_sema_id, 1);

	if (ret < 0) {
		return -1;
	}

	return retv;
}

// 0x00000740
static int IoRead(PspIoDrvFileArg *arg, char *data, int len) {
	int ret = sceKernelWaitSema(g_umd9660_sema_id, 1, 0);

	if (ret < 0) {
		ret = -1;
		goto exit;
	}

	int idx = (int)arg->arg;
	int offset = g_open_slot[idx].offset;
	ret = sceKernelSignalSema(g_umd9660_sema_id, 1);

	if (ret < 0) {
		ret = -1;
		goto exit;
	}

	u32 read_len = len;

	if (g_total_sectors < offset + len) {
		read_len = g_total_sectors - offset;
	}

	int retv = iso_read_with_stack(offset * ISO_SECTOR_SIZE, data, read_len * ISO_SECTOR_SIZE);

	if (retv <= 0) {
		ret = retv;
		goto exit;
	}

	retv = retv / ISO_SECTOR_SIZE;
	ret = sceKernelWaitSema(g_umd9660_sema_id, 1, 0);

	if (ret < 0) {
		ret = -1;
		goto exit;
	}

	g_open_slot[idx].offset += retv;
	ret = sceKernelSignalSema(g_umd9660_sema_id, 1);

	if (ret < 0) {
		ret = -1;
		goto exit;
	}

	ret = retv;

exit:
	logmsg("%s: len 0x%08X -> 0x%08X\n", __func__, len, ret);
	return ret;
}

// 0x000000D8
static SceOff IoLseek(PspIoDrvFileArg *arg, SceOff ofs, int whence) {
	int ret = sceKernelWaitSema(g_umd9660_sema_id, 1, NULL);

	if (ret < 0) {
		ret = -1;
		goto exit;
	}

	int idx = (int)arg->arg;

	if (whence == PSP_SEEK_SET) {
		g_open_slot[idx].offset = ofs;
	} else if (whence == PSP_SEEK_CUR) {
		g_open_slot[idx].offset += ofs;
	} else if (whence == PSP_SEEK_END) {
		/*
		 * Original march33 code, is it buggy?
		 * g_open_slot[idx].offset = g_total_sectors - (u32)ofs;
		 */
		g_open_slot[idx].offset = g_total_sectors + ofs;
	} else {
		ret = sceKernelSignalSema(g_umd9660_sema_id, 1);

		if (ret < 0) {
			ret = -1;
			goto exit;
		}

		ret = 0x80010016;
		goto exit;
	}

	if (g_total_sectors < g_open_slot[idx].offset) {
		g_open_slot[idx].offset = g_total_sectors;
	}

	ret = sceKernelSignalSema(g_umd9660_sema_id, 1);

	if (ret < 0) {
		ret = -1;
		goto exit;
	}

	ret = g_open_slot[idx].offset;

exit:
	logmsg("%s: ofs=0x%08X, whence=%d -> 0x%08X\n", __func__, (uint)ofs, whence, ret);

	if (ret>=0) cur_offset = ret;

	return ret;
}

// 0x0000083C
static int IoIoctl(PspIoDrvFileArg *arg, unsigned int cmd, void *indata, int inlen, void *outdata, int outlen) {
	int ret;

	int idx = (int)arg->arg;

	if (cmd == 0x01F010DB) {
		ret = 0;
		goto exit;
	} else if (cmd == 0x01D20001) {
		/* added more data len checks */
		if (outdata == NULL || outlen < 4) {
			ret = 0x80010016;
			goto exit;
		}

		/* Read fd current offset */
		ret = sceKernelWaitSema(g_umd9660_sema_id, 1, NULL);

		if (ret < 0) {
			ret = -1;
			goto exit;
		}


		VWRITE32((u32)outdata, g_open_slot[idx].offset);
		ret = sceKernelSignalSema(g_umd9660_sema_id, 1);

		if (ret < 0) {
			ret = -1;
			goto exit;
		}

		ret = 0;
		goto exit;
	} else if (cmd == 0x01F100A6) {
		/* UMD file seek whence */
		struct IoIoctlSeekCmd *seek_cmd;

		if (indata == NULL || inlen < sizeof(struct IoIoctlSeekCmd)) {
			ret = 0x80010016;
			goto exit;
		}

		seek_cmd = (struct IoIoctlSeekCmd *)indata;
		ret = IoLseek(arg, seek_cmd->offset, seek_cmd->whence);
		goto exit;
	} else if (cmd == 0x01F30003) {
		u32 len;

		if (indata == NULL || inlen < 4) {
			ret = 0x80010016;
			goto exit;
		}

		len = *(u32*)indata;

		if (outdata == NULL || outlen < len) {
			ret = 0x80010016;
			goto exit;
		}

		ret = IoRead(arg, outdata, len);
		goto exit;
	}

	logmsg("%s: Unknown ioctl 0x%08X\n", __func__, cmd);
	ret = 0x80010086;
exit:
	logmsg("%s: cmd:0x%08X -> 0x%08X\n", __func__, cmd, ret);
	return ret;
}

// 0x00000488
static int umd_devctl_read(void *outdata, int outlen, struct LbaParams *param) {
	u32 byte_size_total = param->byte_size_total;

	if (outlen < byte_size_total) {
		return 0x80010069;
	}

	u32 lba_top = param->lba_top;
	u32 byte_size_start = param->byte_size_start;

	u32 offset;
	if (!byte_size_start) {
		offset = lba_top * ISO_SECTOR_SIZE;
	} else if (param->byte_size_centre) {
		offset = lba_top * ISO_SECTOR_SIZE - byte_size_start + ISO_SECTOR_SIZE;
	} else if (!param->byte_size_last) {
		offset = lba_top * ISO_SECTOR_SIZE + byte_size_start;
	} else {
		offset = lba_top * ISO_SECTOR_SIZE - byte_size_start + ISO_SECTOR_SIZE;
	}

	int ret = iso_read_with_stack(offset, outdata, byte_size_total);

	return ret;
}

// 0x000004F4
static int IoDevctl(PspIoDrvFileArg *arg, const char *devname, unsigned int cmd, void *indata, int inlen, void *outdata, int outlen) {
	int ret;

	if (cmd == 0x01F00003) {
		ret = 0;
		goto exit;
	} else if (cmd == 0x01F010DB) {
		ret = 0;
		goto exit;
	} else if (cmd == 0x01F20001) {
		// get UMD disc type
		VWRITE32((u32)(outdata), -1);
		VWRITE32((u32)(outdata+4), g_disc_type);

		ret = 0;
		goto exit;
	} else if (cmd == 0x01F100A3) {
		/* missing cmd in march33, seek UMD disc (raw). */
		if (indata == NULL || inlen < 4) {
			ret = 0x80010016;
			goto exit;
		}

		ret = 0;
		goto exit;
	} else if (cmd == 0x01F100A4) {
		u32 lba;
		u32 sector;

		/* missing cmd in march33, prepare UMD data into cache */
		if (indata == NULL || inlen < 16) {
			ret = 0x80010016;
			goto exit;
		}

		lba = ((u32*)(indata))[1];
		sector = ((u32*)(indata))[3];
		infernoCacheAdd(lba * ISO_SECTOR_SIZE, sector * ISO_SECTOR_SIZE);
		ret = 0;
		goto exit;
	} else if (cmd == 0x01F300A5) {
		u32 lba;
		u32 sector;

		/* missing cmd in march33, prepare UMD data into cache */
		if (indata == NULL || inlen < 16) {
			ret = 0x80010016;
			goto exit;
		}

		if (outdata == NULL || outlen < 4) {
			ret = 0x80010016;
			goto exit;
		}

		lba = ((u32*)(indata))[1];
		sector = ((u32*)(indata))[3];
		infernoCacheAdd(lba * ISO_SECTOR_SIZE, sector * ISO_SECTOR_SIZE);

		VWRITE32((u32)outdata, 1);

		ret = 0;
		goto exit;
	} else if (cmd == 0x01F300A7 || cmd == 0x01F300A8 || cmd == 0x01F300A9) {
		/* missing cmd in march33, cache control */
		if (indata == NULL || inlen < 4) {
			ret = 0x80010016;
			goto exit;
		}

		ret = 0;
		goto exit;
	} else if (cmd == 0x01F20002 || cmd == 0x01F20003) {
		VWRITE32((u32)(outdata), g_total_sectors);

		ret = 0;
		goto exit;
	} else if (cmd == 0x01E18030) {
		ret = 1;
		goto exit;
	} else if (cmd == 0x01E180D3) {
		ret = 0x80010086;
		goto exit;
	} else if (cmd == 0x01E080A8) {
		ret = 0x80010086;
		goto exit;
	} else if (cmd == 0x01E28035) {
		/* Added check for outdata */
		if (outdata == NULL || outlen < 4) {
			ret = 0x80010016;
			goto exit;
		}

		VWRITE32((u32)(outdata), (u32)g_sector_buf);

		ret = 0;
		goto exit;
	} else if (cmd == 0x01E280A9) {
		/* Added check for outdata */
		if (outdata == NULL || outlen < 4) {
			ret = 0x80010016;
			goto exit;
		}

		VWRITE32((u32)(outdata), ISO_SECTOR_SIZE);

		ret = 0;
		goto exit;
	} else if (cmd == 0x01E38034) {
		if (indata == NULL || outdata == NULL) {
			ret = 0x80010016;
			goto exit;
		}

		VWRITE32((u32)(outdata), 0);

		ret = 0;
		goto exit;
	} else if (cmd == 0x01E380C0 || cmd == 0X01F200A1 || cmd == 0x01F200A2) {
		/**
		 * 0x01E380C0: read sectors general
		 * 0x01F200A1: read sectors
		 * 0x01F200A2: read sectors dircache
		 */
		if (indata == NULL || outdata == NULL) {
			ret = 0x80010016;
			goto exit;
		}

		ret = umd_devctl_read(outdata, outlen, indata);
		goto exit;
	} else if (cmd == 0x01E38012) {
		int outlen2 = outlen;

		// loc_6E0
		if (outlen < 0) {
			outlen2 = outlen + 3;
		}

		memset(outdata, 0, outlen2);
		VWRITE32((u32)outdata, 0xE0000800);
		VWRITE32((u32)(outdata + 8), 0);
		VWRITE32((u32)(outdata + 0x1C), g_total_sectors)
		VWRITE32((u32)(outdata + 0x24), g_total_sectors)

		ret = 0;
		goto exit;
	} else {
		logmsg("%s: Unknown cmd 0x%08X\n", __func__, cmd);
		ret = 0x80010086;
	}

exit:
	return ret;
}

// 0x000023EC
static PspIoDrvFuncs g_drv_funcs = {
	.IoInit    = &IoInit,
	.IoExit    = &IoExit,
	.IoOpen    = &IoOpen,
	.IoClose   = &IoClose,
	.IoRead    = &IoRead,
	.IoWrite   = NULL,
	.IoLseek   = &IoLseek,
	.IoIoctl   = &IoIoctl,
	.IoRemove  = NULL,
	.IoMkdir   = NULL,
	.IoRmdir   = NULL,
	.IoDopen   = NULL,
	.IoDclose  = NULL,
	.IoDread   = NULL,
	.IoGetstat = NULL,
	.IoChstat  = NULL,
	.IoRename  = NULL,
	.IoChdir   = NULL,
	.IoMount   = NULL,
	.IoUmount  = NULL,
	.IoDevctl  = &IoDevctl,
	.IoUnk21   = NULL,
};

// 0x00002444
PspIoDrv g_iodrv = {
	.name = "umd",
	.dev_type = 4, // block device
	.unk2 = 0x800,
	.name2 = "UMD9660",
	.funcs = &g_drv_funcs,
};
