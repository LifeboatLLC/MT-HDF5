#! /bin/sh

# Modify the following variables as command line options for the h5_read.c:
#     number of threads
#     number of steps for thread pool queue
#     maximal number of data elements for each data pieces to be read
#     dimension one of dataset
#     dimension two of dataset
#     number of files
NTHREADS_FOR_MULTI=4
NTHREADS_FOR_TPOOL=4
NSTEPS_QUEUE=1024
MAX_NELMTS=1048576

# Dataset size = 1KB
DIM1=2
DIM2=2
NFILES=32

# Dataset size = 16GB
# DIM1=65536
# DIM2=65536
# NFILES=4

# Dataset size = 64GB
# DIM1=131072
# DIM2=131072
# NFILES=4

# Make sure these two environment variables aren't set in order to run the test without Bypass VOL
unset HDF5_VOL_CONNECTOR
unset HDF5_PLUGIN_PATH

echo "Test 0: Creating multiple HDF5 files with a single dataset in each of them"
./h5_create -d ${DIM1}x${DIM2} -f ${NFILES}

echo ""
echo "Test 1: Reading single dataset in a single file with straight HDF5 (no Bypass VOL)"
./h5_read -t 0 -d ${DIM1}x${DIM2} -f ${NFILES} -k

# Set the environment variables to use Bypass VOL. Need to modify them with your own paths 
export HDF5_PLUGIN_PATH=/Users/raylu/Lifeboat/HDF/Matt/MT-HDF5_no_tpool/vol_bypass
export HDF5_VOL_CONNECTOR="bypass under_vol=0;under_info={};"
export DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:/Users/raylu/Lifeboat/HDF/Jordan/build/hdf5/lib:$HDF5_PLUGIN_PATH
export BYPASS_VOL_NTHREADS=${NTHREADS_FOR_TPOOL}
export BYPASS_VOL_NSTEPS=${NSTEPS_QUEUE}
export BYPASS_VOL_MAX_NELMTS=${MAX_NELMTS}

# Instead of the thread pool, each thread does its own reading
export BYPASS_VOL_NO_TPOOL=true

echo ""
echo "Test 2a: Reading a single dataset in multiple files with Bypass VOL with multiple threads (not using thread pool)"
./h5_read -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -f ${NFILES} -k

# Use the thread pool
unset BYPASS_VOL_NO_TPOOL

echo ""
echo "Test 2b: Reading a single dataset in multiple files with Bypass VOL using the thread pool"
./h5_read -t 0 -d ${DIM1}x${DIM2} -f ${NFILES} -k

echo ""
echo "Test 2c: Reading a single dataset in multiple files using H5Dread_multi with Bypass VOL using the thread pool"
./h5_read -t 0 -d ${DIM1}x${DIM2} -f ${NFILES} -l -k

# The C test must follow the test with Bypass VOL immediately to use info.log file which contains file name and data info
echo ""
echo "Test 3a: Reading single dataset in a single file in C only with multi-thread (no thread pool)"
./posix_read_mthread -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -f ${NFILES} -k

# Avoid checking the correctness of the data if there are more than one section because the thread pool may still be 
# reading the data during the check.  Each section corresponds to a H5Dread.  Sections are seperated by ### in info.log.
# The way thread pool is set up doesn't guarantee the data reading is finished during the check.
echo ""
echo "Test 3b: Reading single dataset in a single file in C only with thread pool"
./posix_read_tpool -t ${NTHREADS_FOR_TPOOL} -d ${DIM1}x${DIM2} -f ${NFILES} -k
