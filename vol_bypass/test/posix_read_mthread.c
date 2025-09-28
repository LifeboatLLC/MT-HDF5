#include "common.h"
#include "common.c"

#define FILE_NAME   "mt_file"
#define DATASETNAME "dset"
#define RANK        2

typedef struct {
    int         fp;
    int         thread_id;
    file_info_t *file_info;
    int         file_info_entry_num;
    int         *data;
} c_args_t;

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading partial dataset in C in the case of a single
 * dataset in a single file
 *------------------------------------------------------------
 */
void* read_partial_dset_with_no_child_thread(void* arg)
{
    int fp = ((c_args_t *)arg)->fp;
    int thread_id = ((c_args_t *)arg)->thread_id;
    int *data = ((c_args_t *)arg)->data;
    int *p, i, j;
    int num_entries = ((c_args_t *)arg)->file_info_entry_num;

    for (i = 0; i < file_info_nsections; i++) {
	for (j = 0; j < file_info_count[i]; j++) {
	    /* printf("thread_id=%d, fp = %d, file_info_nsections = %d, data = %p, file_name=%s, dset_name=%s, dset_offset=%lld, offset_f=%lld, nelmts=%lld, offset_m=%lld\n", 
		thread_id, fp, file_info_nsections, data, file_info_array[i][j].file_name, file_info_array[i][j].dset_name, file_info_array[i][j].dset_offset, 
		file_info_array[i][j].offset_f, file_info_array[i][j].nelmts, file_info_array[i][j].offset_m); */

	    read_big_data(fp, data + file_info_array[i][j].offset_m, sizeof(int) * file_info_array[i][j].nelmts, 
                (file_info_array[i][j].dset_offset + file_info_array[i][j].offset_f * sizeof(int)));
	}

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
	    check_data(data, 0, i);
    }

    return NULL;
}

void* read_partial_dset_with_multiple_threads(void* arg)
{
    int fp = ((c_args_t *)arg)->fp;
    int thread_id = ((c_args_t *)arg)->thread_id;
    int *data = ((c_args_t *)arg)->data;
    file_info_t *file_info = ((c_args_t *)arg)->file_info;
    int *p, i, j;
    int num_entries = ((c_args_t *)arg)->file_info_entry_num;

    for (i = 0; i < num_entries; i++) {
	/* printf("thread_id=%d, fp = %d, file_info_nsections = %d, data = %p, file_name=%s, dset_name=%s, dset_offset=%lld, offset_f=%lld, nelmts=%lld, offset_m=%lld\n", 
	    thread_id, fp, file_info_nsections, data, file_info[i].file_name, file_info[i].dset_name, file_info[i].dset_offset, 
	    file_info[i].offset_f, file_info[i].nelmts, file_info[i].offset_m); */

        read_big_data(fp, data + file_info[i].offset_m, sizeof(int) * file_info[i].nelmts, (file_info[i].dset_offset + file_info[i].offset_f * sizeof(int)));
    }

    return NULL;
}

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading multiple datasets in C in the case of multiple
 * datasets in a single file
 *------------------------------------------------------------
 */
void* read_multiple_dsets_with_multiple_threads(void* arg)
{
    int fp = ((c_args_t *)arg)->fp;
    int thread_id = ((c_args_t *)arg)->thread_id;
    int *data;
    file_info_t *local_file_info;
    int nerrors = 0;
    int k;

    data = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int));

    for (k = 0; k < hand.num_dsets / hand.num_threads; k++) {
        local_file_info = file_info_array[thread_id * (file_info_nsections / hand.num_threads) + k];

        //printf("k=%d, thread_id=%d, dset_name=%s, dset_offset=%lld, offset_f=%lld, nelmts=%lld, offset_m=%lld\n", k, thread_id, local_file_info->dset_name, local_file_info->dset_offset, local_file_info->offset_f, local_file_info->nelmts, local_file_info->offset_m);

        read_big_data(fp, data + local_file_info->offset_m, sizeof(int) * local_file_info->nelmts,
            (local_file_info->dset_offset + local_file_info->offset_f * sizeof(int)));

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
            nerrors = check_data(data, (thread_id * (file_info_nsections / hand.num_threads) + k), 0);

        /* This data verification is only for debugging purpose */
        if (nerrors > 0)
            printf("%d errors during data verification at line %d.\n", nerrors, __LINE__);
    }

    free(data);

    return NULL;
}

/*------------------------------------------------------------
 * Function executed by main process (no child thread)
 *
 * Reading multiple datasets in C in the case of multiple
 * datasets in a single file
 *------------------------------------------------------------
 */
void* read_multiple_dsets_with_no_child_thread(void* arg)
{
    int fp = ((c_args_t *)arg)->fp;
    file_info_t *local_file_info;
    int *data;
    int nerrors = 0;
    int k;

    data = (int *)malloc(hand.dset_dim1 * hand.dset_dim2 * sizeof(int));

    for (k = 0; k < hand.num_dsets; k++) {
        local_file_info = file_info_array[k];

        //printf("k=%d, thread_id=%d, dset_name=%s, dset_offset=%lld, offset_f=%lld, nelmts=%lld, offset_m=%lld\n", k, thread_id, local_file_info->dset_name, local_file_info->dset_offset, local_file_info->offset_f, local_file_info->nelmts, local_file_info->offset_m);

        read_big_data(fp, data + local_file_info->offset_m, sizeof(int) * local_file_info->nelmts,
            (local_file_info->dset_offset + local_file_info->offset_f * sizeof(int)));

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
            nerrors = check_data(data, k, 0);

        /* This data verification is only for debugging purpose. */
        if (nerrors > 0)
            printf("%d errors during data verification at line %d.\n", nerrors, __LINE__);
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
void* read_multiple_files_with_multiple_threads(void* arg)
{
    int fp;
    int thread_id = ((c_args_t *)arg)->thread_id;
    int *p, *data = NULL;
    file_info_t *local_file_info;
    int nerrors = 0;
    int i, j, k;

    data = (int *)malloc(sizeof(int) * hand.dset_dim1 * hand.dset_dim2);

    for (k = 0; k < hand.num_files / hand.num_threads; k++) {
        local_file_info = file_info_array[thread_id * (file_info_nsections / hand.num_threads) + k];

        //printf("thread_id=%d, file_name=%s, dset_name=%s, dset_offset=%d, offset_f=%d, nelmts=%d, offset_m=%d\n", thread_id, local_file_info->file_name, local_file_info->dset_name, local_file_info->dset_offset, local_file_info->offset_f, local_file_info->nelmts, local_file_info->offset_m);

        fp = open(local_file_info->file_name, O_RDONLY);

        read_big_data(fp, data + local_file_info->offset_m, sizeof(int) * local_file_info->nelmts,
            (local_file_info->dset_offset + local_file_info->offset_f * sizeof(int)));

        close(fp);

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
            nerrors = check_data(data, (thread_id * (file_info_nsections / hand.num_threads) + k), 0);

        /* This data verification is only for debugging purpose */
        if (nerrors > 0)
            printf("%d errors during data verification at line %d in the function %s.\n", nerrors, __LINE__, __func__);

    }

    free(data);

    return NULL;
}

/*------------------------------------------------------------
 * Function executed by the main process (no child thread)
 *
 * Reading multiple files in C in the case of a single
 * dataset in multiple files
 *------------------------------------------------------------
 */
void* read_multiple_files_with_no_child_thread(void* arg)
{
    int fp;
    int *data = NULL;
    file_info_t *local_file_info;
    int nerrors = 0;
    int k;

    data = (int *)malloc(sizeof(int) * hand.dset_dim1 * hand.dset_dim2);

    for (k = 0; k < hand.num_files; k++) {
        local_file_info = file_info_array[k];

        //printf("thread_id=%d, file_name=%s, dset_name=%s, dset_offset=%d, offset_f=%d, nelmts=%d, offset_m=%d\n", thread_id, local_file_info->file_name, local_file_info->dset_name, local_file_info->dset_offset, local_file_info->offset_f, local_file_info->nelmts, local_file_info->offset_m);

        fp = open(local_file_info->file_name, O_RDONLY);

        read_big_data(fp, data + local_file_info->offset_m, sizeof(int) * local_file_info->nelmts,
            (local_file_info->dset_offset + local_file_info->offset_f * sizeof(int)));

        close(fp);

        /* Data verification if enabled */
        if (hand.check_data && !hand.random_data)
            nerrors = check_data(data, k, 0);

        /* This data verification is only for debugging purpose */
        if (nerrors > 0)
            printf("%d errors during data verification at line %d in the function %s.\n", nerrors, __LINE__, __func__);

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
    int *p, *data_out = NULL;
    char file_name[1024];
    int finfo_entry_num;
    int file_info_num_in_group  = 0;
    int file_info_num_extra     = 0;
    int file_info_num_allocated = 0;
    int nerrors = 0;

    struct timeval begin, end;

    /* Break down the dataset into NUM_DATA_SECTIONS sections if the dataset is too big */
    if (hand.num_data_sections > 1) {
        data_in_section = true;
        //hand.check_data = false;   /* Skipping data verification in this case */

        data_out = (int *)calloc((hand.dset_dim1 / hand.num_data_sections) * hand.dset_dim2, sizeof(int)); /* output buffer */
    } else {
        data_out = (int *)calloc(hand.dset_dim1 * hand.dset_dim2, sizeof(int)); /* output buffer */
    }

    if (!data_out) {
	printf("data_out is NULL\n");
	goto error;
    }

    /* Start to read the data using C functions without HDF5 involved */

    /* Open the file */
    fp = open(file_info_array[0][0].file_name, O_RDONLY);

    gettimeofday(&begin, 0);

    if (hand.num_threads == 0) {
        c_args_t c_info;

	c_info.thread_id = 0;
        c_info.data = data_out;
        c_info.fp = fp;

	read_partial_dset_with_no_child_thread(&c_info);
    } else if (hand.num_threads > 0) {
        c_args_t c_info[hand.num_threads];

        /* Handle each section seperately. For each section, create multiple threads to
         * read the data then close them. */
        for (j = 0; j < file_info_nsections; j++) {
	    /* The number of data pieces that each thread takes at least */
	    file_info_num_in_group = file_info_count[j] / hand.num_threads;

	    /* The leftover number of data pieces when the number can't be evenly divided */
	    file_info_num_extra    = file_info_count[j] % hand.num_threads;

            /* Reset */
            file_info_num_allocated = 0;

	    /* Create threads to read the data */
	    for (i = 0; i < hand.num_threads; i++) {
		c_info[i].thread_id = i;

		/* The starting index of FILE_INFO_ARRAY for this thread.  
                 * Evenly divide the FILE_INFO_ARRAY among the threads */
		c_info[i].file_info = &(file_info_array[j][file_info_num_allocated]);

		/* Assign a number of data pieces to each thread.  For example, there are
		 * 10 pieces of data and 4 threads, the numbers of data pieces for each thread
		 * are 3, 3, 2, 2.
		 */
		if (file_info_num_extra) {
		    c_info[i].file_info_entry_num = file_info_num_in_group + 1;
		    file_info_num_extra--;
		    file_info_num_allocated += (file_info_num_in_group + 1);
		} else {
		    c_info[i].file_info_entry_num = file_info_num_in_group;
		    file_info_num_allocated += file_info_num_in_group;
		}

		c_info[i].data = data_out;
		c_info[i].fp = fp;

		//printf("c_info[%d].file_info_entry_num = %d, file_info_num_allocated = %d\n", i, c_info[i].file_info_entry_num, file_info_num_allocated);

		pthread_create(&threads[i], NULL, read_partial_dset_with_multiple_threads, &c_info[i]);
            }

	    /* Wait for threads to complete */
	    for (i = 0; i < hand.num_threads; i++)
		pthread_join(threads[i], NULL);

            /* Data verification if enabled */
            if (hand.check_data && !hand.random_data)
                check_data(data_out, 0, j);
        }
    }

    close(fp);

    /* Stop time after file closing to match the design of the Bypass VOL */
    gettimeofday(&end, 0);

    save_statistics(begin, end);

    if (nerrors > 0)
	printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);

    if (data_out)
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
    char file_name[1024];
    int fp;
    int         finfo_entry_num;
    struct timeval begin, end;

    /* Start to read the data using C functions without HDF5 involved */
    fp = open(file_info_array[0][0].file_name, O_RDONLY);

    gettimeofday(&begin, 0);

    if (hand.num_threads == 0) {
        c_args_t c_info;
        c_info.fp = fp;

	read_multiple_dsets_with_no_child_thread(&c_info);
    } else if (hand.num_threads > 0) {
	/* Create threads to read the data */
	for (i = 0; i < hand.num_threads; i++) {
	    c_info[i].thread_id = i;
	    c_info[i].fp = fp;

	    pthread_create(&threads[i], NULL, read_multiple_dsets_with_multiple_threads, &c_info[i]);
	}

	/* Wait for threads to complete */
	for (i = 0; i < hand.num_threads; i++) {
	    pthread_join(threads[i], NULL);
	}
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
    c_args_t c_info[hand.num_threads];
    int i;
    struct timeval begin, end;

    gettimeofday(&begin, 0);

    if (hand.num_threads == 0) {
	read_multiple_files_with_no_child_thread(NULL);
    } else if (hand.num_threads > 0) {
	/* Create threads to read the multiple files in C only */
	for (i = 0; i < hand.num_threads; i++) {
	    c_info[i].thread_id = i;
	    pthread_create(&threads[i], NULL, read_multiple_files_with_multiple_threads, &c_info[i]);
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

    read_info_log_file_array();

    if (hand.num_files == 1 && hand.num_dsets == 1) {
        launch_single_file_single_dset_read();
    } else if (hand.num_files == 1 && hand.num_dsets > 1) {
        launch_single_file_multiple_dset_read();
    } else if (hand.num_files > 1) {
        launch_multiple_file_read(hand.num_files);
    }

    /* Print out the performance statistics */
    report_statistics();

    free_file_info_array();

    return 0;
}
