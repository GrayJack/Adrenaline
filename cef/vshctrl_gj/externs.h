#ifndef _EXTERNS_H
#define _EXTERNS_H

#include <systemctrl_se.h>
#include <pspctrl.h>

extern AdrenalineConfig g_cfw_config;
extern int (* g_vshmenu_ctrl)(SceCtrlData *pad_data, int count);
extern SceCtrlData *g_last_control_data;
extern u32 g_psp_model;
extern char g_mounted_iso[64];
extern int g_has_umd_iso;

extern u8 g_set;
extern int g_cpu_list[9];
extern int g_bus_list[9];
#define N_CPU (sizeof(g_cpu_list) / sizeof(int))

extern u32 g_firsttick;


#endif