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

#define FILE_NAME   "mt_file"
#define DATASETNAME "dset"
#define RANK        2

typedef struct {
    int         fp;
    int         thread_id;
    file_info_t *file_info;
    //int         finfo_entry_num;
    int         *data;
} c_args_t;

/*------------------------------------------------------------
 * Read and parse the info.log file for the preparation of  
 * reading the data in C only
 *------------------------------------------------------------
 */
int read_info_log_file(int *finfo_entry_num)
{
    FILE *fp = NULL;
    int display;
    int i, counter = 0;
    size_t len;
    char *buf = NULL;
    char *token = NULL;
    const char delimiter[] = " \n\0";

    /* Make sure the info.log file exists */
    if (access("info.log", F_OK) != 0) {
        printf("info.log doesn't exist.  You must run this test with Bypass VOL to generate it.\n"); 
        exit(1);
    }

    /* Open the info.log file and count the number of characters */
    fp = fopen("info.log", "r");

    while (1) {
        display = fgetc(fp);
        counter++;
        if (feof(fp))
            break;    
    }

    fclose(fp);

    /* Allocate enough buffer and read the file content into the buffer */
    buf = malloc(counter + 1);

    fp = fopen("info.log", "r");

    fread(buf, sizeof(char), counter, fp);

    buf[counter] = '\0'; 


    /* Begin to parse the buffer containing the contents of the log file */
    counter = 0;

    /* File name */
    token = strtok(buf, delimiter);
    //printf("%s ", token);
    strcpy(file_info[counter].file_name, token);

    /* Dataset name (unused) */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    strcpy(file_info[counter].dset_name, token);

    /* Location of dataset in the file */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    file_info[counter].dset_offset = atoll(token);

    /* Offset of data in the HDF5 file to be read into the memory */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    file_info[counter].offset_f = atoll(token);

    /* Number of elements to be read */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    file_info[counter].nelmts = atoll(token);

    /* Offset of the data in the memory to be read */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    file_info[counter].offset_m = atoll(token);

    while (1) {
        /* File name */
        token = strtok(NULL, delimiter);
        if (!token)
            break;
        //printf("%s ", token);

        counter++;

        if (counter == *finfo_entry_num) {
            *finfo_entry_num *= 2;
            file_info = (file_info_t *)realloc(file_info, *finfo_entry_num * sizeof(file_info_t));
        }

        strcpy(file_info[counter].file_name, token);

        /* Dataset name (unused) */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        strcpy(file_info[counter].dset_name, token);

        /* Location of dataset in the file */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        file_info[counter].dset_offset = atoll(token);

        /* Offset of data in the HDF5 file to be read into the memory */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        file_info[counter].offset_f = atoll(token);

        /* Number of elements to be read */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        file_info[counter].nelmts = atoll(token);

        /* Offset of the data in the memory to be read */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        file_info[counter].offset_m = atoll(token);
    }

    fclose(fp);
    free(buf);

    /* printf("3. Print out file_info:\n");
    if (hand.num_files == 1 && hand.num_dsets == 1) {
        for (i = 0; i < hand.num_threads; i++)
            printf("%s %s %d %d %d %d\n", file_info[i].file_name, file_info[i].dset_name, file_info[i].dset_offset, file_info[i].offset_f, file_info[i].nelmts, file_info[i].offset_m);
    } else if (hand.num_files == 1 && hand.num_dsets > 1) {
        for (i = 0; i < hand.num_dsets; i++)
            printf("%s %s %d %d %d %d\n", file_info[i].file_name, file_info[i].dset_name, file_info[i].dset_offset, file_info[i].offset_f, file_info[i].nelmts, file_info[i].offset_m);
    } else {
        for (i = 0; i < hand.num_files; i++)
            printf("%s %s %d %d %d %d\n", file_info[i].file_name, file_info[i].dset_name, file_info[i].dset_offset, file_info[i].offset_f, file_info[i].nelmts, file_info[i].offset_m);
    }
    printf("\n");*/

    return 0;

error:
    return -1;
}

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading partial dataset in C in the case of a single
 * dataset in a single file
 *------------------------------------------------------------
 */
void* read_partial_dset_in_c(void* arg)
{
    int fp = ((c_args_t *)arg)->fp;
    int thread_id = ((c_args_t *)arg)->thread_id;
    int *data = ((c_args_t *)arg)->data;
    file_info_t *file_info = ((c_args_t *)arg)->file_info;
    int *p;

    /* printf("thread_id=%d, data = %p, file_name=%s, dset_name=%s, dset_offset=%lld, offset_f=%lld, nelmts=%lld, offset_m=%lld\n", thread_id, data, file_info->file_name, file_info->dset_name, file_info->dset_offset, file_info->offset_f, file_info->nelmts, file_info->offset_m); */

    read_big_data(fp, data + file_info->offset_m, sizeof(int) * file_info->nelmts, (file_info->dset_offset + file_info->offset_f * sizeof(int)));

    return NULL;
}

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading multiple datasets in C in the case of multiple
 * datasets in a single file
 *------------------------------------------------------------
 */
void* read_multiple_dsets_in_c(void* arg)
{
    int fp = ((c_args_t *)arg)->fp;
    int thread_id = ((c_args_t *)arg)->thread_id;
    int *data;
    file_info_t *local_file_info;
    int *p;
    int nerrors = 0;
    int i, j, k;

    data = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int));

    for (k = 0; k < hand.num_dsets / hand.num_threads; k++) {
        local_file_info = &(file_info[k * hand.num_threads + thread_id]);

        //printf("k=%d, thread_id=%d, dset_name=%s, dset_offset=%lld, offset_f=%lld, nelmts=%lld, offset_m=%lld\n", k, thread_id, local_file_info->dset_name, local_file_info->dset_offset, local_file_info->offset_f, local_file_info->nelmts, local_file_info->offset_m);

        read_big_data(fp, data + local_file_info->offset_m, sizeof(int) * local_file_info->nelmts, (local_file_info->dset_offset + local_file_info->offset_f * sizeof(int)));

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
            nerrors = check_data(data, (k * hand.num_threads + thread_id));

        /* This data verification is only for debugging purpose.  The order of the dataset being read in C
         * may not be the same as with HDF5.  So it may report error here. */
        if (nerrors > 0)
            printf("%d errors during data verification at line %d.  It could be caused by the different order of datasets being read.\n", nerrors, __LINE__);
    }

    free(data);

    return NULL;
}

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading multiple files in C in the case of a single 
 * dataset in multiple files
 *------------------------------------------------------------
 */
void* read_multiple_files_in_c(void* arg)
{
    int fp;
    int thread_id = ((c_args_t *)arg)->thread_id;
    int *p, *data = NULL;
    file_info_t *local_file_info;
    int nerrors = 0;
    int i, j, k;

    data = (int *)malloc(sizeof(int) * hand.dset_dim1 * hand.dset_dim2);

    for (k = 0; k < hand.num_files / hand.num_threads; k++) {
        local_file_info = &(file_info[k * hand.num_threads + thread_id]);

        //printf("thread_id=%d, file_name=%s, dset_name=%s, dset_offset=%d, offset_f=%d, nelmts=%d, offset_m=%d\n", thread_id, local_file_info->file_name, local_file_info->dset_name, local_file_info->dset_offset, local_file_info->offset_f, local_file_info->nelmts, local_file_info->offset_m);

        fp = open(local_file_info->file_name, O_RDONLY);

        read_big_data(fp, data + local_file_info->offset_m, sizeof(int) * local_file_info->nelmts, (local_file_info->dset_offset + local_file_info->offset_f * sizeof(int)));

        close(fp);

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
            nerrors = check_data(data, (k * hand.num_threads + thread_id));

        /* This data verification is only for debugging purpose.  The order of the data in the files being read in C
         * may not be the same as with HDF5.  So it may report error here. */
        if (nerrors > 0)
            printf("%d errors during data verification at line %d in the function %s.  It could be caused by the different order of datasets being read.\n", nerrors, __LINE__, __func__);

    }

    free(data);
  
    return NULL;
}

/*------------------------------------------------------------
 * Start to test the case of a single dataset in a single file
 * with multi-thread with HDF5 and C
 *------------------------------------------------------------
 */
int
launch_single_file_single_dset_read(void)
{
    int         fp;
    int         i, j, k;
    pthread_t threads[hand.num_threads];
    int thread_ids[hand.num_threads];
    c_args_t c_info[hand.num_threads];
    int *p, *data_out = NULL;
    char file_name[1024];
    int         finfo_entry_num;
    int nerrors = 0;

    struct timeval begin, end;

    data_out = (int *)calloc(hand.dset_dim1 * hand.dset_dim2, sizeof(int)); /* output buffer */

    if (!data_out) {
	printf("data_out is NULL\n");
	goto error;
    }

    finfo_entry_num = hand.num_threads;

    if (read_info_log_file(&finfo_entry_num) < 0) {
	printf("read_info_log_file failed\n");
	exit(1);
    }

    /* Start to read the data using C functions without HDF5 involved */
    memset(data_out, 0, (sizeof(int) * hand.dset_dim1 * hand.dset_dim2));

    /* Open the file */
    fp = open(file_info->file_name, O_RDONLY);

    gettimeofday(&begin, 0);

    /* Create threads to read the data */
    for (i = 0; i < hand.num_threads; i++) {
	c_info[i].thread_id = i;
	c_info[i].file_info = &file_info[i];
	c_info[i].data = data_out;
	c_info[i].fp = fp;
	pthread_create(&threads[i], NULL, read_partial_dset_in_c, &c_info[i]);
    }

    /* Wait for threads to complete */
    for (i = 0; i < hand.num_threads; i++) {
	pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, 0);

    save_statistics(begin, end);

    close(fp);

    /* Data verification if enabled */
    if (hand.check_data && !hand.random_data)
	    check_data(data_out, 0);

	if (nerrors > 0)
	    printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);

    free(data_out);

    return 0;

error:
    return -1;
}

/*------------------------------------------------------------
 * Start to test the case of multiple datasets in a single file
 * with multi-thread with HDF5 and C
 *------------------------------------------------------------
 */
int
launch_single_file_multiple_dset_read()
{
    int         i, j, k;
    pthread_t threads[hand.num_threads];
    int thread_ids[hand.num_threads];
    c_args_t c_info[hand.num_threads];
    int *p, *data_out = NULL;
    char file_name[1024];
    int fp;
    int         finfo_entry_num;
    struct timeval begin, end;

    if (read_info_log_file(&finfo_entry_num) < 0) {
	printf("read_info_log_file failed\n");
	exit(1);
    }

    /* Start to read the data using C functions without HDF5 involved */
    fp = open(file_info->file_name, O_RDONLY);

    gettimeofday(&begin, 0);

    /* Create threads to read the data */
    for (i = 0; i < hand.num_threads; i++) {
	c_info[i].thread_id = i;
	c_info[i].file_info = &file_info[i];
	c_info[i].fp = fp;
	pthread_create(&threads[i], NULL, read_multiple_dsets_in_c, &c_info[i]);
    }

    /* Wait for threads to complete */
    for (i = 0; i < hand.num_threads; i++) {
	pthread_join(threads[i], NULL);
    }

    gettimeofday(&end, 0);

    save_statistics(begin, end);

    close(fp);

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
    c_args_t c_info[hand.num_threads];
    int i, j;
    int *data_out = NULL;
    int         finfo_entry_num;
    struct timeval begin, end;

    if (read_info_log_file(&finfo_entry_num) < 0) {
	printf("read_info_log_file failed\n");
	exit(1);
    }

    gettimeofday(&begin, 0);

    /* Create threads to read the multiple files in C only */
    for (i = 0; i < hand.num_threads; i++) {
	c_info[i].thread_id = i;
	pthread_create(&threads[i], NULL, read_multiple_files_in_c, &c_info[i]);
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

    if (hand.num_files == 1 && hand.num_dsets == 1) {
        file_info = (file_info_t *)calloc(sizeof(file_info_t), hand.num_threads);

        launch_single_file_single_dset_read();
    } else if (hand.num_files == 1 && hand.num_dsets > 1) {
        file_info = (file_info_t *)calloc(sizeof(file_info_t), hand.num_dsets);

        launch_single_file_multiple_dset_read();
    } else if (hand.num_files > 1) {
        file_info = (file_info_t *)calloc(sizeof(file_info_t), hand.num_files);

        launch_multiple_file_read(hand.num_files);
    }

    /* Print out the performance statistics */
    report_statistics();

    free(file_info);

    return 0;
}
