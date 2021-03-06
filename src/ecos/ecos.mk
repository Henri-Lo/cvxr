# Makefile configuration for ECOS

# Whether to use Long or Int for index type
# comment it out to use ints
USE_LONG = 1

## Intel C Compiler
#CC = icc
#CFLAGS = -O3 -m64 -Wall -strict-ansi -DLDL_LONG -DDLONG
#LIBS = -lm

## GNU C Compiler
#CC = gcc
## Modified by bnaras
##CFLAGS += -O2 -Wall -DCTRLC=1 -Wextra -fPIC #-ansi -Werror #-ipo
ifdef USE_LONG
ECOS_CFLAGS = -DCTRLC=1 -DLDL_LONG -DDLONG
LDL = ldll.o
AMD = amd_l*.o amd_global.o
else
ECOS_CFLAGS = -DCTRLC=1
LDL = ldl.o
AMD = amd_i*.o amd_global.o
endif

UNAME = ""
ifeq ("$(OS)","Windows")
ISWINDOWS := 1
else
ISWINDOWS := 0
UNAME = $(shell uname)
endif

ifeq ($(UNAME), Darwin)
# we're on apple, no need to link rt library
ECOS_LDFLAGS = -lm
# shared library has extension .dylib
SHAREDNAME = libecos.dylib
else ifeq ($(ISWINDOWS), 1)
# we're on windows (cygwin or msys)
ECOS_LDFLAGS = -lm
# shared library has extension .dll
SHAREDNAME = libecos.dll
else
# we're on a linux system, use accurate timer provided by clock_gettime()
ECOS_LDFLAGS = -lm -lrt
# shared library has extension .so
SHAREDNAME = libecos.so
endif


## AR and RANLIB FOR GENERATING LIBRARIES
##AR = ar
##ARFLAGS = rcs
ARCHIVE = $(AR) $(ARFLAGS)
##RANLIB = ranlib

## WHICH FILES TO CLEAN UP
CLEAN = *.o *.obj *.ln *.bb *.bbg *.da *.tcov *.gcov gmon.out *.bak *.d *.gcda *.gcno libecos*.a libecos*.so libecos*.dylib libecos*.dll ecos_bb_test ecostester ecostester.exe runecosexp
