#! /bin/sh

# Modify the following variables as command line options for the h5_create.c, h5_read.c.
# The number of data sections isn't used in posix_read_mthread.c, and posix_read_tpool.c:
#     NTHREADS_FOR_MULTI: number of threads for the multi-threaded application
#     NTHREADS_FOR_TPOOL: number of threads for the thread pool
#     NSTEPS_QUEUE:       number of tasks to be put into thread pool queue in each step
#     MAX_NELMTS:         maximal number of data elements (not in bytes) for each data pieces to be read
#     NDATA_SECTIONS:     number of dataset sections to break down big datasets by rows in case they are bigger than the memory size.
#                         e.g. if the memory size is 32GB, datasets of 64GB won't fit in.  This option breaks down the datasets
#                         in sections for creation and reading. Remember to avoid break down small datasets in case unexpected errors.
#     DIM1:               dataset dimension one
#     DIM2:               dataset dimension two
#     CHUNK_DIM1:         chunk dimension one
#     CHUNK_DIM2:         chunk dimension two
NTHREADS_FOR_MULTI=4
NTHREADS_FOR_TPOOL=4
NSTEPS_QUEUE=1024
MAX_NELMTS=1048576
NDATA_SECTIONS=4

# Dataset size = 32x4 bytes
# DIM1=4
# DIM2=4
# CHUNK_DIM1=2
# CHUNK_DIM2=2

# Dataset size = 4MB
# DIM1=1024
# DIM2=1024
# CHUNK_DIM1=64
# CHUNK_DIM2=64

# Dataset size = 16GB
DIM1=65536
DIM2=65536
CHUNK_DIM1=8192
CHUNK_DIM2=8192

# Dataset size = 64GB
# DIM1=131072
# DIM2=131072
# CHUNK_DIM1=8192
# CHUNK_DIM2=8192

echo "Test 0: Creating a HDF5 file with a single dataset in it"
./h5_create -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS}

# Make sure these two environment variables aren't set in order to run the test without Bypass VOL
unset HDF5_VOL_CONNECTOR
unset HDF5_PLUGIN_PATH

echo ""
echo ""
echo "Test 1a: Reading single dataset in a single file with straight HDF5 (no Bypass VOL) with no child thread"
./h5_read -t 0 -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS}

echo ""
echo ""
echo "Test 1b: Reading single dataset in a single file with straight HDF5 (no Bypass VOL) with multiple threads "
./h5_read -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS} -k

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
echo ""
echo "		===================================================================		"
echo "Test 2a: Reading single dataset in a single file with Bypass VOL running no child thread (no thread pool)"
./h5_read -t 0 -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS}

echo ""
echo ""
echo "Test 2b: Reading single dataset in a single file with Bypass VOL running multiple threads (no thread pool)"
./h5_read -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS}

# Use the thread pool
unset BYPASS_VOL_NO_TPOOL

echo ""
echo ""
echo "Test 2c: Reading single dataset in a single file running multi-threaded application and the thread pool in the Bypass VOL"
./h5_read -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS}

echo ""
echo ""
echo "Test 2d: Reading single dataset in a single file with Bypass VOL with thread pool (serial application)"
./h5_read -t 0 -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS}

# The C test must follow the test with Bypass VOL immediately to use info.log file which contains file name and data info
# echo ""
# echo ""
# echo "		===================================================================		"
# echo "Test 3a: Reading single dataset in a single file in C only with no child thread (serial)"
# ./posix_read_mthread -t 0 -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS}

# echo ""
# echo ""
# echo "Test 3b: Reading single dataset in a single file in C only running multi-thread (no thread pool)"
# ./posix_read_mthread -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -q ${NDATA_SECTIONS}

# Checking the correctness of the data may not work if there are more than one section because the thread pool may still be 
# reading the data during the check.  Each section corresponds to a H5Dread.  Sections are seperated by ### in info.log.
# The way thread pool is set up doesn't guarantee the data reading is finished during the check. The test program sleeps for
# a second before checking the data, trying to give the thread pool enough time to finish reading the data.
# echo ""
# echo ""
# echo "Test 3c: Reading single dataset in a single file in C only with thread pool"
# ./posix_read_tpool -t ${NTHREADS_FOR_TPOOL} -d ${DIM1}x${DIM2} -c ${CHUNK_DIM1}x${CHUNK_DIM2} -m ${NSTEPS_QUEUE} -q ${NDATA_SECTIONS}
