#Your HDF5 install path
HDF5_DIR=~/Lifeboat/HDF/build_hdf5/hdf5

H5CC=$(HDF5_DIR)/bin/h5cc -shlib
CC=clang

CREATESRC = h5_create.c
CREATEOBJ = $(CREATESRC:.c=.o)
CREATEEXE = h5_create

DSETRSRC = h5_read.c
DSETROBJ = $(DSETRSRC:.c=.o)
DSETREXE = h5_read

POSIXRMSRC = posix_read_mthread.c
POSIXRMOBJ = $(POSIXRMSRC:.c=.o)
POSIXRMEXE = posix_read_mthread

POSIXRTSRC = posix_read_tpool.c
POSIXRTOBJ = $(POSIXRTSRC:.c=.o)
POSIXRTEXE = posix_read_tpool

all: $(CREATEEXE) $(DSETREXE) $(POSIXRMEXE) $(POSIXRTEXE)

$(CREATEEXE): $(CREATESRC)
	$(H5CC) $^ -o $(CREATEEXE)

$(DSETREXE): $(DSETRSRC)
	$(H5CC) -O0 -g -pthread $^ -o $(DSETREXE)

$(POSIXRMEXE): $(POSIXRMSRC)
	$(CC) -O3 -pthread $^ -o $(POSIXRMEXE)

$(POSIXRTEXE): $(POSIXRTSRC)
	$(CC) -O3 -pthread $^ -o $(POSIXRTEXE)

.PHONY: clean all
clean:
	rm -rf $(CREATEEXE) $(CREATEOBJ) $(DSETREXE) $(DSETROBJ) $(POSIXRMEXE) $(POSIXRMOBJ) $(POSIXRTEXE) $(POSIXRTOBJ)
