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
    int     nerrors = 0;
    herr_t  status;
    int     i;

    dataspace = H5Dget_space(dataset); /* dataspace handle */

    /* Break down the data buffer into DATA_SECTION_NUM sections if the dataset is greater than 16GB.  Only allow it
     * when there is no child thread */
    if (hand.dset_dim1 * hand.dset_dim2 > (long long int)4 * GB && hand.dset_dim1 % DATA_SECTION_NUM == 0 && hand.num_threads == 0) {
    //if (hand.dset_dim1 * hand.dset_dim2 > (long long int)1 && hand.dset_dim1 % DATA_SECTION_NUM == 0 && hand.num_threads == 0) {
        data_in_section = true;

	/* Define the memory dataspace */
	dimsm[0] = hand.dset_dim1 / DATA_SECTION_NUM;
	dimsm[1] = hand.dset_dim2;

	memspace = H5Screate_simple(RANK, dimsm, NULL);

        data = (int *)malloc((hand.dset_dim1 / DATA_SECTION_NUM) * hand.dset_dim2 * sizeof(int)); /* output buffer */
    } else {
	/* Define the memory dataspace. */
	dimsm[0] = hand.dset_dim1;
	dimsm[1] = hand.dset_dim2;

	memspace = H5Screate_simple(RANK, dimsm, NULL);

        data = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int)); /* output buffer */
    }

    if (hand.space_select == 1) {
        if (data_in_section) {
	    for (i = 0; i < DATA_SECTION_NUM; i++) {
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
		offset[0] = i * (hand.dset_dim1 / DATA_SECTION_NUM);
		offset[1] = 0;
		count[0]  = hand.dset_dim1 / DATA_SECTION_NUM;
		count[1]  = hand.dset_dim2;

		status = H5Sselect_none(dataspace);
		status = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset, NULL, count, NULL);

		/* Read data from hyperslab in the file into the memory. */
		status = H5Dread(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

		if (status < 0)
		    printf("H5Dread failed\n");

		/* Data verification if enabled.  Do not enable this option (-k) for performance study */
		if (hand.check_data && !hand.random_data) {
		    /* Checking the correctness of the data if there are more than one H5Dread may not pass because the thread pool may still be 
		     * reading the data during the check.  The way thread pool is set up doesn't guarantee the data reading is finished during the check.
		     * Sleeping for a while may give the thread pool enough time to finish reading the data.
		     */
		    //sleep(1);

		    nerrors = check_data(data, 0, i);
                }

		if (nerrors > 0)
		    printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);
	    }
        } else {
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
	    status = H5Dread(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

	    if (status < 0)
		printf("H5Dread failed\n");
        }
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

	/* Read data from hyperslab in the file into the hyperslab in memory. */
	status = H5Dread(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

	if (status < 0)
	    printf("H5Dread failed\n");
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

	/* Read data from hyperslab in the file into the hyperslab in memory. */
	status = H5Dread(dataset, H5T_NATIVE_INT, memspace, dataspace, H5P_DEFAULT, data);

	if (status < 0)
	    printf("H5Dread failed\n");
    }

    //printf("thread_id = %d, offset[0]=%llu, offset[1]=%llu, count[0]=%llu, count[1] = %llu\n", thread_id, offset[0], offset[1], count[0], count[1]);

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
    int num_dsets_local;
    int nerrors = 0;
    int i, j, k;

    if (hand.num_threads > 0)
        num_dsets_local = hand.num_dsets / hand.num_threads;
    else
        num_dsets_local = hand.num_dsets; 


    data = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int)); /* output buffer */

    if (!data)
        printf("data buffer is NULL\n");

    for (k = 0; k < num_dsets_local; k++) {
        if (hand.num_threads > 0)
            sprintf(dset_name, "%s%d", DATASETNAME, (k * hand.num_threads + thread_id + 1));
        else
            sprintf(dset_name, "%s%d", DATASETNAME, k + 1);

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
        if (hand.check_data && !hand.random_data) {
	    /* Checking the correctness of the data if there are more than one H5Dread may not pass because the thread pool may still be 
	     * reading the data during the check.  The way thread pool is set up doesn't guarantee the data reading is finished during the check.
	     * Sleeping for a while may give the thread pool enough time to finish reading the data.
	     */
	    //sleep(1);

            if (hand.num_threads > 0)
                nerrors = check_data(data, (k * hand.num_threads + thread_id), 0);
            else
                nerrors = check_data(data, k, 0);
        }

        if (nerrors > 0)
            printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);
    }

    free(data);

    return NULL;
}

void* read_multiple_dsets_with_hdf5_multi(void* arg)
{
    int thread_id = ((args_t *)arg)->thread_id;
    hid_t file = ((args_t *)arg)->file_id;
    char dset_name[1024];
    void *data, *p;
    void **rbufs;
    hsize_t dimsm[2];    /* memory space dimensions */
    hid_t dataset, *dset_ids;
    hid_t *dataspace_ids, *memspace_ids;
    hid_t *mem_dtype_ids;
 
    hsize_t count[2];      /* size of the hyperslab in the file */
    hsize_t offset[2];     /* hyperslab offset in the file */
    hsize_t count_out[2];  /* size of the hyperslab in memory */
    hsize_t offset_out[2]; /* hyperslab offset in memory */
    int nerrors = 0;
    int i, j, k;

    data = (void *)malloc(hand.num_dsets * hand.dset_dim1 * hand.dset_dim2 * sizeof(int)); /* output buffer */
    
    if (!data)
        printf("data buffer is NULL\n");

    rbufs = (void **)malloc(sizeof(void *) * hand.num_dsets);

    for (i = 0; i < hand.num_dsets; i++)
        rbufs[i] = data + i * hand.dset_dim1 * hand.dset_dim2 * sizeof(int);

    dset_ids       = (hid_t *)malloc(hand.num_dsets * sizeof(hid_t));
    dataspace_ids  = (hid_t *)malloc(hand.num_dsets * sizeof(hid_t));
    memspace_ids   = (hid_t *)malloc(hand.num_dsets * sizeof(hid_t));
    mem_dtype_ids  = (hid_t *)malloc(hand.num_dsets * sizeof(hid_t));

    for (i = 0; i < hand.num_dsets; i++) {
        sprintf(dset_name, "%s%d", DATASETNAME, i + 1);

        /* Open the dataset */
        dset_ids[i] = H5Dopen2(file, dset_name, H5P_DEFAULT);

        mem_dtype_ids[i] = H5T_NATIVE_INT;

        dataspace_ids[i] = H5S_ALL;
        memspace_ids[i] = H5S_ALL;
    }

    /* Read data */
    H5Dread_multi(hand.num_dsets, dset_ids, mem_dtype_ids, memspace_ids, dataspace_ids, H5P_DEFAULT, rbufs);

    /* Close/release resources */
    for (i = 0; i < hand.num_dsets; i++)
        H5Dclose(dset_ids[i]);

    for (i = 0; i < hand.num_dsets; i++) {
	/* Data verification if enabled */
	if (hand.check_data && !hand.random_data)
	    nerrors = check_data(rbufs[i], i, 0);

	if (nerrors > 0)
	    printf("%d errors during data verification at line %d\n", nerrors, __LINE__);
    }

    if (data)
        free(data);
    if (rbufs)
        free(rbufs);
    if (dset_ids)
        free(dset_ids);
    if (dataspace_ids)
        free(dataspace_ids);
    if (memspace_ids)
        free(memspace_ids);
    if (mem_dtype_ids)
        free(mem_dtype_ids);

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
    char dset_name[1024];
    int  num_files_local = 0;
    int *data, *p;
    hsize_t dimsm[2];    /* memory space dimensions */
    hid_t *file, *dataset;
    hid_t *dataspace, *memspace;
    htri_t is_regular_hyperslab;
    hsize_t count[2];      /* size of the hyperslab in the file */
    hsize_t offset[2];     /* hyperslab offset in the file */
    hsize_t count_out[2];  /* size of the hyperslab in memory */
    hsize_t offset_out[2]; /* hyperslab offset in memory */
    int nerrors = 0;
    int i, j;

    if (hand.num_threads > 0)
        num_files_local = hand.num_files / hand.num_threads;
    else
        num_files_local = hand.num_files; 

    data      = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int)); /* output buffer */
    file      = (hid_t *)malloc(num_files_local * sizeof(hid_t));
    dataset   = (hid_t *)malloc(num_files_local * sizeof(hid_t));
    dataspace = (hid_t *)malloc(num_files_local * sizeof(hid_t));
    memspace  = (hid_t *)malloc(num_files_local * sizeof(hid_t));

    if (!data)
        printf("data_out is NULL\n");

    for (i = 0; i < num_files_local; i++) {
	/* When a single dataset in multiple files is created, different dataset names were used
	 * since Bypass VOL uses names to identify datasets.  The numbers in dataset names match
         * the number in file names.
	 */
        if (hand.num_threads > 0) {
            sprintf(file_name, "%s%d.h5", FILE_NAME, (i * hand.num_threads + thread_id + 1));
            sprintf(dset_name, "%s%d", DATASETNAME, (i * hand.num_threads + thread_id + 1));
        } else {
            sprintf(file_name, "%s%d.h5", FILE_NAME, i + 1);
	    sprintf(dset_name, "%s%d", DATASETNAME, i + 1);
        }

        memset(data, 0, (hand.dset_dim1 * hand.dset_dim2 * sizeof(int)));

        /* Open the file and the dataset */
        file[i]    = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

        dataset[i] = H5Dopen2(file[i], dset_name, H5P_DEFAULT);

        dataspace[i] = H5Dget_space(dataset[i]); /* dataspace handle */

        /* Define the memory dataspace */
        dimsm[0] = hand.dset_dim1;
        dimsm[1] = hand.dset_dim2;
        memspace[i] = H5Screate_simple(RANK, dimsm, NULL);

        /* Select the whole dataset as one hyperslab in the dataset and memory so that 
         * the log file contains the number of elements to be read in C */
        offset[0] = 0;
        offset[1] = 0;
        count[0]  = hand.dset_dim1;
        count[1]  = hand.dset_dim2;

        H5Sselect_hyperslab(dataspace[i], H5S_SELECT_SET, offset, NULL, count, NULL);
        H5Sselect_hyperslab(memspace[i], H5S_SELECT_SET, offset, NULL, count, NULL);
    }

    for (i = 0; i < num_files_local; i++) {
        /* Read data */
        H5Dread(dataset[i], H5T_NATIVE_INT, memspace[i], dataspace[i], H5P_DEFAULT, data);

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data) {
	    /* Checking the correctness of the data if there are more than one H5Dread may not pass because the thread pool may still be 
	     * reading the data during the check.  The way thread pool is set up doesn't guarantee the data reading is finished during the check.
	     * Sleeping for a while may give the thread pool enough time to finish reading the data.
	     */
	    //sleep(1);

            if (hand.num_threads > 0)
                nerrors = check_data(data, (i * hand.num_threads + thread_id), 0);
            else
                nerrors = check_data(data, i, 0);
        }

        if (nerrors > 0)
            printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);
    }

    for (i = 0; i < num_files_local; i++) {
        /* Close/release resources */
        H5Sclose(dataspace[i]);
        H5Sclose(memspace[i]);
        H5Dclose(dataset[i]);
        H5Fclose(file[i]);
    }

    if (data)
        free(data);
    if (file)
        free(file);
    if (dataset)
        free(dataset);
    if (dataspace)
        free(dataspace);
    if (memspace)
        free(memspace);

    return NULL;
}

void* read_multiple_files_with_hdf5_multi(void* arg)
{
    int thread_id = ((args_t *)arg)->thread_id;
    char file_name[1024];
    char dset_name[1024];
    int  num_files_local = 0;
    void *data, *p;
    void **rbufs;
    hsize_t dimsm[2];    /* memory space dimensions */
    hid_t *file, *dataset;
    hid_t *dataspace, *memspace;
    hid_t *mem_dtype_ids;
    htri_t is_regular_hyperslab;
    hsize_t count[2];      /* size of the hyperslab in the file */
    hsize_t offset[2];     /* hyperslab offset in the file */
    hsize_t count_out[2];  /* size of the hyperslab in memory */
    hsize_t offset_out[2]; /* hyperslab offset in memory */
    int nerrors = 0;
    int i, j;

    if (hand.num_threads > 0)
        num_files_local = hand.num_files / hand.num_threads;
    else
        num_files_local = hand.num_files; 

    data      = (void *)malloc(num_files_local * hand.dset_dim1 * hand.dset_dim2 * sizeof(int)); /* output buffer */

    if (!data)
        printf("data is NULL\n");

    rbufs = (void **)malloc(sizeof(void *) * num_files_local);

    for (i = 0; i < num_files_local; i++)
        rbufs[i] = data + i * hand.dset_dim1 * hand.dset_dim2 * sizeof(int);

    file      = (hid_t *)malloc(num_files_local * sizeof(hid_t));
    dataset   = (hid_t *)malloc(num_files_local * sizeof(hid_t));
    dataspace = (hid_t *)malloc(num_files_local * sizeof(hid_t));
    memspace  = (hid_t *)malloc(num_files_local * sizeof(hid_t));
    mem_dtype_ids  = (hid_t *)malloc(num_files_local * sizeof(hid_t));

    for (i = 0; i < num_files_local; i++) {
	/* When a single dataset in multiple files is created, different dataset names were used
	 * since Bypass VOL uses names to identify datasets.  The numbers in dataset names match
         * the number in file names.
	 */
        if (hand.num_threads > 0) {
            sprintf(file_name, "%s%d.h5", FILE_NAME, (i * hand.num_threads + thread_id + 1));
            sprintf(dset_name, "%s%d", DATASETNAME, (i * hand.num_threads + thread_id + 1));
        } else {
            sprintf(file_name, "%s%d.h5", FILE_NAME, i + 1);
	    sprintf(dset_name, "%s%d", DATASETNAME, i + 1);
        }

        /* Open the file and the dataset */
        file[i]    = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

        dataset[i] = H5Dopen2(file[i], dset_name, H5P_DEFAULT);

        mem_dtype_ids[i] = H5T_NATIVE_INT;

        dataspace[i] = H5Dget_space(dataset[i]); /* dataspace handle */

        /* Define the memory dataspace */
        dimsm[0] = hand.dset_dim1;
        dimsm[1] = hand.dset_dim2;
        memspace[i] = H5Screate_simple(RANK, dimsm, NULL);

        /* Select the whole dataset as one hyperslab in the dataset and memory so that 
         * the log file contains the number of elements to be read in C */
        offset[0] = 0;
        offset[1] = 0;
        count[0]  = hand.dset_dim1;
        count[1]  = hand.dset_dim2;

        H5Sselect_hyperslab(dataspace[i], H5S_SELECT_SET, offset, NULL, count, NULL);
        H5Sselect_hyperslab(memspace[i], H5S_SELECT_SET, offset, NULL, count, NULL);
    }

    /* Read multiple datasets using H5Dread_multi */
    H5Dread_multi(num_files_local, dataset, mem_dtype_ids, memspace, dataspace, H5P_DEFAULT, rbufs);

    /* Checking the correctness of the data if there are more than one H5Dread may not pass because the thread pool may still be 
     * reading the data during the check.  The way thread pool is set up doesn't guarantee the data reading is finished during the check.
     * Sleeping for a while may give the thread pool enough time to finish reading the data.
     */
    if (hand.check_data && !hand.random_data)
	//sleep(1);

    for (i = 0; i < num_files_local; i++) {
	if (hand.check_data && !hand.random_data)
	    nerrors = check_data(rbufs[i], i, 0);

	if (nerrors > 0)
	    printf("%d errors during data verification at line %d\n", nerrors, __LINE__);
    }

    for (i = 0; i < num_files_local; i++) {
        /* Close/release resources */
        H5Sclose(dataspace[i]);
        H5Sclose(memspace[i]);
        H5Dclose(dataset[i]);
        H5Fclose(file[i]);
    }

    if (data)
        free(data);
    if (file)
        free(file);
    if (dataset)
        free(dataset);
    if (dataspace)
        free(dataspace);
    if (memspace)
        free(memspace);
    if (mem_dtype_ids)
        free(mem_dtype_ids);

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
    hid_t       file, fapl;
    hid_t       dataset; /* handles */
    int         mdc_nelmts;
    size_t      rdcc_nelmts;
    size_t      rdcc_nbytes;
    double      rdcc_w0;
    int         i, j, k;
    pthread_t threads[hand.num_threads];
    int thread_ids[hand.num_threads];
    //args_t info[hand.num_threads];
    int *p, *data_out = NULL;
    char file_name[1024];
    int nerrors = 0;

    struct timeval begin, end;

    /* Make sure the info.log file is deleted before running each round */
    if (access("info.log", F_OK) == 0 && remove("info.log") != 0) {
	printf("unable to delete existing info.log.  Must delete it by hand before running this test.\n");
	exit(1);
    }

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

    /* Open the file and the dataset */
    sprintf(file_name, "%s.h5", FILE_NAME);

    file    = H5Fopen(file_name, H5F_ACC_RDONLY, fapl);
    dataset = H5Dopen2(file, DATASETNAME, H5P_DEFAULT);

    gettimeofday(&begin, 0);

    if (hand.num_threads == 0) {
        args_t info;

	info.thread_id = 0;
	info.dset_id = dataset;
	info.data = NULL;  /* Memory buffer is allocated in the function read_partial_dset_with_hdf5 */

	read_partial_dset_with_hdf5(&info);
    } else if (hand.num_threads > 0) {
        args_t info[hand.num_threads];
    
        data_out = (int *)calloc(hand.dset_dim1 * hand.dset_dim2, sizeof(int)); /* output buffer */

	if (!data_out) {
	    printf("data_out is NULL\n");
	    goto error;
	}

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

	/* Data verification if enabled.  Do not enable this option (-k) for performance study */
	if (hand.check_data && !hand.random_data)
	    nerrors = check_data(data_out, 0, 0);

	if (nerrors > 0)
	    printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);
    }

    /* Close/release resources */
    H5Dclose(dataset);
    H5Fclose(file);
    H5Pclose(fapl);

    /* Stop time after file closing to match the design of the Bypass VOL */
    gettimeofday(&end, 0);

    save_statistics(begin, end);

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

    if (hand.num_threads == 0) {
        args_t info;

	info.thread_id = 0;
	info.file_id = file;
	info.data = NULL;  /* Memory buffer is allocated in the function read_multiple_files_with_hdf5 */

        /* Use H5Dread_multi to read from multiple datasets if enabled */
        if (hand.multi_dsets)
            read_multiple_dsets_with_hdf5_multi(&info);
        else
            read_multiple_dsets_with_hdf5(&info);

    } else {
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

    if (hand.num_threads == 0) {
        args_t info;

	info.thread_id = 0;
	info.data = NULL;  /* Memory buffer is allocated in the function read_multiple_files_with_hdf5 */

        /* Use H5Dread_multi to read from multiple datasets in different files if enabled */
        if (hand.multi_dsets)
            read_multiple_files_with_hdf5_multi(&info);
        else
            read_multiple_files_with_hdf5(&info);
    } else {
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
