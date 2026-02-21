#Your HDF5 install path
HDF5_DIR=/Users/raylu/Lifeboat/HDF/Jordan/build/hdf5

H5CC=$(HDF5_DIR)/bin/h5cc -shlib
CC=clang

CREATESRC = h5_create.c
CREATEOBJ = $(CREATESRC:.c=.o)
CREATEEXE = h5_create

DSETWSRC = h5_write.c
DSETWOBJ = $(DSETWSRC:.c=.o)
DSETWEXE = h5_write

DSETRSRC = h5_read.c
DSETROBJ = $(DSETRSRC:.c=.o)
DSETREXE = h5_read

POSIXRMSRC = posix_read_mthread.c
POSIXRMOBJ = $(POSIXRMSRC:.c=.o)
POSIXRMEXE = posix_read_mthread

POSIXRTSRC = posix_read_tpool.c
POSIXRTOBJ = $(POSIXRTSRC:.c=.o)
POSIXRTEXE = posix_read_tpool

all: $(CREATEEXE) $(DSETREXE) $(DSETWEXE) $(POSIXRMEXE) $(POSIXRTEXE)

$(CREATEEXE): $(CREATESRC)
	$(H5CC) $^ -o $(CREATEEXE)

$(DSETREXE): $(DSETRSRC)
	$(H5CC) -O3 -pthread $^ -o $(DSETREXE)

$(DSETWEXE): $(DSETWSRC)
	$(H5CC) -O3 -pthread $^ -o $(DSETWEXE)

$(POSIXRMEXE): $(POSIXRMSRC)
	$(CC) -O3 -pthread $^ -o $(POSIXRMEXE)

$(POSIXRTEXE): $(POSIXRTSRC)
	$(CC) -O3 -pthread $^ -o $(POSIXRTEXE)

.PHONY: clean all
clean:
	rm -rf $(CREATEEXE) $(CREATEOBJ) $(DSETREXE) $(DSETROBJ) $(DSETWEXE) $(DSETWOBJ) $(POSIXRMEXE) $(POSIXRMOBJ) $(POSIXRTEXE) $(POSIXRTOBJ) *.h5
