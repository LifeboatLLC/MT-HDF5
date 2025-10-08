#include "common.h"
#include "common.c"

#define FILE_NAME   "mt_file"
#define DATASETNAME "dset"

pthread_mutex_t mutex_queue;
pthread_cond_t  cond_value, cond_value_finish;

typedef struct {
    int         fp;
    int         thread_id;
    int         file_info_entry_num;
    int         step;
    int         *data;
} c_args_t;

typedef struct {
    int  info_pointer;
    int  task_count;
    bool finished;
} tpool_var_t;

tpool_var_t *tpool_vars;
int  section_index = 0;
bool all_section_done = false;

/*------------------------------------------------------------
 * Function executed by each thread: 
 *
 * Reading partial dataset in C in the case of a single
 * dataset in a single file
 *------------------------------------------------------------
 */
void submit_task(int step, int index)
{
    pthread_mutex_lock(&mutex_queue);

    tpool_vars[index].task_count += step;
    section_index = index;

    pthread_mutex_unlock(&mutex_queue);

    pthread_cond_broadcast(&cond_value);
}

void* read_in_c(void* arg)
{
    //int fp = ((c_args_t *)arg)->fp;
    int thread_id = ((c_args_t *)arg)->thread_id;
    int *data = ((c_args_t *)arg)->data;
    //file_info_t *file_info = ((c_args_t *)arg)->file_info;
    int step = ((c_args_t *)arg)->step;
    int num_entries = ((c_args_t *)arg)->file_info_entry_num;
    file_info_t *file_info_local;
    int local_count = 0;
    int nfinished = 0;
    int *p, i, j;

    /* To do: Avoid memory allocation here */
    file_info_local = (file_info_t *)malloc(step * sizeof(file_info_t));

    /* ALL_SECTION_DONE is a flag to indicate all sections are finished. Each section corresponds to a H5Dread */
    while(!all_section_done) {
        pthread_mutex_lock(&mutex_queue);

        /* This thread waits for more tasks */
        while(tpool_vars[section_index].task_count == 0 && !tpool_vars[section_index].finished)
            pthread_cond_wait(&cond_value, &mutex_queue);

        /* TASK_COUNT can be smaller or larger than STEP */
        local_count = MIN(tpool_vars[section_index].task_count, step);
//fprintf(stderr, "thread_id %d: after assignment, local_count = %d, section_index = %d\n", thread_id, local_count, section_index);

        /* Save the location of the data to be read */
	for (i = 0; i < local_count; i++) {
	    file_info_local[i].fp = file_info_array[section_index][tpool_vars[section_index].info_pointer].fp; 
	    file_info_local[i].dset_offset = file_info_array[section_index][tpool_vars[section_index].info_pointer].dset_offset; 
	    file_info_local[i].offset_f = file_info_array[section_index][tpool_vars[section_index].info_pointer].offset_f;
	    file_info_local[i].offset_m = file_info_array[section_index][tpool_vars[section_index].info_pointer].offset_m;
	    file_info_local[i].nelmts = file_info_array[section_index][tpool_vars[section_index].info_pointer].nelmts; 

	    tpool_vars[section_index].info_pointer++;
	    tpool_vars[section_index].task_count--;
	}

        /* Check if this section is finished */
	if (tpool_vars[section_index].info_pointer == file_info_count[section_index] && tpool_vars[section_index].task_count == 0) {
	    tpool_vars[section_index].finished = true;

            /* Check if all sections are finished */
            nfinished = 0;
            for (i = 0; i < file_info_nsections; i++) {
                if (tpool_vars[i].finished)
                    nfinished++;
            }
            if (nfinished == file_info_nsections)
                all_section_done = true;

            /* Notify the main process */
            pthread_cond_broadcast(&cond_value_finish);
        }

	pthread_mutex_unlock(&mutex_queue);

//fprintf(stderr, "	before reading data. local_count = %d, finished[%d] = %d\n", local_count, section_index, tpool_vars[section_index].finished);

        for (i = 0; i < local_count; i++) {
            /* fprintf(stderr, "thread_id=%d, fp = %d, file_info_nsections = %d, section_index = %d, data = %p, dset_offset=%lld, offset_f=%lld, nelmts=%lld, offset_m=%lld\n", 
	    thread_id, fp, file_info_nsections, section_index, data, file_info_local[i].dset_offset, file_info_local[i].offset_f, file_info_local[i].nelmts, file_info_local[i].offset_m); */

            read_big_data(file_info_local[i].fp, data + file_info_local[i].offset_m, sizeof(int) * file_info_local[i].nelmts, (file_info_local[i].dset_offset + file_info_local[i].offset_f * sizeof(int)));
        }

//fprintf(stderr, "thread_id %d: after reading data\n", thread_id);
    }

    free(file_info_local);

    return NULL;
}

/*------------------------------------------------------------
 * Start to test the case of a single dataset in a single file
 * with multi-thread with HDF5 and C
 *------------------------------------------------------------
 */
int
launch_read(bool single_file_single_dset)
{
    int         fp;
    int         i, j, k;
    pthread_t threads[hand.num_threads];
    int thread_ids[hand.num_threads];
    c_args_t c_info[hand.num_threads];
    int *p, *data_out = NULL;
    char file_name[1024];
    int finfo_entry_num;
    int finfo_rounds = 0, finfo_leftover = 0;
    int nerrors = 0;
    struct timeval begin, end;

    pthread_mutex_init(&mutex_queue, NULL);
    pthread_cond_init(&cond_value, NULL);
    pthread_cond_init(&cond_value_finish, NULL);

    tpool_vars = (tpool_var_t *)calloc(file_info_nsections, sizeof(tpool_var_t));

    /* Break down the dataset into NUM_DATA_SECTIONS sections if the dataset is too big */
    if (hand.num_data_sections > 1) {
        data_in_section = true;

        data_out = (int *)calloc((hand.dset_dim1 / hand.num_data_sections) * hand.dset_dim2, sizeof(int)); /* output buffer */
    } else {
        data_out = (int *)calloc(hand.dset_dim1 * hand.dset_dim2, sizeof(int)); /* output buffer */
    }

    if (!data_out) {
	printf("data_out is NULL\n");
	goto error;
    }

    finfo_entry_num = hand.num_threads;

    /* Start to read the data using C functions without HDF5 involved */

    /* Open the file in each section */
    if (hand.num_files > 1) {
	for (i = 0; i < file_info_nsections; i++) {
	    /* All pieces of data in a section are supposed to belong to one file */
	    fp = open(file_info_array[i][0].file_name, O_RDONLY);

	    /* Put this file pointer into all the entries in this section */
	    for (j = 0; j < file_info_count[i]; j++)
		file_info_array[i][j].fp = fp;
	}
    } else {
        /* Open the file only once */
        fp = open(file_info_array[0][0].file_name, O_RDONLY);

	for (i = 0; i < file_info_nsections; i++) {
	    /* Put this file pointer into all the entries in this section */
	    for (j = 0; j < file_info_count[i]; j++)
		file_info_array[i][j].fp = fp;
	}
    }

    /* Create threads to start the thread pool */
    for (i = 0; i < hand.num_threads; i++) {
	c_info[i].thread_id = i;
	c_info[i].file_info_entry_num = finfo_entry_num;
	c_info[i].data = data_out;
        c_info[i].step = hand.step_size;

        //printf("c_info[%d].file_info_entry_num = %d, file_info_num_allocated = %d, finfo_entry_num = %d\n", i, c_info[i].file_info_entry_num, file_info_num_allocated, finfo_entry_num);

	pthread_create(&threads[i], NULL, read_in_c, &c_info[i]);
    }

    /* Start the time after starting the thread pool just like the Bypass VOL */
    gettimeofday(&begin, 0);

    /* In info.log, each section (seperated by '###') corresponds to a H5Dread.  This loop handles 
     * multiple H5Dread. */
    for (i = 0; i < file_info_nsections; i++) {
	/* First submit the number of rounds, each round of STEPs.  e.g. there are 15 entries in the file info array.
	 * If STEP is 4, the number of rounds is 3. */ 
	finfo_rounds = file_info_count[i] / hand.step_size;
	for (j = 0; j < finfo_rounds; j++)
	    submit_task(hand.step_size, i);

	/* Then submit the leftover.  Using the example above, the leftover is 3. */
	finfo_leftover = file_info_count[i] % hand.step_size;
	if (finfo_leftover) 
	    submit_task(finfo_leftover, i);

	pthread_mutex_lock(&mutex_queue);

	while (!tpool_vars[i].finished)
	    pthread_cond_wait(&cond_value_finish, &mutex_queue);


	/* Data verification if enabled.  Generally avoid checking the correctness of the data if there are more than one section
         * because the thread pool may still be reading the data during the check.  Each section corresponds to a H5Dread.
         * Sections are seperated by ### in info.log.  The way thread pool is set up doesn't guarantee the data reading 
         * is finished during the check.  Letting the main process sleep for a while may give the thread pool enough time to finish
         * reading data.
         */
	if (hand.check_data && !hand.random_data && file_info_nsections > 1) {
            sleep(1);

            if (single_file_single_dset)
	        check_data(data_out, 0, i);
            else
	        check_data(data_out, i, 0);
        }

	pthread_mutex_unlock(&mutex_queue);

	if (nerrors > 0)
	    printf("%d errors during data verification at line %d in the function %s\n", nerrors, __LINE__, __func__);
    }

    /* Wait for threads to complete */
    for (i = 0; i < hand.num_threads; i++) {
	pthread_join(threads[i], NULL);
    }

    /* Data verification if enabled.  If there is only one section to read, wait until all the threads are closed and
     * the data reading is finished.
     */
    if (hand.check_data && !hand.random_data && file_info_nsections == 1) {
        sleep(1);

	check_data(data_out, 0, 0);
    }

    /* Stop time after file closing to match the design of the Bypass VOL */
    gettimeofday(&end, 0);

    save_statistics(begin, end);

    pthread_mutex_destroy(&mutex_queue);
    pthread_cond_destroy(&cond_value);
    pthread_cond_destroy(&cond_value_finish);

    /* Close the files in all sections */
    if (hand.num_files > 1) {
	for (i = 0; i < file_info_nsections; i++)
	    close(file_info_array[i][0].fp);
    } else
	close(file_info_array[0][0].fp);

    free(data_out);
    free(tpool_vars);

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

    if (hand.num_threads == 0) {
         printf("Error: The number of child threads must be greater than zero to use thread pool.\n");
        exit(1);
    }
   
    read_info_log_file_array();

    if (hand.num_files == 1 && hand.num_dsets == 1) {
        launch_read(true);
    } else if (hand.num_files == 1 && hand.num_dsets > 1) {
        launch_read(false);
    } else if (hand.num_files > 1) {
        launch_read(false);
    }

    /* Print out the performance statistics */
    report_statistics();

    free_file_info_array();

    return 0;
}
