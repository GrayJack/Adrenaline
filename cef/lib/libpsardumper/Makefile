TARGET = libpsardumper
OBJS = main.o pspdecrypt.o

INCDIR = ../../include
CFLAGS = -O2 -Os -G0 -Wall -fshort-wchar -fno-pic -mno-check-zero-division -D__KERNEL__
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
PSP_FW_VERSION = 661

ifdef PSAR_DUMPER_DEBUG
CFLAGS += -DDEBUG=$(PSAR_DUMPER_DEBUG)
endif

ifdef DEBUG
CFLAGS += -DDEBUG=$(DEBUG)
endif

BUILD_PRX = 1
PRX_EXPORTS = exports.exp

USE_KERNEL_LIBC=1
USE_KERNEL_LIBS=1

LIBDIR = ../
# LDFLAGS = -mno-crt0 -nostartfiles
LIBS = -lpsploadcore_kernel -lpspsemaphore -lpspmesgd_driver -lpspnwman_driver -lpsputils_kernel

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
