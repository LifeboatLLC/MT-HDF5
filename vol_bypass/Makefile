#Your HDF5 install path
HDF5_DIR=../build_hdf5/hdf5
#HDF5_DIR=/Users/koziol/HDF5/github/hpc-io/async_vol_register_optional/build_parallel_debug/hdf5
#MPI_DIR=/usr/local

CC=clang
H5CC=$(HDF5_DIR)/bin/h5cc
#CC=gcc-9
AR=ar

#DEBUG=-DENABLE_EXT_PASSTHRU_LOGGING -g -O0
DEBUG=-g -O0
#DEBUG=-O3
#INCLUDES=-I$(MPI_DIR)/include -I$(HDF5_DIR)/include
INCLUDES=-I$(HDF5_DIR)/include
CFLAGS = $(DEBUG) -fPIC $(INCLUDES) -Wall
#LIBS=-L$(HDF5_DIR)/lib -L$(MPI_DIR)/lib -lhdf5 -lz
LIBS=-L$(HDF5_DIR)/lib -lhdf5 -lz
# Uncomment this line Linux builds:
# DYNLDFLAGS = $(DEBUG) -shared -fPIC $(LIBS)
# Uncomment this line MacOS builds:
DYNLDFLAGS = $(DEBUG) -dynamiclib -current_version 1.0 -fPIC $(LIBS)
LDFLAGS = $(DEBUG) $(LIBS)
ARFLAGS = rs

DYNSRC = H5VLpassthru_ext.c
DYNOBJ = $(DYNSRC:.c=.o)
# Uncomment this line Linux builds:
# DYNLIB = libh5passthrough_vol.so
# Uncomment this line MacOS builds:
DYNLIB = libh5passthrough_vol.dylib
DYNDBG = libh5passthrough_vol.dylib.dSYM

STATSRC = new_h5api.c
STATOBJ = $(STATSRC:.c=.o)
STATLIB = libnew_h5api.a

EXSRC = new_h5api_ex.c
EXOBJ = $(EXSRC:.c=.o)
EXEXE = new_h5api_ex.exe
EXDBG = new_h5api_ex.exe.dSYM

ASYNC_EXSRC = async_new_h5api_ex.c
ASYNC_EXOBJ = $(ASYNC_EXSRC:.c=.o)
ASYNC_EXEXE = async_new_h5api_ex.exe
ASYNC_EXDBG = async_new_h5api_ex.exe.dSYM

DATAFILE = testfile.h5

all: $(EXEXE) $(ASYNC_EXEXE) $(DYNLIB) $(STATLIB)

$(EXEXE): $(EXSRC) $(STATLIB) $(DYNLIB)
	$(CC) $(CFLAGS) $^ -o $(EXEXE) $(LDFLAGS) -L. -lnew_h5api

$(ASYNC_EXEXE): $(ASYNC_EXSRC) $(STATLIB) $(DYNLIB)
	$(CC) $(CFLAGS) $^ -o $(ASYNC_EXEXE) $(LDFLAGS) -L. -lnew_h5api

$(DYNLIB): $(DYNSRC)
	$(CC) $(CFLAGS) $(DYNLDFLAGS) $^ -o $@

$(STATOBJ): $(STATSRC)
	$(CC) -c $(CFLAGS) $^ -o $(STATOBJ)

$(STATLIB): $(STATOBJ)
	$(AR) $(ARFLAGS) $@ $^

.PHONY: clean all
clean:
	rm -rf $(DYNOBJ) $(DYNLIB) $(DYNDBG) \
            $(STATOBJ) $(STATLIB) \
            $(EXOBJ) $(EXEXE) $(EXDBG) \
            $(ASYNC_EXOBJ) $(ASYNC_EXEXE) $(ASYNC_EXDBG) \
            $(DATAFILE)
