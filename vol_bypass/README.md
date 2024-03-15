### HDF5 Dependency
This VOL connector was tested with the version of the HDF5 1.14.2 release.

**Note**: Make sure you have libhdf5 shared dynamic libraries in your hdf5/lib. For Linux, it's libhdf5.so, for OSX, it's libhdf5.dylib.

### Generate HDF5 shared library
If you don't have the shared dynamic libraries, you'll need to reinstall HDF5.
- Get the 1.14.2 release of HDF5;
- In the repo directory, run ./autogen.sh
- Before building the library, you need to disable the free list (In H5FLprivate.h, uncomment the line "#define H5_NO_FREE_LISTS").
- In your build directory, run configure and make sure to enable thread safety.  To benchmark performance, you also need to build the optimization.  For example:
    >    ../hdf5/configure --enable-threadsafe --disable-hl --enable-build-mode=production
- make; make install

### Settings
If using the Makefile directly, change following paths in the Makefile of the Bypass VOL:

- **HDF5_DIR**: path to your hdf5 install/build location, such as hdf5_build/hdf5/
- **SRC_DIR**: path to this VOL connector source code directory.

If using CMake, change the following path through ccmake or cmake:

- **CMAKE_INSTALL_PREFIX**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

### Build the Bypass VOL library
Type *make* in the source dir and you'll see **libh5bypass_vol.so** (on Linux) or **libh5bypass_vol.dylib** (on Mac OS), which is the Bypass VOL connector library.
To run the demo, set following environment variables first:
>
    export HDF5_PLUGIN_PATH=PATH_TO_YOUR_bypass_vol
    export HDF5_VOL_CONNECTOR="bypass under_vol=0;under_info={};"
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:PATH_TO_YOUR_hdf5_build/hdf5/lib:$HDF5_PLUGIN_PATH

    on MacOS, use DYLD_LIBRARY_PATH instead of LD_LIBRARY_PATH

By default, the debugging mode is disabled to avoid debugging info being printed out.  To ensure the VOL connector is working, simply add the $(DEBUG) option from the CC line in Makefile, and rerun make.

After add the $(DEBUG) option, you can run "**HDF5_DIR**/bin/h5ls sample.h5", and it should show many lines of debugging info like:
>
    ------- BYPASS VOL INIT
    ------- BYPASS VOL INFO String To Info
    ------- BYPASS VOL INFO Copy

### Build the performance benchmark and run it
The benchmark program is h5_read.c under test/ directory.  Simply use the Makefile provided and change the following path:

- **HDF5_DIR**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

If using CMake, change the following path through ccmake or cmake:

- **CMAKE_INSTALL_PREFIX**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

There are scripts to run the benchmark: run_contiguous_simple.sh, run_chunk_simple.sh, and run_multi_dsets.sh, and run_multi_files.sh.  To run them correctly, you must modify the three environment variables (HDF5_PLUGIN_PATH, HDF5_VOL_CONNECTOR, and LD_LIBRARY_PATH) in them.

To run it with this VOL by hand, you must set the three environment variables mentioned above (HDF5_PLUGIN_PATH, HDF5_VOL_CONNECTOR, and LD_LIBRARY_PATH).  Example commands are as below:
>
    ./h5_read -t 4 -d 1024x1024
    ./h5_read -t 4 -d 1024x1024 -n 8
    ./h5_read -t 8 -d 1024x1024 -f 16

The first command creates an HDF5 file with a single dataset (1024 by 1024 elements) of integer data type, then reads the dataset with 4 threads.  The second command creates an HDF5 file with 8 datasets.  Each thread reads 2 entire datasets.  The third command creates 16 HDF5 files with a single dataset in each file.  Then each thread reads the entire dataset in 2 files.

To run the benchmark without this VOL by hand, you need to open another terminal without the three environment variables defined.  Then run the same commands above.

The full list of command line options are as follow:
>
    % ./h5_read --help     

    Help page:
        [-h] [-c --readInC] [-d --dimsDset] [-f --nFiles] [-k --checkData] [-n --nDsets] [-t --nThreads]
        [-h --help]: this help page
        [-c --readInC]: read the data with C functions. The default is false.
        [-d --dimsDset]: this 2D dimensions of the datasets.  The default is 10 x 10.
        [-f --nFiles]: for testing multiple files, this number must be a multiple of the number of threads.  The default is 1.
        [-k --checkData]: make sure the data is correct while not running for benchmark. The default is false.
        [-n --nDsets]: number of datasets in a single file.  The default is 1.
        [-t --nThreads]: number of threads.  The default is 1. 
