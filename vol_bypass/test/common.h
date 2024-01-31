#ifndef COMMON_H
#define COMMON_H

/* Public headers needed by this file */
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include <assert.h>
#include <getopt.h>

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <errno.h>
#include <fcntl.h>

#define POSIX_MAX_IO_BYTES INT_MAX
#define MB (1024 * 1024)
#define GB (1024 * 1024 * 1024)

typedef struct {
    int   num_threads;
    int   num_files;
    int   num_dsets;
    long long int   dset_dim1;
    long long int   dset_dim2;
    long long int   chunk_dim1;
    long long int   chunk_dim2;
    int   space_select;
    bool  check_data;
    bool  random_data;
    bool  plain_hdf5;
    bool  read_in_c;
} handler_t;

typedef struct {
    char file_name[32];
    char dset_name[32];
    long long int  dset_offset;
    long long int  offset_f;
    long long int  nelmts;
    long long int  offset_m;
} file_info_t;

typedef struct {
    int round_index;
    double data_amount;
    double time;
    double speed;
} statistics_t;

handler_t    hand;
file_info_t  *file_info;
statistics_t statistics;

void usage(void);
void parse_command_line(int argc, char *argv[]);
int read_data(int fd, int *buf, size_t size, off_t offset);

#endif /* COMMON_H */
