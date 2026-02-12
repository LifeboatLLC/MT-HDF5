/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Purpose:	The private header file for the bypass VOL connector.
 */

#ifndef _H5VLbypass_private_H
#define _H5VLbypass_private_H

/* Public headers needed by this file */
#include "H5VLbypass.h"        /* Public header for connector */
#include <pthread.h>
#include <stdatomic.h>

/* Private characteristics of the bypass VOL connector */
#define H5VL_BYPASS_VERSION     0

#define POSIX_MAX_IO_BYTES INT_MAX
//#define FILE_STUFF_SIZE    32
//#define DSET_INFO_SIZE     32
#define INFO_SIZE          1024
#define SEL_SIZE           1024
#define DIM_RANK_MAX       32
#define SEL_SEQ_LIST_LEN   128
#define LOCAL_VECTOR_LEN   1024
#define NUM_LOCAL_THREADS  4
#define THREAD_STEP        1024
#define NTHREADS_MIN       1
#define NTHREADS_MAX       32
#define BYPASS_NAME_SIZE_LONG   1024
#define MIN(a, b)          (((a) < (b)) ? (a) : (b))
#define GB (1024 * 1024 * 1024)
#define MB (1024 * 1024)

pthread_mutex_t mutex_local;
pthread_cond_t  cond_local;

int  nthreads_tpool       = NUM_LOCAL_THREADS;
int  nsteps_tpool         = THREAD_STEP;
int64_t  nelmts_max       = MB;
bool no_tpool             = false;                 /* use the thread pool unless the application set the environment variable "BYPASS_VOL_NO_TPOOL" */
int  info_pointer         = 0;

bool stop_tpool           = false;                   /* Flag to tell the thread pool to terminate, turned on in H5VL_bypass_term */
pthread_t th[NTHREADS_MAX];

/* Log info to be written out for the C program */
typedef struct {
    char    file_name[BYPASS_NAME_SIZE_LONG];        /* file name to be read or written */
    char    dset_name[BYPASS_NAME_SIZE_LONG];        /* unused */
    haddr_t dset_loc;             /* dataset location (for contiguous) or chunk location (for chunked dataset) in bytes */
    hsize_t data_offset_file;     /* offset from 'dset_loc' in number of elements */
    hsize_t real_offset;          /* dset_loc + data_offset_file used only for sorting, not outputting to the log file  */
    hsize_t nelmts;               /* number of elements to be read or written */
    hsize_t data_offset_mem;      /* in number of elements */
    bool    end_of_read;          /* indicates if H5Dread ends.  It's used for multiple H5Dread calls */
} info_t;

typedef struct {
    int      thread_id;
    int      fd;
} info_for_thread_t;

typedef struct dtype_info_t {
    H5T_class_t class;
    size_t size;
    H5T_sign_t sign; /* Signed vs. unsigned */
    H5T_order_t order; /* Bit order */
} dtype_info_t;

typedef struct Bypass_file_t {
    char name[BYPASS_NAME_SIZE_LONG];
    int  fd;                /* C file descriptor  */
    /* void *vfd_file_handle;  Currently not used */
    unsigned ref_count;     /* Reference count to keep track of objects like datasets or groups in this file */
    int  num_reads;         /* Number of reads still left undone */
    bool read_started;      /* Flag to indicate reads have started */
    pthread_cond_t close_ready;    /* Condition variable to indicate all reads are finished and the file can be close */
} Bypass_file_t;

/* Forward declaration of the bypass VOL connector's object */
struct H5VL_bypass_t;

/* Is name necessary? */
typedef struct Bypass_group_t {
    struct H5VL_bypass_t *file; /* File containing the group */
} Bypass_group_t;

typedef struct Bypass_dataset_t {
    hid_t dcpl_id;
    hid_t space_id;
    H5D_layout_t layout;
    int num_filters;
    dtype_info_t dtype_info;
    bool use_native;             /* Ray: remove this line */
    bool use_native_checked;     /* Ray: remove this line */
    struct H5VL_bypass_t *file;  /* Use the forward-declared type */
} Bypass_dataset_t;

/* The bypass VOL connector's object */
typedef struct H5VL_bypass_t {
    hid_t under_vol_id; /* ID for underlying VOL connector */
    void *under_object; /* Underlying VOL connector's object */
    H5I_type_t type; /* Type of this object. */

    union {
        Bypass_file_t file;
        Bypass_group_t group;
        Bypass_dataset_t dataset;
    } u;
} H5VL_bypass_t;

/* Forward declaration of Bypass_task_t and the task queue for the thread pool */
typedef struct Bypass_task_t Bypass_task_t;

typedef struct Bypass_task_t {
    int            file_index; /* Ray: Remove it.  Index of the file containing the dset to read from in the file_stuff array */
    H5VL_bypass_t *file;
    haddr_t        addr;       /* Location in filesystem file to read from */
    size_t         size;
    void          *vec_buf;    /* User buffer to populate */
    atomic_int    *task_count_ptr;
    pthread_cond_t *local_condition_ptr;
    Bypass_task_t *next;
} Bypass_task_t;

typedef struct task_queue_t {
    Bypass_task_t *bypass_queue_head_g;
    Bypass_task_t *bypass_queue_tail_g;
    int            tasks_in_queue;      /* Used only by the queue local to each thread when the thread pool isn't used */
} task_queue_t;

task_queue_t queue_for_tpool;

typedef struct {
    size_t  counter;

    char    dset_name[BYPASS_NAME_SIZE_LONG];

    hid_t   file_space_id;
    hid_t   mem_space_id;
    haddr_t chunk_addr;

    H5VL_bypass_t *file; // TODO: Replace this and names above with dset pointer
    int     dtype_size;

    bool    memory_allocated;
    atomic_int *task_count_ptr;
    pthread_cond_t *local_condition_ptr;
} sel_info_t;

static info_t *info_stuff;
static int64_t info_count = 0;
static int64_t info_size = INFO_SIZE;
static info_for_thread_t *info_for_thread;

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif

#endif /* _H5VLbypass_private_H */
