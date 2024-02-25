#Your HDF5 install path
HDF5_DIR=/Users/raylu/Lifeboat/HDF/build_hdf5/hdf5

CC=clang
AR=ar

# DEBUG=-DENABLE_BYPASS_LOGGING -g -O0
DEBUG=-g -O0

INCLUDES=-I$(HDF5_DIR)/include
CFLAGS = $(DEBUG) -fPIC $(INCLUDES) -Wall

LIBS=-L$(HDF5_DIR)/lib -lhdf5 -lz

# Uncomment this line Linux builds:
# DYNLDFLAGS = $(DEBUG) -shared -fPIC $(LIBS)

# Uncomment this line MacOS builds:
DYNLDFLAGS = $(DEBUG) -dynamiclib -current_version 1.0 -fPIC $(LIBS)

LDFLAGS = $(DEBUG) $(LIBS)
ARFLAGS = rs

DYNSRC = H5VLbypass.c
DYNOBJ = $(DYNSRC:.c=.o)

# Uncomment this line Linux builds:
# DYNLIB = libh5bypass_vol.so

# Uncomment this line MacOS builds:
DYNLIB = libh5bypass_vol.dylib
DYNDBG = libh5bypass_vol.dylib.dSYM

# all: $(EXEXE) $(ASYNC_EXEXE) $(DYNLIB) $(STATLIB)
all: $(DYNLIB)

$(DYNLIB): $(DYNSRC)
	$(CC) $(CFLAGS) $(DYNLDFLAGS) $^ -o $@

.PHONY: clean all
clean:
	rm -rf $(DYNOBJ) $(DYNLIB) $(DYNDBG)
