#include "common.h"

/*------------------------------------------------------------
 * Display command line usage
 *------------------------------------------------------------
 */
void
usage(void)
{
    printf("    [-h] [-c --dimsChunk] [-d --dimsDset] [-e --enableChunkCache] [-f --nFiles] [-k --checkData] [-n --nDsets] [-r --randomData] [-s --spaceSelect] [-t --nThreads]\n");
    printf("    [-h --help]: this help page\n");
    printf("    [-c --dimsChunk]: the 2D dimensions of the chunks.  The default is no chunking.\n");
    printf("    [-d --dimsDset]: the 2D dimensions of the datasets.  The default is 1024 x 1024.\n");
    printf("    [-e --enableChunkCache]: enable chunk cache for better data I/O performance in HDF5 library (not in Bypass VOL). The default is disabled.\n");
    printf("    [-f --nFiles]: for testing multiple files, this number must be a multiple of the number of threads.  The default is 1.\n");
    printf("    [-k --checkData]: make sure the data is correct while not running for benchmark. The default is false.\n");
    printf("    [-n --nDsets]: number of datasets in a single file.  The default is 1.\n");
    printf("    [-r --randomData]: the data has random values. The default is false.\n");
    printf("    [-s --spaceSelect]: hyperslab selection of data space.  The default is the rows divided by the number of threads (value 1)\n");
    printf("            The other options are each thread reads a row alternatively (value 2) and the columns divided by the number of threads (value 3)\n");
    printf("    [-t --nThreads]: number of threads.  The default is 1.\n");
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
                                    {"nDsets=", required_argument, NULL, 'n'},
                                    {"randomData", no_argument, NULL, 'r'},
                                    {"spaceSelect=", required_argument, NULL, 's'},
                                    {"nThreads=", required_argument, NULL, 't'},
                                    {NULL, 0, NULL, 0}};

    /* Initialize the command line options */
    hand.num_threads              = 1;
    hand.num_files                = 1;
    hand.num_dsets                = 1;
    hand.chunk_cache              = false;
    hand.plain_hdf5               = false;
    hand.check_data               = false;
    hand.random_data              = false;
    hand.read_in_c                = false;
    hand.dset_dim1                = 1024;
    hand.dset_dim2                = 1024;
    hand.chunk_dim1               = 0; /* No chunking.  Contiguous is the default. */
    hand.chunk_dim2               = 0; /* No chunking.  Contiguous is the default. */
    hand.space_select             = 1;

    while ((opt = getopt_long(argc, argv, "c:d:ef:hkn:rs:t:", long_options, NULL)) != -1) {
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
            case 'n':
                /* The number of datasets */
                if (optarg) {
                    fprintf(stdout, "number of datasets in a single file:\t\t\t%s\n", optarg);
                    hand.num_dsets = atoi(optarg);
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
                        fprintf(stdout, "options of data space selection:\t\t\trows alternated by threads\n");
                    else if (hand.space_select == 3)
                        fprintf(stdout, "options of data space selection:\t\t\tcolumns divided by the number of threads\n");
                }
                else
                    printf("optarg is null\n");
                break;
            case 't':
                /* The number of threads */
                if (optarg) {
                    fprintf(stdout, "number of threads:\t\t\t\t\t%s\n", optarg);
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
        printf("Testing multiple datasets can only be in a single file\n");
        exit(1);
    }

    if (hand.num_dsets > 1 && (hand.num_dsets % hand.num_threads != 0)) {
        printf("The number of multiple datasets must be in a multiplication of the number of threads\n");
        exit(1);
    }

    if (hand.num_files > 1 && (hand.num_files % hand.num_threads != 0)) {
        printf("The number of multiple files must be in a multiplication of the number of threads\n");
        exit(1);
    }

    if (hand.num_files == 1 && hand.num_dsets == 1 && (hand.dset_dim1 % hand.num_threads != 0)) {
        printf("The number of the row in the dataset must be in a multiplication of the number of threads\n");
        exit(1);
    }

    if (hand.random_data == true && hand.check_data == true) {
        printf("Can't verify the correctness of the data if its values are random\n");
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
 * Check the correctness of the data: the original values are
 * obsolete because of the random number involved in data 
 * inittialization.
 *------------------------------------------------------------
 */
int
check_data(int *data, int file_or_dset_index)
{
    int *p = data;
    int original_value;
    int nerrors = 0;
    int i, j;

    for (j = 0; j < hand.dset_dim1; j++) {
        for (i = 0; i < hand.dset_dim2; i++) {
            /* Compare to the values being initialized in create_files() for the cases of 
             *   1. a single dataset in a single file
             *   2. multiple datasets in a single file
             *   3. a single dataset in multiple files
             */
            original_value = i + j + file_or_dset_index * hand.dset_dim1 * hand.dset_dim2;

            if (*p != original_value) {
                printf("Data error at index (%d, %d) in line %d: actual value is %d; expected value is %d\n", i, j, __LINE__, *p, original_value);
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
