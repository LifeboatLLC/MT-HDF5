#! /bin/sh

# Modify the following variables as command line options for the h5_read.c:
#     number of threads
#     number of steps for thread pool queue
#     maximal number of data elements for each data pieces to be read
#     dimension one of dataset
#     dimension two of dataset
NTHREADS=4
NSTEPS_QUEUE=1024
MAX_NELMTS=1048576

# Dataset size = 1KB
# DIM1=16
# DIM2=16

# Dataset size = 4MB
DIM1=1024
DIM2=1024

# Dataset size = 16GB
# DIM1=65536
# DIM2=65536

# Dataset size = 64GB
# DIM1=131072
# DIM2=131072

SPACE_SELLECTION=1

echo "Test 0: Creating a HDF5 file with a single dataset in it"
./h5_create -d ${DIM1}x${DIM2}

# Make sure these two environment variables aren't set in order to run the test without Bypass VOL
unset HDF5_VOL_CONNECTOR
unset HDF5_PLUGIN_PATH

echo ""
echo ""
echo "Test 1a: Reading single dataset in a single file with straight HDF5 (no Bypass VOL) with no child thread"
./h5_read -t 0 -d ${DIM1}x${DIM2} -s ${SPACE_SELLECTION}

# echo ""
# echo "Test 1b: Reading single dataset in a single file with straight HDF5 (no Bypass VOL)"
# ./h5_read -t ${NTHREADS} -d ${DIM1}x${DIM2} -s ${SPACE_SELLECTION}

# Set the environment variables to use Bypass VOL. Need to modify them with your own paths 
export HDF5_PLUGIN_PATH=/Users/raylu/Lifeboat/HDF/Matt/MT-HDF5_main2/vol_bypass
export HDF5_VOL_CONNECTOR="bypass under_vol=0;under_info={};"
export DYLD_LIBRARY_PATH=$DYLD_LIBRARY_PATH:/Users/raylu/Lifeboat/HDF/Jordan/build/hdf5/lib:$HDF5_PLUGIN_PATH
export BYPASS_VOL_NTHREADS=${NTHREADS}
export BYPASS_VOL_NSTEPS=${NSTEPS_QUEUE}
export BYPASS_VOL_MAX_NELMTS=${MAX_NELMTS}

echo ""
echo ""
echo "Test 2a: Reading single dataset in a single file with Bypass VOL with thread pool"
./h5_read -t 0 -d ${DIM1}x${DIM2} -s ${SPACE_SELLECTION}

# echo ""
# echo "Test 2b: Reading single dataset in a single file with Bypass VOL"
# ./h5_read -t ${NTHREADS} -d ${DIM1}x${DIM2} -s ${SPACE_SELLECTION}

# The C test must follow the test with Bypass VOL immediately to use info.log file which contains file name and data info
echo ""
echo ""
echo "Test 3a: Reading single dataset in a single file in C only with no child thread (serial)"
./posix_read_mthread -t 0 -d ${DIM1}x${DIM2}

echo ""
echo ""
echo "Test 3b: Reading single dataset in a single file in C only with multi-thread"
./posix_read_mthread -t ${NTHREADS} -d ${DIM1}x${DIM2}

# The C test must follow the test with Bypass VOL immediately to use info.log file which contains file name and data info
# Avoid checking the correctness of the data if there are more than one section because the thread pool may still be 
# reading the data during the check.  Each section corresponds to a H5Dread.  Sections are seperated by ### in info.log.
# The way thread pool is set up doesn't guarantee the data reading is finished during the check.
echo ""
echo "Test 3: Reading single dataset in a single file in C only with thread pool"
./posix_read_tpool -t ${NTHREADS} -d ${DIM1}x${DIM2} -m ${NSTEPS_QUEUE}
