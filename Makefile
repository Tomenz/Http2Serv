
# http://stackoverflow.com/questions/2394609/makefile-header-dependencies
# http://lackof.org/taggart/hacking/make-example/
# http://owen.sj.ca.us/~rk/howto/slides/make/slides/makerecurs.html
# https://gcc.gnu.org/onlinedocs/gcc/Invoking-GCC.html

CC = g++
#CC = clang++
CFLAGS = -w -O3 -std=c++14 -m32 -D ZLIB_CONST -pthread -ffunction-sections -fdata-sections -fomit-frame-pointer # -lstdc++fs # -ffunction-sections -fdata-sections -fomit-frame-pointer
#CFLAGS = -ggdb -w -std=c++14 -D _DEBUG -D ZLIB_CONST -pthread # -lstdc++fs # -ffunction-sections -fdata-sections -fomit-frame-pointer -DPOSIX
#CFLAGS = -w -std=c++14 -m32 -pthread -lstdc++fs # -lstdc++fs -ffunction-sections -fdata-sections -fomit-frame-pointer -DPOSIX
LDFLAGS = -Wl,--gc-sections -lpthread -static-libgcc -static-libstdc++ # -lstdc++fs
TARGET = Http2Serv

#INC_PATH = -I ../matrixssl-3-7-2b-open/ -I .
#LIB_PATH = -L ../matrixssl-3-7-2b-open/core -L ../matrixssl-3-7-2b-open/crypto -L ../matrixssl-3-7-2b-open/matrixssl -L ./zlib
INC_PATH = -I ../openssl-1.0.2f/include -I .
LIB_PATH = -L ./zlib -L ./socketlib -L ../openssl-1.0.2f

OBJ = Http2Serv.o
#LIB = -l ssl_s -l crypt_s -l core_s -l zlib -l crypto -l ssl
LIB = -l zlib -l socketlib -l crypto -l ssl

$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LIB_PATH) $(LIB) $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) $(INC_PATH) $(OBJECT_FILES) -c $< 2> error.out

clean:
	rm -f $(TARGET) $(OBJ) *~

