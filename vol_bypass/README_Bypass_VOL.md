### HDF5 Dependency
This VOL connector was tested with the version of the HDF5 1.14.1 release.

**Note**: Make sure you have libhdf5 shared dynamic libraries in your hdf5/lib. For Linux, it's libhdf5.so, for OSX, it's libhdf5.dylib.

### Generate HDF5 shared library
If you don't have the shared dynamic libraries, you'll need to reinstall HDF5.
- Get the 1.14.1 release of HDF5;
- In the repo directory, run ./autogen.sh
- Before building the library, you need to disable the free list (In H5FLprivate.h, uncomment the line "#define H5_NO_FREE_LISTS").
- In your build directory, run configure and make sure to enable thread safety.  To benchmark performance, you also need to build the optimization.  For example:
    >    ../hdf5/configure --enable-threadsafe --disable-hl --enable-build-mode=production
- make; make install

### Settings
Change following paths in the Makefile of the Bypass VOL:

- **HDF5_DIR**: path to your hdf5 install/build location, such as hdf5_build/hdf5/
- **SRC_DIR**: path to this VOL connector source code directory.

### Build the Bypass VOL library and run the demo
Type *make* in the source dir and you'll see **libh5passthrough_vol.so** (on Linux) or **libh5passthrough_vol.dylib** (on Mac OS), which is the Bypass VOL connector library.
To run the demo, set following environment variables first:
>
    export HDF5_PLUGIN_PATH=PATH_TO_YOUR_pass_through_vol
    export HDF5_VOL_CONNECTOR="pass_through_ext under_vol=0;under_info={};"
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:PATH_TO_YOUR_hdf5_build/hdf5/lib:$HDF5_PLUGIN_PATH

    on MacOS, use DYLD_LIBRARY_PATH instead of LD_LIBRARY_PATH

### Build the performance benchmark and run it
The benchmark programs are h5_create.c, h5_read.c, and posix_read.c under test/ directory.  Simply run make to compile them.  There are several scripts to run the benchmark: run_contiguous_simple.sh, run_contiguous_simple.sh, run_multi_dsets.sh, and run_multi_files.sh.  To run them correctly, you must modify the three environment variables (HDF5_PLUGIN_PATH, HDF5_VOL_CONNECTOR, and LD_LIBRARY_PATH) in them.

To run the benchmark program with this VOL by hand, you must set the three environment variables mentioned above (HDF5_PLUGIN_PATH, HDF5_VOL_CONNECTOR, and LD_LIBRARY_PATH).  Example commands are as below:
>
    create a file with a contiguous dataset:	./h5_create -d 1024x1024
    read the file above with four threads:	./h5_read -t 4 -d 1024x1024
    create a file with a chunked dataset:	./h5_create -d 1024x1024 -c 16x16
    read the file above with four threads:	./h5_read -t 4 -d 1024x1024 -c 16x16

The first command creates an HDF5 file with a single contiguous dataset (1024 by 1024 elements) of integer data type.  The second command reads the dataset with 4 threads.  The third command creates an HDF5 file with a chunked dataset (chunk size is 16x16).  The fourth command read the chunked dataset with 4 threads. 

To run the benchmark without this VOL by hand, you need to open another terminal without the three environment variables defined.  Then run the same commands above.

The full list of command line options are as follow:
>
    % ./h5_read -h  

    Help page:
        [-h] [-c --dimsChunk] [-d --dimsDset] [-f --nFiles] [-k --checkData] [-n --nDsets] [-r --randomData] [-s --spaceSelect] [-t --nThreads]
        [-h --help]: this help page
        [-c --dimsChunk]: the 2D dimensions of the chunks.  The default is no chunking.
        [-d --dimsDset]: the 2D dimensions of the datasets.  The default is 1024 x 1024.
        [-f --nFiles]: for testing multiple files, this number must be a multiple of the number of threads.  The default is 1.
        [-k --checkData]: make sure the data is correct while not running for benchmark. The default is false.
        [-n --nDsets]: number of datasets in a single file.  The default is 1.
        [-r --randomData]: the data has random values. The default is false.
        [-s --spaceSelect]: hyperslab selection of data space.  The default is the rows divided by the number of threads (value 1)
                The other options are each thread reads a row alternatively (value 2) and the columns divided by the number of threads (value 3)
        [-t --nThreads]: number of threads.  The default is 1.
