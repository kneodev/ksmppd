SHELL = /bin/sh

KANNEL_PATH=./kannel-svn-trunk
CC = gcc
CFLAGS = `${KANNEL_PATH}/gw-config --cflags` -I${KANNEL_PATH} -O1 -Wall -I. -I./smpp -I`pwd`/build/include -I/sw/include -fPIC  
LIBS = `${KANNEL_PATH}/gw-config --libs` -L${KANNEL_PATH} -L/sw/lib -L/usr/lib -L`pwd`/build/libevent/.libs -levent -rdynamic -lrt

# platform specific shared library extentions and flags
DSO_EXT = so
DSO_CFLAGS = $(CFLAGS) -fPIC
DSO_LDFLAGS =  -shared

RANLIB = ranlib

testsrcs = $(wildcard tests/*.c)
testobjs = $(testsrcs:.c=.o)
testprogs = $(testsrcs:.c=)
	
smppsrcs = $(wildcard smpp/*.c)
smppobjs = $(smppsrcs:.c=.o)
smpp = $(smppsrcs:.c=)
	
testlibs = libvsmsc.a

libsrcs = $(wildcard smpp/libsmpp/*.c) 
libobjs = $(libsrcs:.c=.o) 

libs = libsmpp.a


all: tests smpp
	
libsmpp.a: ${libobjs}
	ar rc  libsmpp.a $(libobjs)
	${RANLIB} libsmpp.a

tests: $(testprogs)

smpp: $(smpp)
	$(info -------------------------------------------------------)
	$(info You can now run ./smpp/ksmppd to start the SMPP server)
	$(info -------------------------------------------------------)

.SUFFIXES: $(SUFFIXES) .c .lo .so

.c.lo:
	$(CC) $(DSO_CFLAGS) -o $@ -c $<
	
.lo.so:
	$(CC) -o $@ $< $(DSO_LDFLAGS)

.lo.dylib:
	$(CC) -o $@ $< $(DSO_LDFLAGS) $(LDFLAGS)
	
.lo.dll:
	$(CC) -o $@ $< $(DSO_LDFLAGS) $(LDFLAGS)

$(testprogs): $(testobjs) $(libs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=).o $(libs) $(LIBS)
	
$(smpp): FORCE $(smppobjs) $(libs)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(@:=).o $(libs) $(LIBS)
	


clean:
	rm -f ${testprogs} tests/*.o smpp/libsmpp/*.o smpp/*.o ${smpp} libsmpp.a 

FORCE:
	rm -f smpp/libsmpp/*.o libsmpp.a ${smpp}