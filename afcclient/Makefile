CC=gcc
CFLAGS=-Iinclude -Ilibimobiledevice -Iplist
LDFLAGS=-limobiledevice.4 -lplist.2 -Iinclude -Ilibimobiledevice -Iplist -L.

OS := $(shell uname)
ifeq ($(OS),Darwin)
  # Nothing special needed for MacOS
LDFLAGS+= -Lmac_libs
else ifeq ($(OS),Linux)
  CFLAGS+=-fblocks
  LDFLAGS+=-lBlocksRuntime
else ifeq (MINGW, $(findstring MINGW, $(OS)))
  $(warning sciance!!")
  CFLAGS+= -Iwininclude
  LDFLAGS= -Lwinlibs -limobiledevice -lplist -Iinclude -Ilibimobiledevice -Iplist -L.
	#$(error Unsupported operating system: $(OS))
endif


TARGETS=afcclient

all: $(TARGETS)

afcclient: afcclient.o libidev.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf *.dSYM *.o *.gch $(TARGETS)

