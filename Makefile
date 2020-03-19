
# ACHTUNG unbedingt TABS benutzen beim einrücken

# http://stackoverflow.com/questions/2394609/makefile-header-dependencies
# http://lackof.org/taggart/hacking/make-example/
# http://owen.sj.ca.us/~rk/howto/slides/make/slides/makerecurs.html
# https://gcc.gnu.org/onlinedocs/gcc/Invoking-GCC.html
# http://www.ijon.de/comp/tutorials/makefile.html (Deutsch)

CC = g++
#CC = clang++
CFLAGS = -Wall -O3 -std=c++14 -D ZLIB_CONST -pthread -ffunction-sections -fdata-sections	# -lstdc++fs # -ffunction-sections -fdata-sections -fomit-frame-pointer
#CFLAGS = -ggdb -w -std=c++14 -D ZLIB_CONST -pthread # -lstdc++fs # -ffunction-sections -fdata-sections -fomit-frame-pointer -DPOSIX
#CFLAGS = -w -std=c++14 -pthread -lstdc++fs # -lstdc++fs -ffunction-sections -fdata-sections -fomit-frame-pointer -DPOSIX
LDFLAGS = -Wl,--gc-sections -lpthread -static-libgcc -static-libstdc++ # -lstdc++fs
TARGET = Http2Serv
#DIRS = zlib SocketLib brotli CommonLib
DIRS = SocketLib CommonLib FastCgi
ZLIBDIR = zlib
BRLIBDIR = brotli

BUILDDIRS = $(DIRS:%=build-%)
CLEANDIRS = $(DIRS:%=clean-%)

#INC_PATH = -I ../matrixssl-3-7-2b-open/ -I .
#LIB_PATH = -L ../matrixssl-3-7-2b-open/core -L ../matrixssl-3-7-2b-open/crypto -L ../matrixssl-3-7-2b-open/matrixssl -L ./zlib
INC_PATH = -I ./brotli/c/include -I .
LIB_PATH = -L ./zlib -L ./SocketLib -L ./brotli -L ./CommonLib -L ./FastCgi

OBJ = Http2Serv.o HttpServ.o ConfFile.o LogFile.o Trace.o SpawnProcess.o HPack.o #OBJ = $(patsubst %.cpp,%.o,$(wildcard *.cpp))
#LIB = -l ssl_s -l crypt_s -l core_s -l zlib -l crypto -l ssl
LIB = -l z -l socketlib -l brotli -l crypto -l ssl -l commonlib -l fastcgi

all: $(TARGET)

$(TARGET): $(BUILDDIRS) $(BRLIBDIR) $(ZLIBDIR) $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LIB_PATH) $(LIB) $(LDFLAGS)

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


