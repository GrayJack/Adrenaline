#ifndef __VSHCTRL_UTILS_H__
#define __VSHCTRL_UTILS_H__

#include <stddef.h>
#include <strings.h>
#include <string.h>
#include <systemctrl.h>
#include <psptypes.h>

typedef struct _pspMsPrivateDirent {
	SceSize size;
	char s_name[16];
	char l_name[1024];
} pspMsPrivateDirent;

#define FAKE_UID 0x0B00B500

size_t strncpy_s(char *strDest, size_t numberOfElements, const char *strSource, size_t count);
size_t strncat_s(char *strDest, size_t numberOfElements, const char *strSource, size_t count);

static inline size_t strcpy_s(char *strDestination, size_t numberOfElements, const char *strSource) {
	return strncpy_s(strDestination, numberOfElements, strSource, -1);
}

static inline size_t strcat_s(char *strDestination, size_t numberOfElements, const char *strSource) {
	return strncat_s(strDestination, numberOfElements, strSource, -1);
}

#define STRCAT_S(d, s) do { strcat_s((d), (sizeof(d) / sizeof(d[0])), (s));} while ( 0 )
#define STRCPY_S(d, s) strcpy_s((d), (sizeof(d) / sizeof(d[0])), (s))

#endif