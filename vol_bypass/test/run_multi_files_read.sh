#! /bin/sh

# Modify the following variables as command line options for the h5_create.c, h5_read.c.  The number of data sections
# (NDATA_SECTIONS) isn't supported in these test cases:
#     NTHREADS_FOR_MULTI: number of threads for the multi-thread application
#     NTHREADS_FOR_TPOOL: number of threads for the thread pool
#     NSTEPS_QUEUE:       number of tasks to be put into thread pool queue in each step
#     MAX_NELMTS:         maximal number of data elements (not in bytes) for each data pieces to be read
#     NDATA_SECTIONS:     number of dataset sections to break down big datasets by rows in case they are bigger than the memory size.
#                         e.g. if the memory size is 32GB, datasets of 64GB won't fit in.  This option breaks down the datasets
#                         in sections for creation and reading. Remember to avoid break down small datasets in case unexpected errors.
#     DIM1:               dataset dimension one
#     DIM2:               dataset dimension two
#     NFILES:             number of files being tested
NTHREADS_FOR_MULTI=4
NTHREADS_FOR_TPOOL=4
NSTEPS_QUEUE=1024
MAX_NELMTS=1048576

# Dataset size = 1KB
# DIM1=4
# DIM2=4
# NFILES=1024

# Dataset size = 4MB
# DIM1=1024
# DIM2=1024
# NFILES=32768

# Dataset size = 16MB
# DIM1=2048
# DIM2=2048
# NFILES=8192

# Dataset size = 64MB
# DIM1=4096
# DIM2=4096
# NFILES=2048

# Dataset size = 256MB
# DIM1=8192
# DIM2=8192
# NFILES=512

# Dataset size = 1GB
DIM1=16384
DIM2=16384
NFILES=16

# Dataset size = 16GB
# DIM1=65536
# DIM2=65536
# NFILES=4

# Dataset size = 64GB
# DIM1=131072
# DIM2=131072
# NFILES=2

# Make sure these two environment variables aren't set in order to run the test without Bypass VOL
unset HDF5_VOL_CONNECTOR
unset HDF5_PLUGIN_PATH

echo "Test 0: Creating multiple HDF5 files with a single dataset in each of them"
./h5_create -d ${DIM1}x${DIM2} -f ${NFILES}

echo ""
echo "Test 1: Reading single dataset in a single file with straight HDF5 (no Bypass VOL)"
./h5_read -t 0 -d ${DIM1}x${DIM2} -f ${NFILES}

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
echo "		===================================================================		"
echo "Test 2a: Reading a single dataset in multiple files with Bypass VOL with multiple threads (not using thread pool)"
./h5_read -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -f ${NFILES}

# Use the thread pool
unset BYPASS_VOL_NO_TPOOL

echo ""
echo ""
echo "Test 2b: Reading single dataset in multiple files running multi-threaded application and the thread pool in the Bypass VOL"
./h5_read -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -f ${NFILES}

echo ""
echo "Test 2c: Reading a single dataset in multiple files with Bypass VOL using the thread pool"
./h5_read -t 0 -d ${DIM1}x${DIM2} -f ${NFILES}

# echo ""
# echo "Test 2d: Reading a single dataset in multiple files using H5Dread_multi with Bypass VOL using the thread pool"
# ./h5_read -t 0 -d ${DIM1}x${DIM2} -f ${NFILES} -l

# The C test must follow the test with Bypass VOL immediately to use info.log file which contains file name and data info
# echo ""
# echo "		===================================================================		"
# echo "Test 3a: Reading single dataset in a single file in C only with no child thread and no thread pool"
# ./posix_read_mthread -t 0 -d ${DIM1}x${DIM2} -f ${NFILES} -k

# echo ""
# echo "Test 3b: Reading single dataset in a single file in C only with multi-thread (no thread pool)"
# ./posix_read_mthread -t ${NTHREADS_FOR_MULTI} -d ${DIM1}x${DIM2} -f ${NFILES} -k

# Avoid checking the correctness of the data if there are more than one section because the thread pool may still be 
# reading the data during the check.  Each section corresponds to a H5Dread.  Sections are seperated by ### in info.log.
# The way thread pool is set up doesn't guarantee the data reading is finished during the check.
# echo ""
# echo "Test 3c: Reading single dataset in a single file in C only with thread pool"
# ./posix_read_tpool -t ${NTHREADS_FOR_TPOOL} -d ${DIM1}x${DIM2} -f ${NFILES} -k
