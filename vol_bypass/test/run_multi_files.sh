#! /bin/sh

# Modify the following variables as command line options for the h5_read.c:
#     number of threads
#     dimension one
#     dimension two
NTHREADS=2
DIM1=64
DIM2=64
NFILES=4

# Make sure these two environment variables aren't set in order to run the test without Bypass VOL
unset HDF5_VOL_CONNECTOR
unset HDF5_PLUGIN_PATH

echo "Test 0: Creating multiple HDF5 files with a single dataset in each of them"
./h5_create -d ${DIM1}x${DIM2} -f ${NFILES}

echo ""
echo "Test 1: Reading single dataset in a single file with straight HDF5 (no Bypass VOL)"
./h5_read -t ${NTHREADS} -d ${DIM1}x${DIM2} -f ${NFILES} -k

# Set the environment variables to use Bypass VOL. Need to modify them with your own paths 
export HDF5_PLUGIN_PATH=/Users/raylu/Lifeboat/HDF/vol_bypass
export HDF5_VOL_CONNECTOR="bypass under_vol=0;under_info={};"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/Users/raylu/Lifeboat/HDF/build_hdf5/hdf5/lib:$HDF5_PLUGIN_PATH

echo ""
echo "Test 2: Reading single dataset in a single file with Bypass VOL"
./h5_read -t ${NTHREADS} -d ${DIM1}x${DIM2} -f ${NFILES} -k

# The C test must follow the test with Bypass VOL immediately to use info.log file which contains file name and data info
echo ""
echo "Test 3: Reading single dataset in a single file in C only"
./posix_read -t ${NTHREADS} -d ${DIM1}x${DIM2} -f ${NFILES}
