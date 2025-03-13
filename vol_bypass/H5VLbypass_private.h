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

/* Private characteristics of the bypass VOL connector */
#define H5VL_BYPASS_VERSION     0

#define POSIX_MAX_IO_BYTES INT_MAX
#define FILE_STUFF_SIZE    32
#define DSET_INFO_SIZE     32
#define INFO_SIZE          1024
#define SEL_SIZE           1024
#define DIM_RANK_MAX       32
#define SEL_SEQ_LIST_LEN   128
#define LOCAL_VECTOR_LEN   1024
#define NUM_LOCAL_THREADS  4
#define THREAD_STEP        1024
#define NTHREADS_MAX       32
#define BYPASS_NAME_SIZE_LONG   1024
#define MIN(a, b)          (((a) < (b)) ? (a) : (b))
#define GB (1024 * 1024 * 1024)
#define MB (1024 * 1024)

pthread_mutex_t mutex_local;
pthread_cond_t  cond_queue_not_empty;
pthread_cond_t  cond_read_finished;

int  nthreads_tpool       = NUM_LOCAL_THREADS;
int  nsteps_tpool         = THREAD_STEP;
int  info_pointer         = 0;
int  tasks_in_queue    = 0;
int  tasks_unfinished  = 0;
bool all_tasks_enqueued = false;                   /* Flag for H5Dread to notify the thread pool that it finished putting tasks in the queue */
bool stop_tpool           = false;                   /* Flag to tell the thread pool to terminate, turned on in H5VL_bypass_term */
pthread_t th[NTHREADS_MAX];

/* Log info to be written out for the C program */
typedef struct {
    char    file_name[64];        /* file name to be read or written */
    char    dset_name[64];        /* unused */
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

typedef struct task_file_t {
    char name[BYPASS_NAME_SIZE_LONG]; /* TBD - Can be removed once log file is no longer needed */
    int  fd;                /* C file descriptor  */
    /* void *vfd_file_handle;  Currently not used */
    unsigned rc;            /* Reference count    */
} task_file_t;

typedef struct Bypass_file_t {
    char name[BYPASS_NAME_SIZE_LONG];
    unsigned ref_count;     /* Reference count    */
    task_file_t *task_file;
} Bypass_file_t;

/* Forward declaration of the bypass VOL connector's object */
struct H5VL_bypass_t;

typedef struct Bypass_dataset_t {
    hid_t dcpl_id;
    hid_t space_id;
    H5D_layout_t layout;
    int num_filters;
    dtype_info_t dtype_info;
    struct H5VL_bypass_t *file;  /* Use the forward-declared type */
} Bypass_dataset_t;

typedef struct Bypass_group_t {
    struct H5VL_bypass_t *file; /* File containing the group */
} Bypass_group_t;

/* The bypass VOL connector's object */
typedef struct H5VL_bypass_t {
    hid_t under_vol_id; /* ID for underlying VOL connector */
    void *under_object; /* Underlying VOL connector's object */
    H5I_type_t type; /* Type of this object. */
    bool top_only;

    union {
        Bypass_dataset_t dataset;
        Bypass_file_t file;
        Bypass_group_t group;
    } u;
} H5VL_bypass_t;

typedef struct {
    int      thread_id;
    int      fd;            /* Remove this field and use file_indices */
    uint32_t step;
    haddr_t  *addrs;
    size_t   *sizes;
    void     **vec_bufs;
    task_file_t **files;

    haddr_t    addrs_local[LOCAL_VECTOR_LEN];
    size_t     sizes_local[LOCAL_VECTOR_LEN];
    void       *vec_bufs_local[LOCAL_VECTOR_LEN];
    task_file_t *files_local[LOCAL_VECTOR_LEN];

    size_t     vec_arr_nalloc;
    size_t     vec_arr_nused;
    bool       free_memory;
} info_for_tpool_t;

typedef struct {
    size_t  counter;

    char    dset_name[BYPASS_NAME_SIZE_LONG];

    hid_t   file_space_id;
    hid_t   mem_space_id;
    haddr_t chunk_addr;

    task_file_t *task_file;
    int     dtype_size;

    bool    memory_allocated;
} sel_info_t;

static info_t *info_stuff;
static int info_count = 0;
static int info_size = INFO_SIZE;
static info_for_tpool_t md_for_thread;
static info_for_thread_t *info_for_thread;

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif

#endif /* _H5VLbypass_private_H */
