	.set noreorder

#include "pspimport.s"

// Build files
// SystemCtrlForUser_XXXX.o

#ifdef F_SystemCtrlForUser_0000
	IMPORT_START "SystemCtrlForUser",0x40090000
#endif
#ifdef F_SystemCtrlForUser_0001
	IMPORT_FUNC  "SystemCtrlForUser",0x78E46415,sctrlHENFindDriver
#endif
#ifdef F_SystemCtrlForUser_0002
	IMPORT_FUNC  "SystemCtrlForUser",0x159AF5CC,sctrlHENFindFunction
#endif
#ifdef F_SystemCtrlForUser_0003
	IMPORT_FUNC  "SystemCtrlForUser",0x46268461,sctrlHENFindFunctionInMod
#endif
#ifdef F_SystemCtrlForUser_0004
	IMPORT_FUNC  "SystemCtrlForUser",0xDF306CFC,sctrlHENFindImportInMod
#endif
#ifdef F_SystemCtrlForUser_0005
	IMPORT_FUNC  "SystemCtrlForUser",0x1090A2E1,sctrlHENGetVersion
#endif
#ifdef F_SystemCtrlForUser_0006
	IMPORT_FUNC  "SystemCtrlForUser",0xD6075560,sctrlHENHookImportByNID
#endif
#ifdef F_SystemCtrlForUser_0007
	IMPORT_FUNC  "SystemCtrlForUser",0xBD8558C9,sctrlHENHookFunctionByNID
#endif
#ifdef F_SystemCtrlForUser_0008
	IMPORT_FUNC  "SystemCtrlForUser",0x2E2935EF,sctrlHENIsDevhook
#endif
#ifdef F_SystemCtrlForUser_0009
	IMPORT_FUNC  "SystemCtrlForUser",0xD339E2E9,sctrlHENIsSE
#endif
#ifdef F_SystemCtrlForUser_0010
	IMPORT_FUNC  "SystemCtrlForUser",0xC4D88D50,sctrlHENIsSystemBooted
#endif
#ifdef F_SystemCtrlForUser_0011
	IMPORT_FUNC  "SystemCtrlForUser",0x745286D1,sctrlHENSetMemory
#endif
#ifdef F_SystemCtrlForUser_0012
	IMPORT_FUNC  "SystemCtrlForUser",0xCC9ADCF8,sctrlHENSetSpeed
#endif
#ifdef F_SystemCtrlForUser_0013
	IMPORT_FUNC  "SystemCtrlForUser",0x1C90BECB,sctrlHENSetStartModuleHandler
#endif
#ifdef F_SystemCtrlForUser_0014
	IMPORT_FUNC  "SystemCtrlForUser",0x2794CCF4,sctrlKernelExitVSH
#endif
#ifdef F_SystemCtrlForUser_0015
	IMPORT_FUNC  "SystemCtrlForUser",0x577AF198,sctrlKernelLoadExecVSHDisc
#endif
#ifdef F_SystemCtrlForUser_0016
	IMPORT_FUNC  "SystemCtrlForUser",0x94FE5E4B,sctrlKernelLoadExecVSHDiscUpdater
#endif
#ifdef F_SystemCtrlForUser_0017
	IMPORT_FUNC  "SystemCtrlForUser",0x75643FCA,sctrlKernelLoadExecVSHMs1
#endif
#ifdef F_SystemCtrlForUser_0018
	IMPORT_FUNC  "SystemCtrlForUser",0xABA7F1B0,sctrlKernelLoadExecVSHMs2
#endif
#ifdef F_SystemCtrlForUser_0019
	IMPORT_FUNC  "SystemCtrlForUser",0x7B369596,sctrlKernelLoadExecVSHMs3
#endif
#ifdef F_SystemCtrlForUser_0020
	IMPORT_FUNC  "SystemCtrlForUser",0xD690750F,sctrlKernelLoadExecVSHMs4
#endif
#ifdef F_SystemCtrlForUser_0021
	IMPORT_FUNC  "SystemCtrlForUser",0x2D10FB28,sctrlKernelLoadExecVSHWithApitype
#endif
#ifdef F_SystemCtrlForUser_0022
	IMPORT_FUNC  "SystemCtrlForUser",0x56CEAF00,sctrlKernelQuerySystemCall
#endif
#ifdef F_SystemCtrlForUser_0023
	IMPORT_FUNC  "SystemCtrlForUser",0xB364FBB4,sctrlKernelRand
#endif
#ifdef F_SystemCtrlForUser_0024
	IMPORT_FUNC  "SystemCtrlForUser",0xEB74FE45,sctrlKernelSetUserLevel
#endif
#ifdef F_SystemCtrlForUser_0025
	IMPORT_FUNC  "SystemCtrlForUser",0x16C3B7EE,sctrlSEGetConfig
#endif
#ifdef F_SystemCtrlForUser_0026
	IMPORT_FUNC  "SystemCtrlForUser",0x8E426F09,sctrlSEGetConfigEx
#endif
#ifdef F_SystemCtrlForUser_0027
	IMPORT_FUNC  "SystemCtrlForUser",0xABEF849B,sctrlSEGetDiscType
#endif
#ifdef F_SystemCtrlForUser_0028
	IMPORT_FUNC  "SystemCtrlForUser",0xB47C9D77,sctrlSEGetVersion
#endif
#ifdef F_SystemCtrlForUser_0029
	IMPORT_FUNC  "SystemCtrlForUser",0x85B520C6,sctrlSEMountUmdFromFile
#endif
#ifdef F_SystemCtrlForUser_0030
	IMPORT_FUNC  "SystemCtrlForUser",0x1DDDAD0C,sctrlSESetConfig
#endif
#ifdef F_SystemCtrlForUser_0031
	IMPORT_FUNC  "SystemCtrlForUser",0xAD4D5EA5,sctrlSESetConfigEx
#endif
#ifdef F_SystemCtrlForUser_0032
	IMPORT_FUNC  "SystemCtrlForUser",0x31C6160D,sctrlSESetDiscType
#endif
#ifdef F_SystemCtrlForUser_0033
	IMPORT_FUNC  "SystemCtrlForUser",0x80C0ED7B,sctrlStartUsb
#endif
#ifdef F_SystemCtrlForUser_0034
	IMPORT_FUNC  "SystemCtrlForUser",0x5FC12767,sctrlStopUsb
#endif
#ifdef F_SystemCtrlForUser_0035
	IMPORT_FUNC  "SystemCtrlForUser",0x5CB025F0,sctrlSESetBootConfFileIndex
#endif
#ifdef F_SystemCtrlForUser_0036
	IMPORT_FUNC  "SystemCtrlForUser",0x17691875,sctrlFlushCache
#endif
#ifdef F_SystemCtrlForUser_0037
	IMPORT_FUNC  "SystemCtrlForUser",0x05D8E209,sctrlGetUsbState
#endif
#ifdef F_SystemCtrlForUser_0038
	IMPORT_FUNC  "SystemCtrlForUser",0xFE84B359,sctrlLZ4Decompress
#endif
#ifdef F_SystemCtrlForUser_0039
	IMPORT_FUNC  "SystemCtrlForUser",0xCF03305A,sctrlLzoDecompress
#endif
#ifdef F_SystemCtrlForUser_0040
	IMPORT_FUNC  "SystemCtrlForUser",0x053172F8,sctrlRebootDevice
#endif
#ifdef F_SystemCtrlForUser_0041
	IMPORT_FUNC  "SystemCtrlForUser",0xF462EE55,sctrlDeflateDecompress
#endif
#ifdef F_SystemCtrlForUser_0042
	IMPORT_FUNC  "SystemCtrlForUser",0x5D665044,sctrlGzipDecompress
#endif
