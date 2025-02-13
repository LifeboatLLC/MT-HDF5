/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://www.hdfgroup.org/licenses.               *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 *   This example reads hyperslab from the SDS.h5 file
 *   created by h5_write.c program into two-dimensional
 *   plane of the three-dimensional array.
 *   Information about dataset in the SDS.h5 file is obtained.
 */
#include "common.h"
#include "common.c"
#include "hdf5.h"

#define FILE_NAME   "mt_file"
#define DATASETNAME "dset"
#define RANK        2

/*------------------------------------------------------------
 * Create HDF5 file(s) for the test
 *------------------------------------------------------------
 */
int create_files()
{
    char    file_name[1024];
    char    dset_name[1024];
    hid_t   file, dataset;       /* file and dataset handles */
    hid_t   datatype, dataspace; /* handles */
    hid_t   memspace;
    hsize_t dimsm[2];
    hsize_t dimsf[2];            /* dataset dimensions */
    hsize_t chunk_dims[2];       /* chunk dimensions */
    hid_t   dcpl;
    hsize_t count[2], offset[2];
    time_t  t;
    herr_t  status;
    int     *data, *p;
    int     i, j, k, m, n;

    /* Initializing random generator */
    srand((unsigned) time(&t));

    /* Break down the data buffer into DATA_SECTION_NUM sections if the dataset is greater than 16GB */
    if (hand.dset_dim1 * hand.dset_dim2 > (long long int)4 * GB && hand.dset_dim1 % DATA_SECTION_NUM == 0) {
    //if (hand.dset_dim1 * hand.dset_dim2 > (long long int)1 && hand.dset_dim1 % DATA_SECTION_NUM == 0) {
        data_in_section = true;

	/* Define the memory dataspace */
	dimsm[0] = hand.dset_dim1 / DATA_SECTION_NUM;
	dimsm[1] = hand.dset_dim2;

	memspace = H5Screate_simple(RANK, dimsm, NULL);

        data = (int *)malloc((hand.dset_dim1 / DATA_SECTION_NUM) * hand.dset_dim2 * sizeof(int)); /* output buffer */
    } else {
	/* Define the memory dataspace */
	dimsm[0] = hand.dset_dim1;
	dimsm[1] = hand.dset_dim2;

	memspace = H5Screate_simple(RANK, dimsm, NULL);

        data = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int)); /* output buffer */
    }

    dcpl = H5Pcreate(H5P_DATASET_CREATE);

    /* If chunk dimensions are passed in through command line, define the chunk size. */
    if (hand.chunk_dim1 > 0 && hand.chunk_dim2 > 0) {
        chunk_dims[0] = hand.chunk_dim1;
        chunk_dims[1] = hand.chunk_dim2;
          
        H5Pset_chunk(dcpl, RANK, chunk_dims); 
    }

    /* Create the dataspace */
    dimsf[0]  = hand.dset_dim1;
    dimsf[1]  = hand.dset_dim2;
    dataspace = H5Screate_simple(RANK, dimsf, NULL);

    /* Define datatype for the data in the file */
    datatype = H5Tcopy(H5T_NATIVE_INT);
    status   = H5Tset_order(datatype, H5T_ORDER_LE);

    for (k = 0; k < hand.num_files; k++) {
        /* Create a new file using H5F_ACC_TRUNC access */
        if (hand.num_files == 1) 
            sprintf(file_name, "%s.h5", FILE_NAME);
        else
            sprintf(file_name, "%s%d.h5", FILE_NAME, k + 1);

        if ((file = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT)) < 0) {
            fprintf(stderr, "Cannot create file %s\n", file_name);
            return -1;
        }
        /* Create and writes data to dataset(s) */
        for (n = 0; n < hand.num_dsets; n++) {
            if (hand.num_dsets == 1)
                sprintf(dset_name, "%s", DATASETNAME);
            else
                sprintf(dset_name, "%s%d", DATASETNAME, n + 1);

            /* Create a new dataset */
            dataset = H5Dcreate2(file, dset_name, datatype, dataspace, H5P_DEFAULT, dcpl, H5P_DEFAULT);

            /* 
             * dset1        dset2
             * 0 1 2 3 4 5  ... ...
             * 1 2 3 4 5 6
             * 2 3 4 5 6 7
             * 3 4 5 6 7 8
             * 4 5 6 7 8 9
             */

            /* Write the data to the dataset */
            if (data_in_section) {
                for (m = 0; m < DATA_SECTION_NUM; m++) {
		    /* Define hyperslab in the dataset. Each time it writes DATA_SECTION_NUM rows of data. The number of rows must be 
		     * evenly divided by the number of threads.
		     *    
		     *    0 0 0 0 
		     *    0 0 0 0 
		     *    1 1 1 1
		     *    1 1 1 1
		     *    2 2 2 2   
		     *    2 2 2 2   
		     *    3 3 3 3
		     *    3 3 3 3
		     */
		    offset[0] = m * (hand.dset_dim1 / DATA_SECTION_NUM);
		    offset[1] = 0;
		    count[0]  = hand.dset_dim1 / DATA_SECTION_NUM;
		    count[1]  = hand.dset_dim2;

                    status = H5Sselect_none(dataspace);
		    status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);

                    /* Data buffer initialization */
                    p = data;

		    for (j = 0; j < hand.dset_dim1 / DATA_SECTION_NUM; j++)
			for (i = 0; i < hand.dset_dim2; i++)
			    if (hand.random_data)
				*p++ = i + j + rand() % 50;
			    else
				*p++ = i + j + m * 10 + k * hand.dset_dim1 * hand.dset_dim2 + n * hand.dset_dim1 * hand.dset_dim2;

                    status = H5Dwrite(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);
                }
            } else {
		/* Data buffer initialization */
		p = data;

		for (j = 0; j < hand.dset_dim1; j++)
		    for (i = 0; i < hand.dset_dim2; i++)
			if (hand.random_data)
			    *p++ = i + j + rand() % 50;
			else
			    *p++ = i + j + k * hand.dset_dim1 * hand.dset_dim2 + n * hand.dset_dim1 * hand.dset_dim2;

                status = H5Dwrite(dataset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
            }

            H5Dclose(dataset);
        }

        H5Fclose(file);
    }

    /* Close/release resources */
    H5Sclose(memspace);
    H5Sclose(dataspace);
    H5Tclose(datatype);
    H5Pclose(dcpl);

    free(data);

    return 0;

error:
    return -1;
}

/*------------------------------------------------------------
 * Main function
 *------------------------------------------------------------
 */
int
main(int argc, char **argv)
{
    int i;

    parse_command_line(argc, argv);

    /* Create the HDF5 file(s) to be read */
    create_files();

    return 0;
}
