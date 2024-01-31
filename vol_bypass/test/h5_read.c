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

typedef struct {
    int   thread_id;
    int   num_files;
    hid_t file_id;
    hid_t dset_id;
    int   *data; 
} args_t;

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading partial dataset with HDF5 in the case of a single
 * dataset in a single file
 *------------------------------------------------------------
 */
void* read_partial_dset_with_hdf5(void* arg)
{
    int thread_id = ((args_t *)arg)->thread_id;
    int *data = ((args_t *)arg)->data;
    hid_t dataset = ((args_t *)arg)->dset_id;
    hid_t dataspace, memspace;

    hsize_t dimsm[2];    /* memory space dimensions */
    hsize_t count[2], block[2];
    hsize_t offset[2], stride[2];
    herr_t  status;

    dataspace = H5Dget_space(dataset); /* dataspace handle */

    /* Define the memory dataspace. */
    dimsm[0] = hand.dset_dim1;
    dimsm[1] = hand.dset_dim2;

    memspace = H5Screate_simple(RANK, dimsm, NULL);

    if (hand.space_select == 1) {
	/* Define hyperslab in the dataset. Each thread reads a few rows of data. The number of rows must be 
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
	offset[0] = thread_id * (hand.dset_dim1 / hand.num_threads);
	offset[1] = 0;
	count[0]  = hand.dset_dim1 / hand.num_threads;
	count[1]  = hand.dset_dim2;

	status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);
        status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset, NULL, count, NULL);
    } else if (hand.space_select == 2) {
	/* Define hyperslab in the dataset.  Each thread reads every other row of data.  The number of rows must 
	 * be evenly divided by the number of threads.
	 * 
	 *     0 ... ...
	 *     1 ... ...
	 *     2 ... ...
	 *     3 ... ...
	 *     0 ... ...
	 *     1 ... ...
	 *       ... ...
	 */
	offset[0] = thread_id;
	offset[1] = 0;
	stride[0] = hand.num_threads;
	stride[1] = 1;
	count[0]  = hand.dset_dim1 / hand.num_threads;;
	count[1]  = 1;
	block[0]  = 1;
	block[1]  = hand.dset_dim2;

	status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, stride, count, block);
        status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset, stride, count, block);
    } else if (hand.space_select == 3) {
	/* Define hyperslab in the dataset. Each thread reads a few columns of data. The number of columns 
	 * must be evenly divided by the number of threads.
	 *
	 *    0 0 1 1 2 2 3 3
	 *    0 0 1 1 2 2 3 3
	 *    0 0 1 1 2 2 3 3
	 *    0 0 1 1 2 2 3 3
	 *    ... ...
	 *    
	 */
	offset[0] = 0;
	offset[1] = thread_id * (hand.dset_dim2 / hand.num_threads);
	count[0]  = hand.dset_dim1;
	count[1]  = hand.dset_dim2 / hand.num_threads;

	status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);
        status = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset, NULL, count, NULL);
    }

    //printf("thread_id = %d, offset[0]=%llu, offset[1]=%llu, count[0]=%llu, count[1] = %llu\n", thread_id, offset[0], offset[1], count[0], count[1]);

    /* Read data from hyperslab in the file into the hyperslab in memory. */
    status = H5Dread(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

    if (status < 0)
        printf("H5Dread failed\n");

    H5Sclose(memspace);
    H5Sclose(dataspace);

    return NULL;
}

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading multiple datasets with HDF5 in the case of multiple
 * datasets in a single file
 *------------------------------------------------------------
 */
void* read_multiple_dsets_with_hdf5(void* arg)
{
    int thread_id = ((args_t *)arg)->thread_id;
    hid_t file = ((args_t *)arg)->file_id;
    char dset_name[1024];
    int *data, *p;
    hsize_t dimsm[2];    /* memory space dimensions */
    hid_t dataset;
    hid_t dataspace, memspace;
    htri_t is_regular_hyperslab;
    hsize_t count[2];      /* size of the hyperslab in the file */
    hsize_t offset[2];     /* hyperslab offset in the file */
    hsize_t count_out[2];  /* size of the hyperslab in memory */
    hsize_t offset_out[2]; /* hyperslab offset in memory */
    int nerrors = 0;
    int i, j, k;

    data = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int)); /* output buffer */

    if (!data)
        printf("data_out is NULL\n");

    for (k = 0; k < hand.num_dsets / hand.num_threads; k++) {
        sprintf(dset_name, "%s%d", DATASETNAME, (k * hand.num_threads + thread_id + 1));

        //printf("thread_id=%d, file=%lld, dset_name=%s\n", thread_id, file, dset_name);

        /* Open the dataset */
        dataset = H5Dopen2(file, dset_name, H5P_DEFAULT);

        dataspace = H5Dget_space(dataset); /* dataspace handle */

        /* Define the memory dataspace */
        dimsm[0] = hand.dset_dim1;
        dimsm[1] = hand.dset_dim2;
        memspace = H5Screate_simple(RANK, dimsm, NULL);

        /* Select the whole dataset as one hyperslab in the dataset and the memory so that 
         * the log file contains the number of elements to be read in C */
        offset[0] = 0;
        offset[1] = 0;
        count[0]  = hand.dset_dim1;
        count[1]  = hand.dset_dim2;

        H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);
        H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset, NULL, count, NULL);

        /* Read data */
        H5Dread(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

        /* Close/release resources */
        H5Sclose(dataspace);
        H5Sclose(memspace);
        H5Dclose(dataset);

        //printf("thread_id=%d. dset_name=%s, data from read_multiple_dsets_with_hdf5 = \n", thread_id, dset_name);

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
            nerrors = check_data(data, (k * hand.num_threads + thread_id));
    
        if (nerrors > 0)
            printf("%d errors during data verification at line %d\n", nerrors, __LINE__);
    }

    free(data);

    return NULL;
}

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading multiple files with HDF5 in the case of a single 
 * dataset in multiple files
 *------------------------------------------------------------
 */
void* read_multiple_files_with_hdf5(void* arg)
{
    int thread_id = ((args_t *)arg)->thread_id;
    char file_name[1024];
    int *data, *p;
    hsize_t dimsm[2];    /* memory space dimensions */
    hid_t file, dataset;
    hid_t dataspace, memspace;
    htri_t is_regular_hyperslab;
    hsize_t count[2];      /* size of the hyperslab in the file */
    hsize_t offset[2];     /* hyperslab offset in the file */
    hsize_t count_out[2];  /* size of the hyperslab in memory */
    hsize_t offset_out[2]; /* hyperslab offset in memory */
    int nerrors = 0;
    int i, j, k;

    data = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int)); /* output buffer */

    if (!data)
        printf("data_out is NULL\n");

    for (k = 0; k < hand.num_files / hand.num_threads; k++) {
        sprintf(file_name, "%s%d.h5", FILE_NAME, (k * hand.num_threads + thread_id + 1));

        /* Open the file and the dataset */
        file    = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);
        dataset = H5Dopen2(file, DATASETNAME, H5P_DEFAULT);

        dataspace = H5Dget_space(dataset); /* dataspace handle */

        /* Define the memory dataspace */
        dimsm[0] = hand.dset_dim1;
        dimsm[1] = hand.dset_dim2;
        memspace = H5Screate_simple(RANK, dimsm, NULL);

        /* Select the whole dataset as one  hyperslab in the dataset and memory so that 
         * the log file contains the number of elements to be read in C */
        offset[0] = 0;
        offset[1] = 0;
        count[0]  = hand.dset_dim1;
        count[1]  = hand.dset_dim2;

        H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);
        H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset, NULL, count, NULL);

        /* Read data */
        H5Dread(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

        /* Close/release resources */
        H5Sclose(dataspace);
        H5Sclose(memspace);
        H5Dclose(dataset);
        H5Fclose(file);

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
            nerrors = check_data(data, (k * hand.num_threads + thread_id));
   
        if (nerrors > 0)
            printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);

    }

    free(data);

    return NULL;
}

/*------------------------------------------------------------
 * Start to test the case of a single dataset in a single file
 * with multi-thread with HDF5
 *------------------------------------------------------------
 */
int
launch_single_file_single_dset_read(void)
{
    int         fp;
    hid_t       file;
    hid_t       dataset; /* handles */
    int         i, j, k;
    pthread_t threads[hand.num_threads];
    int thread_ids[hand.num_threads];
    args_t info[hand.num_threads];
    int *p, *data_out = NULL;
    char file_name[1024];
    int nerrors = 0;

    struct timeval begin, end;

    data_out = (int *)calloc(hand.dset_dim1 * hand.dset_dim2, sizeof(int)); /* output buffer */

    if (!data_out) {
        printf("data_out is NULL\n");
        goto error;
    }

    /* Make sure the info.log file is deleted before running each round */
    if (access("info.log", F_OK) == 0 && remove("info.log") != 0) {
	printf("unable to delete existing info.log.  Must delete it by hand before running this test.\n");
	exit(1);
    }

    /* Open the file and the dataset */
    sprintf(file_name, "%s.h5", FILE_NAME);

    file    = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);
    dataset = H5Dopen2(file, DATASETNAME, H5P_DEFAULT);

    gettimeofday(&begin, 0);

    /* Create threads to read the data */
    for (i = 0; i < hand.num_threads; i++) {
	info[i].thread_id = i;
	info[i].dset_id = dataset;
	info[i].data = data_out;
	pthread_create(&threads[i], NULL, read_partial_dset_with_hdf5, &info[i]);
    }

    /* Wait for threads to complete */
    for (i = 0; i < hand.num_threads; i++) {
	pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, 0);

    save_statistics(begin, end);

    /* Data verification if enabled */
    if (hand.check_data && !hand.random_data)
	nerrors = check_data(data_out, 0);

    if (nerrors > 0)
	printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);

    /* Close/release resources */
    H5Dclose(dataset);
    H5Fclose(file);

    free(data_out);

    return 0;

error:
    return -1;
}

/*------------------------------------------------------------
 * Start to test the case of multiple datasets in a single file
 * with multi-thread with HDF5
 *------------------------------------------------------------
 */
int
launch_single_file_multiple_dset_read()
{
    hid_t       file;
    hid_t       dataset; /* handles */
    int         i, j, k;
    pthread_t threads[hand.num_threads];
    int thread_ids[hand.num_threads];
    args_t info[hand.num_threads];
    int *p, *data_out = NULL;
    char file_name[1024];
    int fp;
    struct timeval begin, end;

    sprintf(file_name, "%s.h5", FILE_NAME);

    /* Make sure the info.log file is deleted before running each round */
    if (access("info.log", F_OK) == 0 && remove("info.log") != 0) {
	printf("unable to delete existing info.log.  Must delete it by hand before running this test.\n");
	exit(1);
    }

    /* Open the file */
    file = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

    gettimeofday(&begin, 0);

    /* Create threads to read the multiple datasets */
    for (i = 0; i < hand.num_threads; i++) {
	info[i].thread_id = i;
	info[i].file_id = file;
	pthread_create(&threads[i], NULL, read_multiple_dsets_with_hdf5, &info[i]);
    }

    /* Wait for threads to complete */
    for (i = 0; i < hand.num_threads; i++) {
	pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, 0);

    save_statistics(begin, end);

    H5Fclose(file);

    return 0;

error:
    return -1;
}

/*------------------------------------------------------------
 * Start to test the case of a single dataset in multiple files
 * with multi-thread with HDF5 and C
 *------------------------------------------------------------
 */
int
launch_multiple_file_read(int num_files)
{
    pthread_t threads[hand.num_threads];
    int thread_ids[hand.num_threads];
    args_t info[hand.num_threads];
    int i, j;
    int *data_out = NULL;
    struct timeval begin, end;

    /* Make sure the info.log file is deleted before running each round */
    if (access("info.log", F_OK) == 0 && remove("info.log") != 0) {
	printf("unable to delete existing info.log.  Must delete it by hand before running this test.\n");
	exit(1);
    }

    gettimeofday(&begin, 0);

    /* Create threads to read the multiple files with HDF5 */
    for (i = 0; i < hand.num_threads; i++) {
	info[i].thread_id = i;
	info[i].num_files = num_files;

	pthread_create(&threads[i], NULL, read_multiple_files_with_hdf5, &info[i]);
    }

    /* Wait for threads to complete */
    for (i = 0; i < hand.num_threads; i++) {
	pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, 0);

    save_statistics(begin, end);

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

    /* Make sure the info.log file is deleted before running the test */
    if (access("info.log", F_OK) == 0 && remove("info.log") != 0) {
	printf("unable to delete existing info.log.  Must delete it by hand before running this test.\n");
	exit(1);
    }

    if (hand.num_files == 1 && hand.num_dsets == 1)
        launch_single_file_single_dset_read();
    else if (hand.num_files == 1 && hand.num_dsets > 1)
        launch_single_file_multiple_dset_read();
    else if (hand.num_files > 1)
        launch_multiple_file_read(hand.num_files);

    /* Print out the performance statistics */
    report_statistics();

    free(file_info);

    return 0;
}
