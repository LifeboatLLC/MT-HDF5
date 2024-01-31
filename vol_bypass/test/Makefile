#Your HDF5 install path
HDF5_DIR=~/Lifeboat/HDF/build_hdf5/hdf5
#HDF5_DIR=/Users/koziol/HDF5/github/hpc-io/async_vol_register_optional/build_parallel_debug/hdf5

H5CC=$(HDF5_DIR)/bin/h5cc
CC=clang

CREATESRC = h5_create.c
CREATEOBJ = $(CREATESRC:.c=.o)
CREATEEXE = h5_create

DSETRSRC = h5_read.c
DSETROBJ = $(DSETRSRC:.c=.o)
DSETREXE = h5_read

POSIXRSRC = posix_read.c
POSIXROBJ = $(POSIXRSRC:.c=.o)
POSIXREXE = posix_read

all: $(CREATEEXE) $(DSETREXE) $(POSIXREXE)

$(CREATEEXE): $(CREATESRC)
	$(H5CC) $^ -o $(CREATEEXE)

$(DSETREXE): $(DSETRSRC)
	$(H5CC) -g -pthread $^ -o $(DSETREXE)

$(POSIXREXE): $(POSIXRSRC)
	$(CC) -O3 -pthread $^ -o $(POSIXREXE)

.PHONY: clean all
clean:
	rm -rf $(CREATEEXE) $(CREATEOBJ) $(DSETREXE) $(DSETROBJ) $(POSIXREXE) $(POSIXROBJ)
