#include "common.h"

/*------------------------------------------------------------
 * Display command line usage
 *------------------------------------------------------------
 */
void
usage(void)
{
    printf("    [-h] [-c --dimsChunk] [-d --dimsDset] [-e --enableChunkCache] [-f --nFiles] [-k --checkData] [-m -stepSize] [-n --nDsets] [-q --nSections] [-r --randomData] [-s --spaceSelect] [-t --nThreads]\n");
    printf("    [-h --help]: this help page\n");
    printf("    [-c --dimsChunk]: the 2D dimensions of the chunks.  The default is no chunking.\n");
    printf("    [-d --dimsDset]: the 2D dimensions of the datasets.  The default is 1024 x 1024.\n");
    printf("    [-e --enableChunkCache]: enable chunk cache for better data I/O performance in HDF5 library (not in Bypass VOL). The default is disabled.\n");
    printf("    [-f --nFiles]: for testing multiple files, this number must be a multiple of the number of threads.  The default is 1.\n");
    printf("    [-k --checkData]: make sure the data is correct while not running for benchmark. The default is false.\n");
    printf("    [-l --multiDsets]: read multiple datasets using H5Dread_multi. The default is false.\n");
    printf("    [-m --stepSize]: the number of data pieces passed into the thread pool.  The default is 1.\n");
    printf("    [-n --nDsets]: number of datasets in a single file.  The default is 1.\n");
    printf("    [-q --nSections]: number of data sections to break down a large dataset.  The default is 1.\n");
    printf("    [-r --randomData]: the data has random values. The default is false.\n");
    printf("    [-s --spaceSelect]: hyperslab selection of data space.  The default is the rows divided by the number of threads - value 1\n");
    printf("            The other options are unsurppoted\n");
    printf("    [-t --nThreads]: number of child threads in addition to the main process.  The default is 1.\n");
    printf("\n");
}

/*------------------------------------------------------------
 * Parse command line option
 *------------------------------------------------------------
 */
void
parse_command_line(int argc, char *argv[])
{
    int           opt;
    struct option long_options[] = {
                                    {"dimsChunk=", required_argument, NULL, 'c'},
                                    {"dimsDset=", required_argument, NULL, 'd'},
                                    {"enableChunkCache", no_argument, NULL, 'e'},
                                    {"nFiles=", required_argument, NULL, 'f'},
                                    {"help", no_argument, NULL, 'h'},
                                    {"checkData", no_argument, NULL, 'k'},
                                    {"stepSize=", required_argument, NULL, 'm'},
                                    {"nDsets=", required_argument, NULL, 'n'},
                                    {"nSections=", required_argument, NULL, 'q'},
                                    {"randomData", no_argument, NULL, 'r'},
                                    {"spaceSelect=", required_argument, NULL, 's'},
                                    {"nThreads=", required_argument, NULL, 't'},
                                    {"multiDsets", no_argument, NULL, 'l'},
                                    {NULL, 0, NULL, 0}};

    /* Initialize the command line options */
    hand.num_threads              = 0; /* No child thread. Serial only            */
    hand.num_files                = 1;
    hand.num_dsets                = 1;
    hand.num_data_sections        = 1;
    hand.step_size                = 1;
    hand.chunk_cache              = false;
    hand.plain_hdf5               = false;
    hand.check_data               = false;
    hand.random_data              = false;
    hand.read_in_c                = false;
    hand.multi_dsets              = false;
    hand.dset_dim1                = 1024;
    hand.dset_dim2                = 1024;
    hand.chunk_dim1               = 0; /* No chunking.  Contiguous is the default. */
    hand.chunk_dim2               = 0; /* No chunking.  Contiguous is the default. */
    hand.space_select             = 1; /* Other values are not supported           */

    while ((opt = getopt_long(argc, argv, "c:d:ef:hklm:n:q:rs:t:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                /* The dimensions of the chunks */
                if (optarg) {
                    char *dims_str, *dim1_str, *dim2_str;
                    fprintf(stdout, "dimensions of chunks:\t\t\t\t\t\%s\n", optarg);
                    dims_str       = strdup(optarg);
                    dim1_str       = strtok(dims_str, "x");
                    dim2_str       = strtok(NULL, "x");
                    hand.chunk_dim1 = atoll(dim1_str);
                    hand.chunk_dim2 = atoll(dim2_str);
                    free(dims_str);
                }
                else
                    printf("optarg is null\n");
                break;
            case 'd':
                /* The dimensions of the dataset */
                if (optarg) {
                    char *dims_str, *dim1_str, *dim2_str;
                    fprintf(stdout, "dimensions of dataset:\t\t\t\t\t\%s\n", optarg);
                    dims_str       = strdup(optarg);
                    dim1_str       = strtok(dims_str, "x");
                    dim2_str       = strtok(NULL, "x");
                    hand.dset_dim1 = atoll(dim1_str);
                    hand.dset_dim2 = atoll(dim2_str);
                    free(dims_str);
                }
                else
                    printf("optarg is null\n");
                break;
            case 'e':
                /* Assign random values to the data during file creation */
                fprintf(stdout, "enable chunk cache in the HDF5 library:\t\t\tTrue\n");
                hand.chunk_cache = true;

                break;
            case 'f':
                /* The number of HDF5 files to be tested */
                if (optarg) {
                    fprintf(stdout, "number of files:\t\t\t\t\t%s\n", optarg);
                    hand.num_files = atoi(optarg);
                }
                else
                    printf("optarg is null\n");
                break;
            case 'h':
                fprintf(stdout, "Help page:\n");
                usage();

                exit(0);

                break;
            case 'k':
                /* Check the correctness of the data while not running benchmark */
                fprintf(stdout, "check the data correctness:\t\t\t\tTrue\n");
                hand.check_data = true;

                break;
            case 'l':
                /* Check the correctness of the data while not running benchmark */
                fprintf(stdout, "read multiple datasets with H5Dread_multi:\t\tTrue\n");
                hand.multi_dsets = true;

                break;
            case 'm':
                /* the number of data pieces passed into the thread pool */
                if (optarg) {
                    fprintf(stdout, "number of data pieces for thread pool:\t\t\t%s\n", optarg);
                    hand.step_size = atoi(optarg);
                }
                else
                    printf("optarg is null\n");
                break;
            case 'n':
                /* The number of datasets */
                if (optarg) {
                    fprintf(stdout, "number of datasets in a single file:\t\t\t%s\n", optarg);
                    hand.num_dsets = atoi(optarg);
                }
                else
                    printf("optarg is null\n");
                break;
            case 'q':
                /* The number of data sections to break down a large dataset */
                if (optarg) {
                    fprintf(stdout, "number of data sections:\t\t\t\t%s\n", optarg);
                    hand.num_data_sections = atoi(optarg);
                }
                else
                    printf("optarg is null\n");
                break;
            case 'r':
                /* Assign random values to the data during file creation */
                fprintf(stdout, "assign random values to the data:\t\t\t\tTrue\n");
                hand.random_data = true;

                break;
            case 's':
                /* The options of data space selection */
                if (optarg) {
                    hand.space_select = atoi(optarg);

                    if (hand.space_select == 1)
                        fprintf(stdout, "options of data space selection:\t\t\trows divided by the number of threads\n");
                    else if (hand.space_select == 2)
                        fprintf(stdout, "options of data space selection:\t\t\trows alternated by threads (unsuppoted)\n");
                    else if (hand.space_select == 3)
                        fprintf(stdout, "options of data space selection:\t\t\tcolumns divided by the number of threads (unsuppoted)\n");
                }
                else
                    printf("optarg is null\n");
                break;
            case 't':
                /* The number of child threads */
                if (optarg) {
                    fprintf(stdout, "number of child threads:\t\t\t\t%s\n", optarg);
                    hand.num_threads = atoi(optarg);
                }
                else
                    printf("optarg is null\n");
                break;
            case ':':
                printf("option needs a value\n");
                break;
            case '?':
                printf("unknown option: %c\n", optopt);
                break;
        }
    }

    /* optind is for the extra arguments which are not parsed */
    for (; optind < argc; optind++) {
        printf("extra arguments not parsed: %s\n", argv[optind]);
    }

    /* Make sure there is no conflict in the command line options */
    if (hand.num_dsets > 1 && hand.num_files != 1) {
        printf("Error: Testing multiple datasets can only be in a single file\n");
        exit(1);
    }

    if (hand.num_dsets > 1 && hand.num_threads > 0 && (hand.num_dsets % hand.num_threads != 0)) {
        printf("Error: The number of multiple datasets must be in a multiplication of the number of threads\n");
        exit(1);
    }

    if (hand.num_files > 1 && hand.num_threads > 0 && (hand.num_files % hand.num_threads != 0)) {
        printf("Error: The number of multiple files must be in a multiplication of the number of threads\n");
        exit(1);
    }

    if (hand.num_files == 1 && hand.num_dsets == 1 && hand.num_threads != 0 && (hand.dset_dim1 % hand.num_threads != 0)) {
        printf("Error: The number of the row in the dataset must be in a multiplication of the number of threads\n");
        exit(1);
    }

    /* Make sure there is no conflict in the command line options */
    if (hand.num_data_sections < 1 || hand.num_data_sections > hand.dset_dim1) {
        printf("Error: Wrong number of dataset sections\n");
        exit(1);
    }

    if (hand.dset_dim1 % hand.num_data_sections != 0) {
        printf("Error: The number of dataset sections must evenly divide the number of the dataset rows\n");
        exit(1);
    }

    if (hand.random_data == true && hand.check_data == true) {
        printf("Error: Can't verify the correctness of the data if its values are random\n");
        exit(1);
    }
}

/*------------------------------------------------------------
 * Save the performance data
 *------------------------------------------------------------
 */
void
save_statistics(struct timeval begin, struct timeval end)
{
    double time = (end.tv_sec - begin.tv_sec) + (end.tv_usec - begin.tv_usec) * 1e-6;
    double total_data = 0;

    if (hand.num_files == 1 && hand.num_dsets == 1)
        total_data = hand.dset_dim1 * hand.dset_dim2 * sizeof(int) / MB;
    else if (hand.num_files == 1 && hand.num_dsets > 1) {
        total_data = hand.num_dsets * hand.dset_dim1 * hand.dset_dim2 * sizeof(int) / MB;
    } else if (hand.num_files > 1) {
        total_data = (hand.num_files * hand.dset_dim1 * hand.dset_dim2 * sizeof(int)) / MB;
    }

    statistics.data_amount = total_data;

    statistics.time = time;
    statistics.speed = total_data / time;
}

/*------------------------------------------------------------
 * Print out the performance data
 *------------------------------------------------------------
 */
void
report_statistics()
{
    int i;

    printf("\nReading data: ");
    printf("total data = %.2lfMB, time = %lfseconds, speed = %.2lfMB/second\n", statistics.data_amount, statistics.time, statistics.speed);
}

/*------------------------------------------------------------
 * Check the correctness of the data
 *------------------------------------------------------------
 */
int
check_data(int *data, int file_or_dset_index, int data_section)
{
    int *p = data;
    int original_value;
    int num_rows;
    int nerrors = 0;
    int i, j;

    if (data_in_section)
        num_rows = hand.dset_dim1 / hand.num_data_sections;
    else
        num_rows = hand.dset_dim1;

    for (i = 0; i < num_rows; i++) {
        for (j = 0; j < hand.dset_dim2; j++) {
            /* Compare to the values being initialized in create_files() for the cases of 
             *   1. a single dataset in a single file
             *   2. multiple datasets in a single file
             *   3. a single dataset in multiple files
             */
            original_value = i + j + data_section * 10 + file_or_dset_index * hand.dset_dim1 * hand.dset_dim2;

            if (*p != original_value) {
                printf("Data (section %d) error at index (%d, %d) in line %d: actual value is %d; expected value is %d\n", data_section, i, j, __LINE__, *p, original_value);
                nerrors++;
            }

            p++;
        }
    }

    return nerrors;
}

/*------------------------------------------------------------
 * Handles reading the data bigger than or equal to 2GB
 *------------------------------------------------------------
 */
void read_big_data(int fd, int *buf, size_t size, off_t offset)
{
    while (size > 0) {
        size_t bytes_in   = 0;  /* # of bytes to read       */
        size_t bytes_read = -1; /* # of bytes actually read */

        /* Trying to read more bytes than the return type can handle is
         * undefined behavior in POSIX.
         */
        if (size > POSIX_MAX_IO_BYTES)
            bytes_in = POSIX_MAX_IO_BYTES;
        else
            bytes_in = size;

        do {
            bytes_read = pread(fd, buf, bytes_in, offset);
            if (bytes_read > 0)
                offset += bytes_read;
        } while (-1 == bytes_read && EINTR == errno);

        size -= (size_t)bytes_read;
        buf = (int *)((char *)buf + bytes_read);
    } /* end while */
}

/* Figure out the number of sections in the info log file */
static int
get_num_of_sections(char *input_str, char *section_break)
{
    int i, j;
    int input_len = 0, section_break_len = 0;
    int numb_matched = 0, numb_sections = 0;

    input_len = strlen(input_str);
    section_break_len = strlen(section_break);

    for (i = 0; i < input_len;)
    {
        j = 0;
        numb_matched = 0;

        while (i < input_len && input_str[i] == section_break[j])
        {
            numb_matched++;
            i++;
            j++;
        }

        if (numb_matched == section_break_len) {
            numb_sections++;
            numb_matched = 0;
        } else
            i++;
    }

    return numb_sections;
}

/* Read a section from the info log file */
static void
read_file_info_sections(char *section_buf, int i)
{
    int counter = 0;
    int finfo_entry_num = hand.num_threads;
    size_t len;
    char *token = NULL;
    const char delimiter[] = " \n\0";
    int j;

    if (hand.num_threads == 0)
        finfo_entry_num = 1;    
    else
        finfo_entry_num = hand.num_threads;

    file_info_array[i] = (file_info_t *)malloc(sizeof(file_info_t) * finfo_entry_num);

    /* Begin to parse the buffer containing the contents of the log file */
    counter = 0;

    /* File name */
    token = strtok(section_buf, delimiter);
    //printf("%s ", token);
    strcpy(file_info_array[i][counter].file_name, token);

    /* Dataset name (unused) */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    strcpy(file_info_array[i][counter].dset_name, token);

    /* Location of dataset in the file */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    file_info_array[i][counter].dset_offset = atoll(token);

    /* Offset of data in the HDF5 file to be read into the memory */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    file_info_array[i][counter].offset_f = atoll(token);

    /* Number of elements to be read */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    file_info_array[i][counter].nelmts = atoll(token);

    /* Offset of the data in the memory to be read */
    token = strtok(NULL, delimiter);
    //printf("%s ", token);
    file_info_array[i][counter].offset_m = atoll(token);

    while (1) {
        /* File name */
        token = strtok(NULL, delimiter);
        if (!token)
            break;
        //printf("%s ", token);

        counter++;

        if (counter == finfo_entry_num) {
            finfo_entry_num *= 2;
            file_info_array[i] = (file_info_t *)realloc(file_info_array[i], finfo_entry_num * sizeof(file_info_t));
        }

        strcpy(file_info_array[i][counter].file_name, token);

        /* Dataset name (unused) */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        strcpy(file_info_array[i][counter].dset_name, token);

        /* Location of dataset in the file */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        file_info_array[i][counter].dset_offset = atoll(token);

        /* Offset of data in the HDF5 file to be read into the memory */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        file_info_array[i][counter].offset_f = atoll(token);

        /* Number of elements to be read */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        file_info_array[i][counter].nelmts = atoll(token);

        /* Offset of the data in the memory to be read */
        token = strtok(NULL, delimiter);
        //printf("%s ", token);
        file_info_array[i][counter].offset_m = atoll(token);
    }

    /* Total number of entries to be returned */
    counter++;

    file_info_count[i] = counter;

    /* printf("Print out file_info_array[%d]:\n", i);
    if (hand.num_files == 1 && hand.num_dsets == 1) {
        for (j = 0; j < counter; j++)
            printf("%s %s %lld %lld %lld %lld\n", file_info_array[i][j].file_name, file_info_array[i][j].dset_name, file_info_array[i][j].dset_offset, file_info_array[i][j].offset_f, file_info_array[i][j].nelmts, file_info_array[i][j].offset_m);
    }
    printf("\n"); */

}

/*------------------------------------------------------------
 * Read and parse the info.log file for the preparation of  
 * reading the data in C only.  This function handles multiple
 * sections generated from multiple calls to H5Dread
 *------------------------------------------------------------
 */
int read_info_log_file_array(void)
{
    FILE *fp = NULL;
    int display;
    int i, counter = 0;
    size_t len;
    char *buf = NULL;
    char *buf_p = NULL;
    char *section = NULL;
    char *section_buf = NULL;
    char **section_array = NULL;

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
    buf_p = buf;

    fclose(fp);

    /* Find out the number of sections */
    file_info_nsections = get_num_of_sections(buf, SECTION_BREAK);

    file_info_count = (int *)malloc(sizeof(int) * file_info_nsections);

    /* Allocate space for file info */
    file_info_array = (file_info_t **)malloc(sizeof(file_info_t *) * file_info_nsections);

    section_array = (char **)malloc(sizeof(char *) * file_info_nsections);

    /* Find the first section.  Section points to the first SECTION_BREAK now. */
    section = strstr(buf, SECTION_BREAK);
    counter = 0;

    while (section) {
        /* Replace the delimiter with a null character to extract the substring */
        *section = '\0';
        //printf("section = %s\n", section);
        //printf("buf = %s\n", buf);

        /* Copy this section */
        section_array[counter] = strdup(buf);
	counter++;

        /* Move the pointer to the next substring */
        buf = section + strlen(SECTION_BREAK);
        section = strstr(buf, SECTION_BREAK);
    }

    /* Read each section and save the information */
    for (i = 0; i < file_info_nsections; i++) {
        read_file_info_sections(section_array[i], i);
    }

    /* Free the memory buffers */
    for (i = 0; i < counter; i++)
        if (section_array[i])
            free(section_array[i]);
    free(section_array);

    if (buf_p)
        free(buf_p);

    return 0;

error:
    return -1;
}

void free_file_info_array()
{
    int i;

    for (i = 0; i < file_info_nsections; i++)
        free(file_info_array[i]);

    free(file_info_array);

    free(file_info_count);
}
