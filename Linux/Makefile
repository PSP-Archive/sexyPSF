#
# PCSX Makefile for Linux
#

all: sexypsf

CC = gcc 
RM = rm -f
STRIP = strip

OPTIMIZE = -O2 -fomit-frame-pointer -finline-functions -ffast-math
FLAGS = -g -D__LINUX__ -DPSS_STYLE=1 
#-DTIMEO
LIBS = -lz

OBJS = ../PsxBios.o ../PsxCounters.o ../PsxDma.o \
       ../Spu.o ../PsxHw.o ../PsxMem.o ../Misc.o \
       ../R3000A.o ../PsxInterpreter.o \
       ../PsxHLE.o ../spu/spu.o oss.o

OBJS+= LnxMain.o

CFLAGS = -Wall -Winline ${OPTIMIZE} -I. -I.. -I/usr/include/g++ ${FLAGS}
CFLAGS+= $(shell gtk-config --cflags)
#CFLAGS+= $(shell pkg-config gtk+-2.0 --cflags)

sexypsf:	${OBJS}
		${CC} ${CFLAGS} ${OBJS} -o sexypsf ${LIBS}

.PHONY: clean sexypsf

clean:
	${RM} *.o ../*.o ../spu/*.o sexypsf

../%.o: ../%.c
	${CC} ${CFLAGS} -c -o $@ $<

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $<
