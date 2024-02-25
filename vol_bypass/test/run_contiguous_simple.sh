#! /bin/sh

# Modify the following variables as command line options for the h5_read.c:
#     number of threads
#     dimension one
#     dimension two
NTHREADS=4
DIM1=1024
DIM2=1024
SPACE_SELLECTION=3

# Make sure these two environment variables aren't set in order to run the test without Bypass VOL
unset HDF5_VOL_CONNECTOR
unset HDF5_PLUGIN_PATH

echo "Test 0: Creating a HDF5 file with a single dataset in it"
./h5_create -d ${DIM1}x${DIM2}

echo ""
echo "Test 1: Reading single dataset in a single file with straight HDF5 (no Bypass VOL)"
./h5_read -t ${NTHREADS} -d ${DIM1}x${DIM2} -s ${SPACE_SELLECTION} -k

# Set the environment variables to use Bypass VOL. Need to modify them with your own paths 
export HDF5_PLUGIN_PATH=/Users/raylu/Lifeboat/HDF/vol_bypass
export HDF5_VOL_CONNECTOR="bypass under_vol=0;under_info={};"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/Users/raylu/Lifeboat/HDF/build_hdf5/hdf5/lib:$HDF5_PLUGIN_PATH

echo ""
echo "Test 2: Reading single dataset in a single file with Bypass VOL"
./h5_read -t ${NTHREADS} -d ${DIM1}x${DIM2} -s ${SPACE_SELLECTION} -k

# Disabled this test as the work is still ongoing
# The C test must follow the test with Bypass VOL immediately to use info.log file which contains file name and data info
# echo ""
# echo "Test 3: Reading single dataset in a single file in C only"
# ./posix_read -t ${NTHREADS} -d ${DIM1}x${DIM2} -k
