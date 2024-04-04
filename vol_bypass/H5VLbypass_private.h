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

/* Private characteristics of the bypass VOL connector */
#define H5VL_BYPASS_VERSION     0

#define POSIX_MAX_IO_BYTES INT_MAX
#define FILE_STUFF_SIZE    32
#define DSET_INFO_SIZE     32
#define INFO_SIZE          1024
#define SEL_SIZE           1024
#define DIM_RANK_MAX       32

#define GB (1024 * 1024 * 1024)

/* The pair of file names and desciptors */
typedef struct {
    char name[32];
    int  fp;
    void *vfd_file_handle;
} file_t;

static file_t *file_stuff;
static int file_stuff_count = 0;
static int file_stuff_size = FILE_STUFF_SIZE;

/* Dataset info */
typedef struct {
    char dset_name[32];
    char file_name[32];
    H5D_layout_t layout;
    hid_t dcpl_id;
    hid_t dtype_id;
    haddr_t location;
} dset_t;

//static dset_t dset_stuff[1024];
static dset_t *dset_stuff;
static int dset_count = 0;
static int dset_info_size = DSET_INFO_SIZE;

/* Log info to be written out for the C program */
typedef struct {
    char file_name[64];           /* file name to be read or written */
    char dset_name[64];           /* unused */
    haddr_t dset_loc;             /* dataset location (for contiguous) or chunk location (for chunked dataset) in bytes */
    hsize_t data_offset_file;     /* offset from 'dset_loc' in number of elements */
    hsize_t real_offset;          /* dset_loc + data_offset_file used only for sorting, not outputting to the log file  */
    hsize_t nelmts;               /* number of elements to be read or written */
    hsize_t data_offset_mem;      /* in number of elements */

} info_t;

typedef struct {
    size_t  counter;

    char    file_name[64];
    char    dset_name[64];

    hid_t   file_space_id_array[SEL_SIZE];
    hid_t   mem_space_id_array[SEL_SIZE];
    haddr_t chunk_addr_array[SEL_SIZE];

    hid_t   *file_spaces, *mem_spaces;
    haddr_t *chunk_addrs;
    int     dtype_size;

    bool    memory_allocated;
} sel_info_t;

static info_t *info_stuff;
static int info_count = 0;
static int info_size = INFO_SIZE;

#ifdef __cplusplus
extern "C" {
#endif


#ifdef __cplusplus
}
#endif

#endif /* _H5VLbypass_private_H */

