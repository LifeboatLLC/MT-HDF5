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
- If you check out the HDF5 source code from Lifeboat's GitHub repo (https://github.com/LifeboatLLC/Experimental), make sure to check out the 1_14_2_multithread branch.
  It contains --enable-multithread option.  Currently, it's the same as --enable-threadsafe.

### Settings
If using the Makefile directly, change following paths in the Makefile of the Bypass VOL:

- **HDF5_DIR**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

If using CMake, change the following path through ccmake or cmake:

- **CMAKE_INSTALL_PREFIX**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

### Build the Bypass VOL library
Type *make* in the source dir and you'll see **libh5bypass_vol.so** (on Linux) or **libh5bypass_vol.dylib** (on Mac OS), which is the Bypass VOL connector library.

### Build the performance benchmark and run it
The benchmark programs are under test/ directory.  Simply use the Makefile provided and change the following path:

- **HDF5_DIR**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

If using CMake, change the following path through ccmake or cmake:

- **CMAKE_INSTALL_PREFIX**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

There are scripts to run the benchmark: run_chunk_simple.sh and run_contiguous_simple.sh (run_multi_dsets.sh and run_multi_files.sh don't work yet).  To run them correctly, you must modify the three environment variables (HDF5_PLUGIN_PATH, HDF5_VOL_CONNECTOR, and LD_LIBRARY_PATH) in them.  Two other environment variables adjust the number of threads for the thread pool in Bypass VOL (BYPASS_VOL_NTHREADS) and the number of steps for the thread pool queue (BYPASS_VOL_NSTEPS).

The full list of command line options for the test programs are as follow:
>
    % ./h5_read --help     

    Help page:
	[-h] [-c --dimsChunk] [-d --dimsDset] [-e --enableChunkCache] [-f --nFiles] [-k --checkData] [-m -stepSize] [-n --nDsets] [-r --randomData] [-s --spaceSelect] [-t --nThreads]
	[-h --help]: this help page
	[-c --dimsChunk]: the 2D dimensions of the chunks.  The default is no chunking.
	[-d --dimsDset]: the 2D dimensions of the datasets.  The default is 1024 x 1024.
	[-e --enableChunkCache]: enable chunk cache for better data I/O performance in HDF5 library (not in Bypass VOL). The default is disabled.
	[-f --nFiles]: for testing multiple files, this number must be a multiple of the number of threads.  The default is 1.
	[-k --checkData]: make sure the data is correct while not running for benchmark. The default is false.
	[-m --stepSize]: the number of data pieces passed into the thread pool.  The default is 1.
	[-n --nDsets]: number of datasets in a single file.  The default is 1.
	[-r --randomData]: the data has random values. The default is false.
	[-s --spaceSelect]: hyperslab selection of data space.  The default is the rows divided by the number of threads (value 1)
		The other options are each thread reads a row alternatively (value 2) and the columns divided by the number of threads (value 3)
	[-t --nThreads]: number of child threads in addition to the main process.  The default is 1.
