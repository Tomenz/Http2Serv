
# ACHTUNG unbedingt TABS benutzen beim einrücken

# http://stackoverflow.com/questions/2394609/makefile-header-dependencies
# http://lackof.org/taggart/hacking/make-example/
# http://owen.sj.ca.us/~rk/howto/slides/make/slides/makerecurs.html
# https://gcc.gnu.org/onlinedocs/gcc/Invoking-GCC.html
# http://www.ijon.de/comp/tutorials/makefile.html (Deutsch)

CC = g++
#CC = clang++
ifeq ($(DEBUG), yes)
CFLAGS = -ggdb -Wno-psabi -O1 -D ZLIB_CONST -pthread
else
CFLAGS = -Wall -Wno-psabi -O3 -D ZLIB_CONST -pthread -ffunction-sections -fdata-sections
endif
LDFLAGS = -Wl,--gc-sections -lpthread -static-libgcc -static-libstdc++
TARGET = Http2Serv
Fetch = Http2Fetch
DIRS = SocketLib CommonLib FastCgi SrvLib
ZLIBDIR = zlib
BRLIBDIR = brotli

BUILDDIRS = $(DIRS:%=build-%)
CLEANDIRS = $(DIRS:%=clean-%)

INC_PATH = -I ./brotli/c/include -I .
LIB_PATH = -L ./zlib -L ./SocketLib -L ./brotli -L ./CommonLib -L ./FastCgi -L ./SrvLib

OBJ = Http2Serv.o HttpServ.o ConfFile.o LogFile.o Trace.o SpawnProcess.o HPack.o #OBJ = $(patsubst %.cpp,%.o,$(wildcard *.cpp))
LIB = -l z -l socketlib -l brotli -l crypto -l ssl -l commonlib -l fastcgi -l srvlib

export $(DEBUG)

all: $(TARGET)

$(TARGET): $(BUILDDIRS) $(BRLIBDIR) $(ZLIBDIR) $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LIB_PATH) $(LIB) $(LDFLAGS)

Fetch: $(BUILDDIRS) $(BRLIBDIR) $(ZLIBDIR) Http2Fetch.o HttpFetch.o HPack.o
	$(CC) -o $(Fetch) Http2Fetch.o HttpFetch.o HPack.o $(LIB_PATH) $(LIB) $(LDFLAGS)

%.o: %.cpp HttpServ.h HPack.h H2Proto.h Timer.h
	$(CC) $(CFLAGS) $(INC_PATH) -c $<

$(DIRS): $(BUILDDIRS)
$(BUILDDIRS):
	$(MAKE) -C $(@:build-%=%)

$(BRLIBDIR):
	$(MAKE) -C $(BRLIBDIR) lib

$(ZLIBDIR):
	$(MAKE) -C $(ZLIBDIR) libz.a

clean: $(CLEANDIRS)
	rm -f $(TARGET) $(OBJ) *~

$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean
	$(MAKE) -C $(BRLIBDIR) clean
	$(MAKE) -C $(ZLIBDIR) clean

.PHONY: subdirs $(DIRS)
.PHONY: subdirs $(BUILDDIRS)
.PHONY: subdirs $(BRLIBDIR)
.PHONY: subdirs $(ZLIBDIR)
.PHONY: subdirs $(CLEANDIRS)
.PHONY: clean


