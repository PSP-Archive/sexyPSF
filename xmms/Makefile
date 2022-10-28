#
# sexyPSF Makefile for xmms
#

all: sexypsf

# CPU.  ix86 or ppc, or LSBFIRST for non-x86 LSB-first platforms or MSBFIRST

CPU = ix86
CC = gcc 
RM = rm -f
#LD = ld -shared
LIBS = -lz -shared $(shell gtk-config --libs)
OPTIMIZE = -O2 -finline-functions -ffast-math
FLAGS = -DPSS_STYLE=1

OBJS = ../PsxBios.o ../PsxCounters.o ../PsxDma.o \
       ../Spu.o ../PsxHw.o ../PsxMem.o ../Misc.o \
       ../R3000A.o ../PsxInterpreter.o \
       ../PsxHLE.o ../spu/spu.o

OBJS+= xmms.o

ifeq (${CPU}, ix86)
	OPTIMIZE += -fomit-frame-pointer -mcpu=i686 -fno-exceptions
endif

ifeq (${CPU}, ppc)
	FLAGS+=-DMSB_FIRST
endif	

ifeq (${CPU}, MSBFIRST)
	FLAGS+=-DMSB_FIRST
endif

CFLAGS = -Wall ${OPTIMIZE} -I. -I.. -I/usr/include/g++ ${FLAGS}
CFLAGS+= $(shell gtk-config --cflags)

#ifdef LD
#sexypsf: ${OBJS}
#	${LD} -o libsexypsf.so ${OBJS} ${LIBS}
#else
sexypsf: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} -o libsexypsf.so ${LIBS}
#endif	

install: sexypsf
	mv libsexypsf.so ${HOME}/.xmms/Plugins

installglobal: sexypsf
	mv libsexypsf.so `xmms-config --input-plugin-dir`

.PHONY: clean sexypsf

clean:
	${RM} *.o ../*.o ../spu/*.o libsexypsf.so

../%.o: ../%.c
	${CC} ${CFLAGS} -c -o $@ $<

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<
