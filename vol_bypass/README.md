### HDF5 Dependency
This VOL connector was tested with the version of the HDF5 1.14.2 release with multithread support, which can be found in Lifeboat's Git repo (https://github.com/LifeboatLLC/hdf5_lifeboat)

**Note**: Make sure you have libhdf5 shared dynamic libraries in your hdf5/lib. For Linux, it's libhdf5.so, for Mac OS, it's libhdf5.dylib.

### Generate HDF5 shared library
If you don't have the shared dynamic libraries, you'll need to reinstall HDF5.

- Get the 1.14.2 release of HDF5 with multithread support located at https://github.com/LifeboatLLC/hdf5_lifeboat (the 1_14_2_multithread branch);
- In the HDF5 source directory, run: ./autogen.sh
- In your build directory, run configure and make sure to enable multithread feature.  To benchmark performance, you need to build the production.  You only need the shared dynamic library.  So remember to disable the static library.  For example:
    >    ../hdf5/configure --enable-multithread --disable-hl --enable-build-mode=production --disable-static
- Compile and install the library: make; make install

### Settings
The Git repo of the Bypass VOL is at https://github.com/LifeboatLLC/MT-HDF5.

If you're using the Makefile directly, change following paths in the Makefile of the Bypass VOL:

- **HDF5_DIR**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

If you're using CMake, change the following path through ccmake or cmake:

- **CMAKE_INSTALL_PREFIX**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

### Build the Bypass VOL library
Type *make* in the source dir and you'll see **libh5bypass_vol.so** (on Linux) or **libh5bypass_vol.dylib** (on Mac OS), which is the Bypass VOL connector library.

### Build the performance benchmark and run it
The benchmark programs are under test/ directory.  Simply use the Makefile provided and change the following path:

- **HDF5_DIR**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

If you're using CMake, change the following path through ccmake or cmake:

- **CMAKE_INSTALL_PREFIX**: path to your hdf5 install/build location, such as hdf5_build/hdf5/

Below are the scripts to run the benchmark:

- run_chunk_simple.sh
- run_contiguous_simple.sh
- run_multi_dsets.sh
- run_multi_files.sh

To run them correctly, you must modify the three environment variables in these scripts:

- **HDF5_PLUGIN_PATH**: the path to the Bypass VOL library
- **HDF5_VOL_CONNECTOR**: the name of the Bypass VOL
- **DYLD_LIBRARY_PATH**(Mac) or **LD_LIBRARY_PATH**(Linux): the paths to the HDF5 library and the Bypass VOL library

There are four other environment variables to be passed into the Bypass VOL:

- **BYPASS_VOL_NTHREADS**:   adjust the number of threads for the thread pool in Bypass VOL
- **BYPASS_VOL_NSTEPS**:     the number of tasks passed into the thread pool queue each time (tasks are processed by the thread pool in batches)
- **BYPASS_VOL_MAX_NELMTS**: the maximal number of data elements (not bytes) for each data pieces to be read
- **BYPASS_VOL_NO_TPOOL**:   if set to be true, the thread pool is not used.  The default is false that the thread pool is used.

The full list of command line options for the test programs are as follow:
>
    % ./h5_read --help     

    Help page:
	    [-h] [-c --dimsChunk] [-d --dimsDset] [-e --enableChunkCache] [-f --nFiles] [-k --checkData] [-m -stepSize] [-n --nDsets] [-q --nSections] [-r --randomData] [-s --spaceSelect] [-t --nThreads]
	    [-h --help]: this help page
	    [-c --dimsChunk]: the 2D dimensions of the chunks.  The default is no chunking.
	    [-d --dimsDset]: the 2D dimensions of the datasets.  The default is 1024 x 1024.
	    [-e --enableChunkCache]: enable chunk cache for better data I/O performance in HDF5 library (not in Bypass VOL). The default is disabled.
	    [-f --nFiles]: for testing multiple files, this number must be a multiple of the number of threads.  The default is 1.
	    [-k --checkData]: make sure the data is correct while not running for benchmark. The default is false.
	    [-l --multiDsets]: read multiple datasets using H5Dread_multi. The default is false.
	    [-m --stepSize]: the number of data pieces passed into the thread pool.  The default is 1.
	    [-n --nDsets]: number of datasets in a single file.  The default is 1.
	    [-q --nSections]: number of data sections to break down a large dataset.  The default is 1.
	    [-r --randomData]: the data has random values. The default is false.
	    [-s --spaceSelect]: hyperslab selection of data space.  The default is the rows divided by the number of threads - value 1
		    The other options are unsurppoted
	    [-t --nThreads]: number of child threads in addition to the main process.  The default is 1.

