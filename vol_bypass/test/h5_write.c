/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by Lifeboat, LLC                                                *
 * All rights reserved.                                                      *
 *                                                                           *
 * The full copyright notice, including terms governing use, modification,   *
 * and redistribution, is contained in the COPYING file, which can be found  *
 * at the root of the source code distribution tree.                         *
 * If you do not have access to either file, you may request a copy from     *
 * help@lifeboat.llc                                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Benchmark the performance of data writing
 */
#include "common.h"
#include "common.c"
#include "hdf5.h"

#define FILE_NAME   "test_write_file"
#define DATASETNAME "dset"
#define RANK        2

typedef struct {
    int   thread_id;
    int   num_files;
    hid_t file_id;
    hid_t dset_id;
    hid_t *dset_id_list;        /* Temporary ID list for reading multiple datasets */
    hid_t *file_id_list;        /* Temporary ID list for reading multiple files    */
    int   data_section_id;
    int   *data; 
} args_t;

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading partial dataset with HDF5 in the case of a single
 * dataset in a single file
 *------------------------------------------------------------
 */
void* write_partial_dset_with_hdf5(void* arg)
{
    int thread_id = ((args_t *)arg)->thread_id;
    int *data = ((args_t *)arg)->data;
    hid_t dataset = ((args_t *)arg)->dset_id;
    int data_section_id = ((args_t *)arg)->data_section_id;
    hid_t dataspace, memspace;

    hsize_t dimsm[2];    /* memory space dimensions */
    hsize_t count[2], block[2];
    hsize_t offset[2], stride[2];
    hsize_t mcount[2], mblock[2];
    hsize_t moffset[2], mstride[2];
    bool    data_buf_allocated = false;
    int     nerrors = 0;
    herr_t  status;
    int     i;

    dataspace = H5Dget_space(dataset); /* dataspace handle */

    if (data_in_section) {
	/* Define the memory dataspace */
	dimsm[0] = hand.dset_dim1 / hand.num_data_sections;
	dimsm[1] = hand.dset_dim2;
    } else {
	/* Define the memory dataspace. */
	dimsm[0] = hand.dset_dim1;
	dimsm[1] = hand.dset_dim2;
    }

    memspace = H5Screate_simple(RANK, dimsm, NULL);

    /* Currently, only selection by row is supported */
    if (hand.space_select == 1) {
	/* Define hyperslab in the dataset. Each thread reads a few rows of data.
         * The number of rows must be * evenly divided by the number of threads.
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
        if (data_in_section) {
	    if (hand.num_threads == 0) {
		offset[0] = data_section_id * (hand.dset_dim1 / hand.num_data_sections);
		count[0]  = hand.dset_dim1 / hand.num_data_sections;
	    } else {
		offset[0] = data_section_id * (hand.dset_dim1 / hand.num_data_sections) +
			    thread_id * (hand.dset_dim1 / (hand.num_data_sections * hand.num_threads));
		count[0]  = hand.dset_dim1 / (hand.num_data_sections * hand.num_threads);
	    }

	    offset[1] = 0;
	    count[1]  = hand.dset_dim2;

	    status = H5Sselect_none(dataspace);
	    status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);

	    /* Selection in memory */
	    if (hand.num_threads == 0) {
		moffset[0] = 0;
		mcount[0]  = hand.dset_dim1 / hand.num_data_sections;
	    } else {
		moffset[0] = thread_id * (hand.dset_dim1 / (hand.num_data_sections * hand.num_threads));
		mcount[0]  = hand.dset_dim1 / (hand.num_data_sections * hand.num_threads);
	    }

	    moffset[1] = 0;
	    mcount[1]  = hand.dset_dim2;

	    status = H5Sselect_none(memspace);
	    status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, moffset, NULL, mcount, NULL);

	    /* Read data from hyperslab in the file into the memory. */
	    status = H5Dwrite(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

	    if (status < 0)
		printf("H5Dwrite failed\n");
        } else {
            if (hand.num_threads == 0) {
	        offset[0] = 0;
	        count[0]  = hand.dset_dim1;
            } else {
	        offset[0] = thread_id * (hand.dset_dim1 / hand.num_threads);
	        count[0]  = hand.dset_dim1 / hand.num_threads;
            }

	    offset[1] = 0;
	    count[1]  = hand.dset_dim2;

//printf("In %s, thread_id=%d: offset[0]=%d, offset[1]=%d, count[0]=%d, count[1]=%d\n", __func__, thread_id, offset[0], offset[1], count[0], count[1]);
	    status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);
	    status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset, NULL, count, NULL);

	    /* Read data from hyperslab in the file into the hyperslab in memory. */
	    status = H5Dwrite(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

	    if (status < 0)
		printf("H5Dwrite failed\n");
        }
    }

    H5Sclose(memspace);
    H5Sclose(dataspace);

    if (data_buf_allocated)
        free(data);

    return NULL;
} /* write_partial_dset_with_hdf5 */

/*------------------------------------------------------------
 * Start to test the case of a single dataset in a single file
 * with multi-thread with HDF5
 *------------------------------------------------------------
 */
int
launch_single_file_single_dset_write(void)
{
    int         fp;
    hid_t       file, fapl, dcpl;
    hid_t       dataset; /* handles */
    int         mdc_nelmts;
    size_t      rdcc_nelmts;
    size_t      rdcc_nbytes;
    double      rdcc_w0;
    hid_t   datatype, dataspace; /* handles */
    hsize_t dimsm[2];
    hsize_t dimsf[2];            /* dataset dimensions */
    hsize_t chunk_dims[2];       /* chunk dimensions */
    hsize_t count[2], offset[2];
    herr_t  status;

    int         i, j, k;
    pthread_t   threads[hand.num_threads];
    int         thread_ids[hand.num_threads];
    int         *p, *data_out = NULL;
    char        file_name[1024];
    char        dset_name[1024];
    H5D_alloc_time_t alloc_time = H5D_ALLOC_TIME_EARLY;
    int         nerrors = 0;

    struct timeval begin, end;

    if ((fapl = H5Pcreate(H5P_FILE_ACCESS)) < 0) {
        printf("H5Pcreate failed at line %d\n", __LINE__);
        goto error;
    }

    /* By default, the chunk cache is turned off for fair performance comparison among the HDF5 library, Bypass VOL, and C only */
    if (!hand.chunk_cache) {
        if (H5Pget_cache(fapl, &mdc_nelmts, &rdcc_nelmts, &rdcc_nbytes, &rdcc_w0) < 0) {
            printf("H5Pget_cache failed at line %d\n", __LINE__);
            goto error;
        }

        rdcc_nbytes = 0;
        if (H5Pset_cache(fapl, mdc_nelmts, rdcc_nelmts, rdcc_nbytes, rdcc_w0) < 0) {
            printf("H5Pset_cache failed at line %d\n", __LINE__);
            goto error;
        }
    }

    dcpl = H5Pcreate(H5P_DATASET_CREATE);

    /* If chunk dimensions are passed in through command line, define the chunk size. */
    if (hand.chunk_dim1 > 0 && hand.chunk_dim2 > 0) {
        chunk_dims[0] = hand.chunk_dim1;
        chunk_dims[1] = hand.chunk_dim2;
          
        H5Pset_chunk(dcpl, RANK, chunk_dims); 
    }

    /* Bypass VOL only handles early allocation */
    if (H5Pset_alloc_time(dcpl, alloc_time) < 0) {
        printf("H5Pset_alloc_time failed at line %d\n", __LINE__);
        goto error;
    }

    /* Create the dataspace */
    dimsf[0]  = hand.dset_dim1;
    dimsf[1]  = hand.dset_dim2;
    dataspace = H5Screate_simple(RANK, dimsf, NULL);

    /* Define datatype for the data in the file */
    datatype = H5Tcopy(H5T_NATIVE_INT);
    status   = H5Tset_order(datatype, H5T_ORDER_LE);

    /* Create the file */
    sprintf(file_name, "%s.h5", FILE_NAME);

    file = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);

    /* Create the dataset */
    sprintf(dset_name, "%s", DATASETNAME);

    dataset = H5Dcreate2(file, dset_name, datatype, dataspace, H5P_DEFAULT, dcpl, H5P_DEFAULT);

    if (hand.num_threads == 0) {
        args_t info;

	/* Break down the dataset into NUM_DATA_SECTIONS sections if the dataset is too big */
        if (hand.num_data_sections > 1) {
            /* Set this global flag, mainly for check_data() */
	    data_in_section = true;

	    data_out = (int *)calloc((hand.dset_dim1 / hand.num_data_sections) * hand.dset_dim2, sizeof(int)); /* output buffer */

	    if (!data_out) {
		printf("data_out is NULL\n");
		goto error;
	    }

	    /* Data buffer initialization */
	    p = data_out;

	    for (j = 0; j < hand.dset_dim1 / hand.num_data_sections; j++) {
		for (i = 0; i < hand.dset_dim2; i++) {
		    if (hand.random_data)
			*p++ = i + j + rand() % 50;
		    else
			*p++ = i + j;
                }
            }

            /* Start the time */
            gettimeofday(&begin, 0);

	    for (j = 0; j < hand.num_data_sections; j++) {
		info.thread_id = 0;
		info.dset_id = dataset;
		info.data_section_id = j;
		info.data = data_out;

	        write_partial_dset_with_hdf5(&info);
            }

            /* Stop the time */
            gettimeofday(&end, 0);
        } else {
	    data_out = (int *)calloc(hand.dset_dim1 * hand.dset_dim2, sizeof(int)); /* output buffer */

	    if (!data_out) {
		printf("data_out is NULL\n");
		goto error;
	    }

	    /* Data buffer initialization */
	    p = data_out;

	    for (j = 0; j < hand.dset_dim1; j++) {
		for (i = 0; i < hand.dset_dim2; i++) {
		    if (hand.random_data)
			*p++ = i + j + rand() % 50;
		    else
			*p++ = i + j;
                }
            }

	    info.thread_id = 0;
	    info.dset_id = dataset;
	    info.data_section_id = 0; /* No section */
	    info.data = data_out;

            /* Start the time */
            gettimeofday(&begin, 0);

	    write_partial_dset_with_hdf5(&info);

            /* Stop the time */
            gettimeofday(&end, 0);
        }
    } else if (hand.num_threads > 0) {
        args_t info[hand.num_threads];
    
	/* Break down the dataset into NUM_DATA_SECTIONS sections if the dataset is too big */
        if (hand.num_data_sections > 1) {
            /* Set this global flag, mainly for check_data() */
	    data_in_section = true;

	    data_out = (int *)calloc((hand.dset_dim1 / hand.num_data_sections) * hand.dset_dim2, sizeof(int)); /* output buffer */

	    if (!data_out) {
		printf("data_out is NULL\n");
		goto error;
	    }

	    /* Data buffer initialization */
	    p = data_out;

	    for (j = 0; j < hand.dset_dim1 / hand.num_data_sections; j++) {
		for (i = 0; i < hand.dset_dim2; i++) {
		    if (hand.random_data)
			*p++ = i + j + rand() % 50;
		    else
			*p++ = i + j;
                }
            }

            /* Start the time */
            gettimeofday(&begin, 0);

            /* Read the data section by section and check its correctness */
	    for (j = 0; j < hand.num_data_sections; j++) {
		/* Create threads to read the data */
		for (i = 0; i < hand.num_threads; i++) {
		    info[i].thread_id = i;
		    info[i].dset_id = dataset;
		    info[i].data_section_id = j;
		    info[i].data = data_out;
		    pthread_create(&threads[i], NULL, write_partial_dset_with_hdf5, &info[i]);
		}

		/* Wait for threads to complete */
		for (i = 0; i < hand.num_threads; i++) {
		    pthread_join(threads[i], NULL);
		}
            }

            /* Stop the time */
            gettimeofday(&end, 0);
        } else {
	    data_out = (int *)calloc(hand.dset_dim1 * hand.dset_dim2, sizeof(int)); /* output buffer */

	    if (!data_out) {
		printf("data_out is NULL\n");
		goto error;
	    }

	    /* Data buffer initialization */
	    p = data_out;

	    for (j = 0; j < hand.dset_dim1; j++) {
		for (i = 0; i < hand.dset_dim2; i++) {
		    if (hand.random_data)
			*p++ = i + j + rand() % 50;
		    else
			*p++ = i + j;
                }
            }

            /* Start the time */
            gettimeofday(&begin, 0);

	    /* Create threads to read the data in the entire buffer */
	    for (i = 0; i < hand.num_threads; i++) {
		info[i].thread_id = i;
		info[i].dset_id = dataset;
		info[i].data = data_out;
		pthread_create(&threads[i], NULL, write_partial_dset_with_hdf5, &info[i]);
	    }

	    /* Wait for threads to complete */
	    for (i = 0; i < hand.num_threads; i++) {
		pthread_join(threads[i], NULL);
	    }

            /* Stop the time */
            gettimeofday(&end, 0);
        }
    }

    /* Calculate and print the performance data */
    save_statistics(begin, end);

    /* Close/release resources */
    H5Sclose(dataspace);
    H5Tclose(datatype);
    H5Pclose(dcpl);

    H5Dclose(dataset);
    H5Fclose(file);
    H5Pclose(fapl);

    free(data_out);

    return 0;

error:
    return -1;
} /* launch_single_file_single_dset_write */

int main(int argc, char **argv)
{
    int i;

    parse_command_line(argc, argv);

    if (hand.num_files == 1 && hand.num_dsets == 1)
        launch_single_file_single_dset_write();
    else if (hand.num_files == 1 && hand.num_dsets > 1)
        ; //launch_single_file_multiple_dset_write();
    else if (hand.num_files > 1)
        ; //launch_multiple_file_write(hand.num_files);

    /* Print out the performance statistics */
    report_statistics();

    return 0;
} /* main */
