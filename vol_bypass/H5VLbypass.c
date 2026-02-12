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
 * Purpose:     This is a "bypass" VOL connector, which forwards each
 *              VOL callback to an underlying connector.
 *
 *              It is designed as an example VOL connector for developers to
 *              use when creating new connectors, especially connectors that
 *              are outside of the HDF5 library.  As such, it should _NOT_
 *              include _any_ private HDF5 header files.  This connector should
 *              therefore only make public HDF5 API calls and use standard C /
 *              POSIX calls.
 *
 *              Note that the HDF5 error stack must be preserved on code paths
 *              that could be invoked when the underlying VOL connector's
 *              callback can fail.
 *
 */

/* Header files needed */
/* Do NOT include private HDF5 files here! */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/resource.h>

/* Public HDF5 headers */
#include "hdf5.h"

/* This connector's private header */
#include "H5VLbypass_private.h"

extern int errno;

/**********/
/* Macros */
/**********/

/* Whether to display log messge when callback is invoked */
/* (Uncomment to enable) */
/* #define ENABLE_BYPASS_LOGGING */

/* DIM_RANK_MAX zeros */
#define ZERO_OFFSETS   (hsize_t[]) {0, 0, 0, 0, 0, 0, 0, 0,\
                                    0, 0, 0, 0, 0, 0, 0, 0,\
                                    0, 0, 0, 0, 0, 0, 0, 0,\
                                    0, 0, 0, 0, 0, 0, 0, 0}

/************/
/* Typedefs */
/************/

/* The bypass VOL wrapper context */
typedef struct H5VL_bypass_wrap_ctx_t {
    hid_t under_vol_id;   /* VOL ID for under VOL */
    void *under_wrap_ctx; /* Object wrapping context for under VOL */
} H5VL_bypass_wrap_ctx_t;

/* Struct to store info for chunk iteration.*/
typedef struct chunk_cb_info_t {
    hid_t file_space;
    hid_t mem_space;
    hid_t file_space_copy;
    int dset_dim_rank;
    H5S_sel_type select_type;
    hsize_t dset_dims[DIM_RANK_MAX];
    hsize_t chunk_dims[DIM_RANK_MAX]; // TBD: Assumes chunk dims are treated as constant even for edge chunks
    sel_info_t *selection_info;
    void *rbuf;
    task_queue_t *task_queue;
} chunk_cb_info_t;

/********************* */
/* Function prototypes */
/********************* */

/* Helper routines */
static H5VL_bypass_t *H5VL_bypass_new_obj(void *under_obj, hid_t under_vol_id);
static herr_t         H5VL_bypass_free_obj(H5VL_bypass_t *obj);

/* "Management" callbacks */
static herr_t H5VL_bypass_init(hid_t vipl_id);
static herr_t H5VL_bypass_term(void);

/* VOL info callbacks */
static void  *H5VL_bypass_info_copy(const void *info);
static herr_t H5VL_bypass_info_cmp(int *cmp_value, const void *info1, const void *info2);
static herr_t H5VL_bypass_info_free(void *info);
static herr_t H5VL_bypass_info_to_str(const void *info, char **str);
static herr_t H5VL_bypass_str_to_info(const char *str, void **info);

/* VOL object wrap / retrieval callbacks */
static void  *H5VL_bypass_get_object(const void *obj);
static herr_t H5VL_bypass_get_wrap_ctx(const void *obj, void **wrap_ctx);
static void  *H5VL_bypass_wrap_object(void *obj, H5I_type_t obj_type, void *wrap_ctx);
static void  *H5VL_bypass_unwrap_object(void *obj);
static herr_t H5VL_bypass_free_wrap_ctx(void *obj);

/* Attribute callbacks */
static void  *H5VL_bypass_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                      hid_t type_id, hid_t space_id, hid_t acpl_id, hid_t aapl_id,
                                      hid_t dxpl_id, void **req);
static void  *H5VL_bypass_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                    hid_t aapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_read(void *attr, hid_t mem_type_id, void *buf, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_write(void *attr, hid_t mem_type_id, const void *buf, hid_t dxpl_id,
                                     void **req);
static herr_t H5VL_bypass_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                        H5VL_attr_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_attr_close(void *attr, hid_t dxpl_id, void **req);

/* Dataset callbacks */
static void  *H5VL_bypass_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                         hid_t lcpl_id, hid_t type_id, hid_t space_id, hid_t dcpl_id,
                                         hid_t dapl_id, hid_t dxpl_id, void **req);
static void  *H5VL_bypass_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                       hid_t dapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_dataset_read(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[],
                                       hid_t file_space_id[], hid_t plist_id, void *buf[], void **req);
static herr_t H5VL_bypass_dataset_write(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[],
                                        hid_t file_space_id[], hid_t plist_id, const void *buf[], void **req);
static herr_t H5VL_bypass_dataset_get(void *dset, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_dataset_specific(void *obj, H5VL_dataset_specific_args_t *args, hid_t dxpl_id,
                                           void **req);
static herr_t H5VL_bypass_dataset_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_dataset_close(void *dset, hid_t dxpl_id, void **req);

/* Datatype callbacks */
static void  *H5VL_bypass_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                          hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id,
                                          hid_t dxpl_id, void **req);
static void  *H5VL_bypass_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                        hid_t tapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_datatype_get(void *dt, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_datatype_specific(void *obj, H5VL_datatype_specific_args_t *args, hid_t dxpl_id,
                                            void **req);
static herr_t H5VL_bypass_datatype_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* File callbacks */
static void  *H5VL_bypass_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id,
                                      hid_t dxpl_id, void **req);
static void  *H5VL_bypass_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id,
                                    void **req);
static herr_t H5VL_bypass_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_file_specific(void *file, H5VL_file_specific_args_t *args, hid_t dxpl_id,
                                        void **req);
static herr_t H5VL_bypass_file_optional(void *file, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_file_close(void *file, hid_t dxpl_id, void **req);

/* Group callbacks */
static void  *H5VL_bypass_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                       hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req);
static void  *H5VL_bypass_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name,
                                     hid_t gapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_group_specific(void *obj, H5VL_group_specific_args_t *args, hid_t dxpl_id,
                                         void **req);
static herr_t H5VL_bypass_group_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_group_close(void *grp, hid_t dxpl_id, void **req);

/* Link callbacks */
static herr_t H5VL_bypass_link_create(H5VL_link_create_args_t *args, void *obj,
                                      const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id,
                                      hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                                    const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id,
                                    hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                                    const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id,
                                    hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_args_t *args,
                                   hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                        H5VL_link_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_link_optional(void *obj, const H5VL_loc_params_t *loc_params,
                                        H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Object callbacks */
static void  *H5VL_bypass_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type,
                                      hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_object_copy(void *src_obj, const H5VL_loc_params_t *src_loc_params,
                                      const char *src_name, void *dst_obj,
                                      const H5VL_loc_params_t *dst_loc_params, const char *dst_name,
                                      hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_object_get(void *obj, const H5VL_loc_params_t *loc_params,
                                     H5VL_object_get_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_object_specific(void *obj, const H5VL_loc_params_t *loc_params,
                                          H5VL_object_specific_args_t *args, hid_t dxpl_id, void **req);
static herr_t H5VL_bypass_object_optional(void *obj, const H5VL_loc_params_t *loc_params,
                                          H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Container/connector introspection callbacks */
static herr_t H5VL_bypass_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl,
                                                  const H5VL_class_t **conn_cls);
static herr_t H5VL_bypass_introspect_get_cap_flags(const void *info, uint64_t *cap_flags);
static herr_t H5VL_bypass_introspect_opt_query(void *obj, H5VL_subclass_t cls, int op_type, uint64_t *flags);

/* Async request callbacks */
static herr_t H5VL_bypass_request_wait(void *req, uint64_t timeout, H5VL_request_status_t *status);
static herr_t H5VL_bypass_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx);
static herr_t H5VL_bypass_request_cancel(void *req, H5VL_request_status_t *status);
static herr_t H5VL_bypass_request_specific(void *req, H5VL_request_specific_args_t *args);
static herr_t H5VL_bypass_request_optional(void *req, H5VL_optional_args_t *args);
static herr_t H5VL_bypass_request_free(void *req);

/* Blob callbacks */
static herr_t H5VL_bypass_blob_put(void *obj, const void *buf, size_t size, void *blob_id, void *ctx);
static herr_t H5VL_bypass_blob_get(void *obj, const void *blob_id, void *buf, size_t size, void *ctx);
static herr_t H5VL_bypass_blob_specific(void *obj, void *blob_id, H5VL_blob_specific_args_t *args);
static herr_t H5VL_bypass_blob_optional(void *obj, void *blob_id, H5VL_optional_args_t *args);

/* Token callbacks */
static herr_t H5VL_bypass_token_cmp(void *obj, const H5O_token_t *token1, const H5O_token_t *token2,
                                    int *cmp_value);
static herr_t H5VL_bypass_token_to_str(void *obj, H5I_type_t obj_type, const H5O_token_t *token,
                                       char **token_str);
static herr_t H5VL_bypass_token_from_str(void *obj, H5I_type_t obj_type, const char *token_str,
                                         H5O_token_t *token);

/* Generic optional callback */
static herr_t H5VL_bypass_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req);

/* Flush the file containing the provided dataset */
static herr_t flush_containing_file(H5VL_bypass_t *file);

/* Populate the dataset structure on the bypass object */
static herr_t dset_open_helper(H5VL_bypass_t *obj, hid_t dxpl_id, void **req);

/* Release the structures associated with the dataset object */
static herr_t release_dset_info(Bypass_dataset_t *dset);

/* Release the structures associated with the file object */
static herr_t release_file_info(Bypass_file_t *file);

/* Release the structures associated with the group object */
static herr_t release_group_info(Bypass_group_t *group);

/* Retrieve the in-file location of the dataset, if any */
static herr_t get_dset_location(H5VL_bypass_t *dset_obj, hid_t dxpl_id, void **req, haddr_t *location);

/* Check if any property of the dataset (type, layout, etc.) should force use of Native VOL */
static herr_t should_dset_use_native(Bypass_dataset_t *dset);

/* Compare two datatype instances for equivalence*/
static bool bypass_types_equal(dtype_info_t *type_info1, dtype_info_t *type_info2);

/* Retrieve and store datatype information on a dataset object */
static herr_t get_dtype_info(H5VL_bypass_t *dset_obj, hid_t dxpl_id, void **req);

/* Populate datatype information from a datatype ID */
static herr_t get_dtype_info_helper(hid_t type_id, dtype_info_t *type_info_out);

static H5D_space_status_t
get_dset_space_status(H5VL_bypass_t *dset_obj, hid_t dxpl_id, void **req);

/* Functions for task queue for the thread pool to process */
static herr_t bypass_queue_destroy(task_queue_t *queue, bool need_mutex);
static herr_t bypass_queue_push(task_queue_t *queue, Bypass_task_t *task, bool need_mutex);
static Bypass_task_t * bypass_queue_pop(task_queue_t *queue, bool need_mutex);
static Bypass_task_t * bypass_task_create(sel_info_t *sel_info, haddr_t addr, size_t io_len, void *buf);
static herr_t bypass_task_release(Bypass_task_t *task);

/*******************/
/* Local variables */
/*******************/

/* Bypass VOL connector class struct */
static const H5VL_class_t H5VL_bypass_g = {
    H5VL_VERSION,                          /* VOL class struct version */
    (H5VL_class_value_t)H5VL_BYPASS_VALUE, /* value        */
    H5VL_BYPASS_NAME,                      /* name         */
    H5VL_BYPASS_VERSION,                   /* connector version */
    H5VL_CAP_FLAG_THREADSAFE,              /* capability flags for thread-safe or multithread */
    H5VL_bypass_init,                      /* initialize   */
    H5VL_bypass_term,                      /* terminate    */
    {
        /* info_cls */
        sizeof(H5VL_bypass_info_t), /* size    */
        H5VL_bypass_info_copy,      /* copy    */
        H5VL_bypass_info_cmp,       /* compare */
        H5VL_bypass_info_free,      /* free    */
        H5VL_bypass_info_to_str,    /* to_str  */
        H5VL_bypass_str_to_info     /* from_str */
    },
    {
        /* wrap_cls */
        H5VL_bypass_get_object,    /* get_object   */
        H5VL_bypass_get_wrap_ctx,  /* get_wrap_ctx */
        H5VL_bypass_wrap_object,   /* wrap_object  */
        H5VL_bypass_unwrap_object, /* unwrap_object */
        H5VL_bypass_free_wrap_ctx  /* free_wrap_ctx */
    },
    {
        /* attribute_cls */
        H5VL_bypass_attr_create,   /* create */
        H5VL_bypass_attr_open,     /* open */
        H5VL_bypass_attr_read,     /* read */
        H5VL_bypass_attr_write,    /* write */
        H5VL_bypass_attr_get,      /* get */
        H5VL_bypass_attr_specific, /* specific */
        H5VL_bypass_attr_optional, /* optional */
        H5VL_bypass_attr_close     /* close */
    },
    {
        /* dataset_cls */
        H5VL_bypass_dataset_create,   /* create */
        H5VL_bypass_dataset_open,     /* open */
        H5VL_bypass_dataset_read,     /* read */
        H5VL_bypass_dataset_write,    /* write */
        H5VL_bypass_dataset_get,      /* get */
        H5VL_bypass_dataset_specific, /* specific */
        H5VL_bypass_dataset_optional, /* optional */
        H5VL_bypass_dataset_close     /* close */
    },
    {
        /* datatype_cls */
        H5VL_bypass_datatype_commit,   /* commit */
        H5VL_bypass_datatype_open,     /* open */
        H5VL_bypass_datatype_get,      /* get_size */
        H5VL_bypass_datatype_specific, /* specific */
        H5VL_bypass_datatype_optional, /* optional */
        H5VL_bypass_datatype_close     /* close */
    },
    {
        /* file_cls */
        H5VL_bypass_file_create,   /* create */
        H5VL_bypass_file_open,     /* open */
        H5VL_bypass_file_get,      /* get */
        H5VL_bypass_file_specific, /* specific */
        H5VL_bypass_file_optional, /* optional */
        H5VL_bypass_file_close     /* close */
    },
    {
        /* group_cls */
        H5VL_bypass_group_create,   /* create */
        H5VL_bypass_group_open,     /* open */
        H5VL_bypass_group_get,      /* get */
        H5VL_bypass_group_specific, /* specific */
        H5VL_bypass_group_optional, /* optional */
        H5VL_bypass_group_close     /* close */
    },
    {
        /* link_cls */
        H5VL_bypass_link_create,   /* create */
        H5VL_bypass_link_copy,     /* copy */
        H5VL_bypass_link_move,     /* move */
        H5VL_bypass_link_get,      /* get */
        H5VL_bypass_link_specific, /* specific */
        H5VL_bypass_link_optional  /* optional */
    },
    {
        /* object_cls */
        H5VL_bypass_object_open,     /* open */
        H5VL_bypass_object_copy,     /* copy */
        H5VL_bypass_object_get,      /* get */
        H5VL_bypass_object_specific, /* specific */
        H5VL_bypass_object_optional  /* optional */
    },
    {
        /* introspect_cls */
        H5VL_bypass_introspect_get_conn_cls,  /* get_conn_cls */
        H5VL_bypass_introspect_get_cap_flags, /* get_cap_flags */
        H5VL_bypass_introspect_opt_query,     /* opt_query */
    },
    {
        /* request_cls */
        H5VL_bypass_request_wait,     /* wait */
        H5VL_bypass_request_notify,   /* notify */
        H5VL_bypass_request_cancel,   /* cancel */
        H5VL_bypass_request_specific, /* specific */
        H5VL_bypass_request_optional, /* optional */
        H5VL_bypass_request_free      /* free */
    },
    {
        /* blob_cls */
        H5VL_bypass_blob_put,      /* put */
        H5VL_bypass_blob_get,      /* get */
        H5VL_bypass_blob_specific, /* specific */
        H5VL_bypass_blob_optional  /* optional */
    },
    {
        /* token_cls */
        H5VL_bypass_token_cmp,     /* cmp */
        H5VL_bypass_token_to_str,  /* to_str */
        H5VL_bypass_token_from_str /* from_str */
    },
    H5VL_bypass_optional /* optional */
};

/* The connector identification number, initialized at runtime */
static hid_t H5VL_BYPASS_g = H5I_INVALID_HID;

/* Required shim routines, to enable dynamic loading of shared library */
/* The HDF5 library _must_ find routines with these names and signatures
 *      for a shared library that contains a VOL connector to be detected
 *      and loaded at runtime.
 */
H5PL_type_t
H5PLget_plugin_type(void)
{
    return H5PL_TYPE_VOL;
}
const void *
H5PLget_plugin_info(void)
{
    return &H5VL_bypass_g;
}

static void *start_thread_for_pool(void *args);

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_new_obj
 *
 * Purpose:     Create a new bypass object for an underlying object
 *
 * Return:      Success:    Pointer to the new bypass object
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Monday, December 3, 2018
 *
 *-------------------------------------------------------------------------
 */
static H5VL_bypass_t *
H5VL_bypass_new_obj(void *under_obj, hid_t under_vol_id)
{
    H5VL_bypass_t *new_obj = NULL;

    new_obj               = (H5VL_bypass_t *)calloc(1, sizeof(H5VL_bypass_t));
    new_obj->under_object = under_obj;
    new_obj->under_vol_id = under_vol_id;
    new_obj->type = H5I_BADID;

    if (H5Iinc_ref(new_obj->under_vol_id) < 0) {
        fprintf(stderr, "failed to increment ref count of underlying vol connector\n");
        goto error;
    }

    return new_obj;
error:
    if (new_obj)
        free(new_obj);

    return NULL;
} /* end H5VL_bypass_obj() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_free_obj
 *
 * Purpose:     Release a bypass object
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Monday, December 3, 2018
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_free_obj(H5VL_bypass_t *obj)
{
    hid_t err_id = H5I_INVALID_HID;
    herr_t ret_value = 0;
    assert(obj);

    pthread_mutex_lock(&mutex_local);

    err_id = H5Eget_current_stack();

    switch(obj->type) {
        case (H5I_DATASET): {
            if (H5Idec_ref(obj->under_vol_id) < 0) {
                fprintf(stderr, "failed to decrement reference count on underlying VOL connector\n");
                ret_value = -1;
                goto done;
            }

            if (release_dset_info(&obj->u.dataset) < 0) {
                fprintf(stderr, "failed to release dataset-specific bypass object\n");
                ret_value = -1;
                goto done;
            }

            free(obj);
            break;
        }

        case (H5I_FILE): {
            if (obj->u.file.ref_count <= 0) {
                fprintf(stderr, "file object has no references\n");
                ret_value = -1;
                goto done;
            }

            obj->u.file.ref_count--;

            /* Only release underlying info once ref count reaches zero */
            if (obj->u.file.ref_count == 0) {
                if (H5Idec_ref(obj->under_vol_id) < 0) {
                    fprintf(stderr, "failed to decrement reference count on underlying VOL connector\n");
                    ret_value = -1;
                    goto done;
                }

                if (release_file_info(&obj->u.file) < 0) {
                    fprintf(stderr, "failed to release file-specific bypass object\n");
                    /* Continue on failure in order to release memory*/
                    ret_value = -1;
                }

                free(obj);
            }

            break;
        }

        case (H5I_GROUP): {
            if (H5Idec_ref(obj->under_vol_id) < 0) {
                fprintf(stderr, "failed to decrement reference count on underlying VOL connector\n");
                ret_value = -1;
                goto done;
            }

            if (release_group_info(&obj->u.group) < 0) {
                fprintf(stderr, "failed to release group-specific bypass object\n");
                ret_value = -1;
                goto done;
            }

            free(obj);
            break;
        }

        default: {
            if (H5Idec_ref(obj->under_vol_id) < 0) {
                fprintf(stderr, "failed to decrement reference count on underlying VOL connector\n");
                ret_value = -1;
                goto done;
            }

            free(obj);
            break;
        }
    }

    H5Eset_current_stack(err_id);

done:
    pthread_mutex_unlock(&mutex_local);

    return ret_value;
} /* end H5VL_bypass_free_obj() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_register
 *
 * Purpose:     Register the bypass VOL connector and retrieve an ID
 *              for it.
 *
 * Return:      Success:    The ID for the bypass VOL connector
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, November 28, 2018
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5VL_bypass_register(void)
{
    /* Singleton register the bypass VOL connector ID */
    if (H5VL_BYPASS_g < 0)
        H5VL_BYPASS_g = H5VLregister_connector(&H5VL_bypass_g, H5P_DEFAULT);

    return H5VL_BYPASS_g;
} /* end H5VL_bypass_register() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_init
 *
 * Purpose:     Initialize this VOL connector, performing any necessary
 *              operations for the connector that will apply to all containers
 *              accessed with the connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_init(hid_t vipl_id)
{
    char *nthreads_str = NULL;
    char *nsteps_str   = NULL;
    char *nelmts_str   = NULL;
    char *no_tpool_str = NULL;
    pthread_mutexattr_t attr;
    int i;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INIT\n");
#endif

    /* Shut compiler up about unused parameter */
    (void)vipl_id;

    /* Memory allocation for some information structures */
    info_stuff = (info_t *)calloc(info_size, sizeof(info_t));

    /* Retrieve the number of threads for the thread pool from the user's input */
    nthreads_str = getenv("BYPASS_VOL_NTHREADS");

    if (nthreads_str)
        nthreads_tpool = atoi(nthreads_str);

    /* The minimal number of threads is 1 while the maximal is 32 */
    if (nthreads_tpool < NTHREADS_MIN)
        nthreads_tpool = NTHREADS_MIN;
    else if (nthreads_tpool > NTHREADS_MAX)
        nthreads_tpool = NTHREADS_MAX;

    /* Retrieve the number of steps for the thread pool from the user's input.
     * The thread pool accumulates the number of steps (jobs) in the queue before
     * signalling the threads to process them.
     */
    nsteps_str = getenv("BYPASS_VOL_NSTEPS");

    if (nsteps_str)
        nsteps_tpool = atoi(nsteps_str);

    /* The smallest step is 1 */
    if (nsteps_tpool < 1)
        nsteps_tpool = 1;

    /* Retrieve the maximal number of elements in data pieces to be read from the user's input */
    nelmts_str = getenv("BYPASS_VOL_MAX_NELMTS");

    if (nelmts_str)
        nelmts_max = atoll(nelmts_str);

    /* The maximal number of data elements to be read must be at least 1 */
    if (nelmts_max < 1)
        nelmts_max = 1;

    //printf("%s at line %d: nthreads_tpool = %d, nsteps_tpool = %d, nelmts_max = %lld, nelmts_str = %s\n", __func__,
    //    __LINE__, nthreads_tpool, nsteps_tpool, nelmts_max, nelmts_str);

    /* Retrieve the flag for not using the thread pool from the user's input */
    no_tpool_str = getenv("BYPASS_VOL_NO_TPOOL");

    if (no_tpool_str && !strcmp(no_tpool_str, "true"))
        no_tpool = true;

    /* Initialize the task queue for the thread pool */
    memset(&queue_for_tpool, 0, sizeof(task_queue_t));

    info_for_thread = malloc(nthreads_tpool * sizeof(info_for_thread_t));

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex_local, &attr);

    pthread_cond_init(&cond_local, NULL);

    /* Start threads for the thread pool to process the data */
    for (i = 0; i < nthreads_tpool; i++) {
        info_for_thread[i].thread_id = i; /* Remove info_for_thread and pass in the
                                             thread_id directly to pthread_create */

        if (pthread_create(&th[i], NULL, &start_thread_for_pool, &info_for_thread[i]) != 0)
            fprintf(stderr, "failed to create thread %d\n", i);
    }

    pthread_mutexattr_destroy(&attr);

    return 0;
} /* end H5VL_bypass_init() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_term
 *
 * Purpose:     Terminate this VOL connector, performing any necessary
 *              operations for the connector that release connector-wide
 *              resources (usually created / initialized with the 'init'
 *              callback).
 *
 * Return:      Success:    0
 *              Failure:    (Can't fail)
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_term(void)
{
    FILE *log_fp;
    int   i;
    void  *thread_ret = NULL;
    bool  locked = false;
    herr_t ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL TERM\n");
#endif

    /* Reset VOL ID */
    H5VL_BYPASS_g = H5I_INVALID_HID;

    /* Stop the thread pool */
    stop_tpool = true;

    /* If H5Dread isn't even called in the application, the thread pool is waiting
     * for this condition variable. This broadcast tells the thread pool to stop
     * waiting.
     */  
    if (pthread_mutex_lock(&mutex_local) < 0) {
        printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    locked = true;

    pthread_cond_broadcast(&cond_local);

    if (pthread_mutex_unlock(&mutex_local) < 0) {
        printf("In %s of %s at line %d: pthread_mutex_unlock failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    locked = false;

    /* Doesn't stop if an error happens */
    for (i = 0; i < nthreads_tpool; i++) {
        if (pthread_join(th[i], &thread_ret) != 0)
            fprintf(stderr, "failed to join thread %d\n", i);
        
        if (thread_ret != (void*) 0) {
            fprintf(stderr, "thread %d failed\n", i);
        }
    }

#ifdef TMP
    /* Open the log file and output the following info:
     * - file name
     * - dataset name
     * - dataset (for contiguous) or chunk (for chunked dataset) location in file
     * - data offset in file
     * - number of elements to be read
     * - data offset in memory
     */
    log_fp = fopen("info.log", "w");

    for (i = 0; i < info_count; i++) {
        /* The END_OF_READ flag is used for multiple H5Dread calls.  It indicates
         * the end of one call with the special symbols of '###\n' in the log file.
         */
        if (!info_stuff[i].end_of_read)
            fprintf(log_fp, "%s %s %" PRIuHADDR " %" PRIuHADDR " %" PRIuHADDR " %" PRIuHADDR"\n",
                    info_stuff[i].file_name, info_stuff[i].dset_name,
                    info_stuff[i].dset_loc, info_stuff[i].data_offset_file, info_stuff[i].nelmts,
                    info_stuff[i].data_offset_mem);
        else {
	    if (!no_tpool)
                fprintf(log_fp, "###\n");
        }
        // printf("%s: %d, i = %d, end_of_read = %d\n", __func__, __LINE__, i,
        // info_stuff[i].end_of_read);
    }

    if (no_tpool)
	fprintf(log_fp, "###\n");

    fclose(log_fp);
#endif

    /* pthread_join has been called, just destroy the queue directly */
    bypass_queue_destroy(&queue_for_tpool, true);

    if (info_stuff)
        free(info_stuff);
    if (info_for_thread)
        free(info_for_thread);

    /* Release thread resources */
    pthread_mutex_destroy(&mutex_local);
    pthread_cond_destroy(&cond_local);

done:
    if (locked)
        pthread_mutex_unlock(&mutex_local);

    return ret_value;
} /* end H5VL_bypass_term() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_info_copy
 *
 * Purpose:     Duplicate the connector's info object.
 *
 * Returns:     Success:    New connector info object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_bypass_info_copy(const void *_info)
{
    const H5VL_bypass_info_t *info = (const H5VL_bypass_info_t *)_info;
    H5VL_bypass_info_t       *new_info = NULL;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO Copy\n");
#endif

    /* Allocate new VOL info struct for the bypass connector */
    new_info = (H5VL_bypass_info_t *)calloc(1, sizeof(H5VL_bypass_info_t));

    /* Increment reference count on underlying VOL ID, and copy the VOL info */
    new_info->under_vol_id = info->under_vol_id;
    H5Iinc_ref(new_info->under_vol_id);
    if (info->under_vol_info)
        H5VLcopy_connector_info(new_info->under_vol_id, &(new_info->under_vol_info), info->under_vol_info);

    return new_info;
} /* end H5VL_bypass_info_copy() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_info_cmp
 *
 * Purpose:     Compare two of the connector's info objects, setting *cmp_value,
 *              following the same rules as strcmp().
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_info_cmp(int *cmp_value, const void *_info1, const void *_info2)
{
    const H5VL_bypass_info_t *info1 = (const H5VL_bypass_info_t *)_info1;
    const H5VL_bypass_info_t *info2 = (const H5VL_bypass_info_t *)_info2;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO Compare\n");
#endif

    /* Sanity checks */
    assert(info1);
    assert(info2);

    /* Initialize comparison value */
    *cmp_value = 0;

    /* Compare under VOL connector classes */
    H5VLcmp_connector_cls(cmp_value, info1->under_vol_id, info2->under_vol_id);
    if (*cmp_value != 0)
        return 0;

    /* Compare under VOL connector info objects */
    H5VLcmp_connector_info(cmp_value, info1->under_vol_id, info1->under_vol_info, info2->under_vol_info);
    if (*cmp_value != 0)
        return 0;

    return 0;
} /* end H5VL_bypass_info_cmp() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_info_free
 *
 * Purpose:     Release an info object for the connector.
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_info_free(void *_info)
{
    H5VL_bypass_info_t *info = (H5VL_bypass_info_t *)_info;
    hid_t               err_id = H5I_INVALID_HID;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO Free\n");
#endif

    err_id = H5Eget_current_stack();

    /* Release underlying VOL ID and info */
    if (info->under_vol_info)
        H5VLfree_connector_info(info->under_vol_id, info->under_vol_info);
    H5Idec_ref(info->under_vol_id);

    H5Eset_current_stack(err_id);

    /* Free bypass info object itself */
    free(info);

    return 0;
} /* end H5VL_bypass_info_free() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_info_to_str
 *
 * Purpose:     Serialize an info object for this connector into a string
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_info_to_str(const void *_info, char **str)
{
    const H5VL_bypass_info_t *info              = (const H5VL_bypass_info_t *)_info;
    H5VL_class_value_t        under_value       = (H5VL_class_value_t)-1;
    char                     *under_vol_string  = NULL;
    size_t                    under_vol_str_len = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO To String\n");
#endif

    /* Get value and string for underlying VOL connector */
    H5VLget_value(info->under_vol_id, &under_value);
    H5VLconnector_info_to_str(info->under_vol_info, info->under_vol_id, &under_vol_string);

    /* Determine length of underlying VOL info string */
    if (under_vol_string)
        under_vol_str_len = strlen(under_vol_string);

    /* Allocate space for our info */
    *str = (char *)H5allocate_memory(32 + under_vol_str_len, (hbool_t)0);
    assert(*str);

    /* Encode our info
     * Normally we'd use snprintf() here for a little extra safety, but that
     * call had problems on Windows until recently. So, to be as
     * platform-independent as we can, we're using sprintf() instead.
     */
    sprintf(*str, "under_vol=%u;under_info={%s}", (unsigned)under_value,
            (under_vol_string ? under_vol_string : ""));

    /* Release under VOL info string, if there is one */
    if (under_vol_string)
        H5free_memory(under_vol_string);

    return 0;
} /* end H5VL_bypass_info_to_str() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_str_to_info
 *
 * Purpose:     Deserialize a string into an info object for this connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_str_to_info(const char *str, void **_info)
{
    H5VL_bypass_info_t *info = NULL;
    unsigned            under_vol_value;
    const char         *under_vol_info_start, *under_vol_info_end;
    hid_t               under_vol_id = H5I_INVALID_HID;
    void               *under_vol_info = NULL;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INFO String To Info\n");
#endif

    /* Retrieve the underlying VOL connector value and info */
    sscanf(str, "under_vol=%u;", &under_vol_value);
    under_vol_id         = H5VLregister_connector_by_value((H5VL_class_value_t)under_vol_value, H5P_DEFAULT);
    under_vol_info_start = strchr(str, '{');
    under_vol_info_end   = strrchr(str, '}');
    assert(under_vol_info_end > under_vol_info_start);
    if (under_vol_info_end != (under_vol_info_start + 1)) {
        char *under_vol_info_str;

        under_vol_info_str = (char *)malloc((size_t)(under_vol_info_end - under_vol_info_start));
        memcpy(under_vol_info_str, under_vol_info_start + 1,
               (size_t)((under_vol_info_end - under_vol_info_start) - 1));
        *(under_vol_info_str + (under_vol_info_end - under_vol_info_start)) = '\0';

        H5VLconnector_str_to_info(under_vol_info_str, under_vol_id, &under_vol_info);

        free(under_vol_info_str);
    } /* end else */

    /* Allocate new bypass VOL connector info and set its fields */
    info                 = (H5VL_bypass_info_t *)calloc(1, sizeof(H5VL_bypass_info_t));
    info->under_vol_id   = under_vol_id;
    info->under_vol_info = under_vol_info;

    /* Set return value */
    *_info = info;

    return 0;
} /* end H5VL_bypass_str_to_info() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_get_object
 *
 * Purpose:     Retrieve the 'data' for a VOL object.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_bypass_get_object(const void *obj)
{
    const H5VL_bypass_t *o = (const H5VL_bypass_t *)obj;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL Get object\n");
#endif

    return H5VLget_object(o->under_object, o->under_vol_id);
} /* end H5VL_bypass_get_object() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_get_wrap_ctx
 *
 * Purpose:     Retrieve a "wrapper context" for an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_get_wrap_ctx(const void *obj, void **wrap_ctx)
{
    const H5VL_bypass_t    *o = (const H5VL_bypass_t *)obj;
    H5VL_bypass_wrap_ctx_t *new_wrap_ctx = NULL;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL WRAP CTX Get\n");
#endif

    /* Allocate new VOL object wrapping context for the bypass connector */
    new_wrap_ctx = (H5VL_bypass_wrap_ctx_t *)calloc(1, sizeof(H5VL_bypass_wrap_ctx_t));

    /* Increment reference count on underlying VOL ID, and copy the VOL info */
    new_wrap_ctx->under_vol_id = o->under_vol_id;
    H5Iinc_ref(new_wrap_ctx->under_vol_id);
    H5VLget_wrap_ctx(o->under_object, o->under_vol_id, &new_wrap_ctx->under_wrap_ctx);

    /* Set wrap context to return */
    *wrap_ctx = new_wrap_ctx;

    return 0;
} /* end H5VL_bypass_get_wrap_ctx() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_wrap_object
 *
 * Purpose:     Use a "wrapper context" to wrap a data object
 *
 * Return:      Success:    Pointer to wrapped object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_bypass_wrap_object(void *obj, H5I_type_t obj_type, void *_wrap_ctx)
{
    H5VL_bypass_wrap_ctx_t *wrap_ctx = (H5VL_bypass_wrap_ctx_t *)_wrap_ctx;
    H5VL_bypass_t          *new_obj = NULL;
    void                   *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL WRAP Object\n");
#endif

    /* Wrap the object with the underlying VOL */
    under = H5VLwrap_object(obj, obj_type, wrap_ctx->under_vol_id, wrap_ctx->under_wrap_ctx);
    if (under)
        new_obj = H5VL_bypass_new_obj(under, wrap_ctx->under_vol_id);
    else
        new_obj = NULL;

    return new_obj;
} /* end H5VL_bypass_wrap_object() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_unwrap_object
 *
 * Purpose:     Unwrap a wrapped object, discarding the wrapper, but returning
 *		underlying object.
 *
 * Return:      Success:    Pointer to unwrapped object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_bypass_unwrap_object(void *obj)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void          *under;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL UNWRAP Object\n");
#endif

    /* Unrap the object with the underlying VOL */
    under = H5VLunwrap_object(o->under_object, o->under_vol_id);

    if (under)
        H5VL_bypass_free_obj(o);

    return under;
} /* end H5VL_bypass_unwrap_object() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_free_wrap_ctx
 *
 * Purpose:     Release a "wrapper context" for an object
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_free_wrap_ctx(void *_wrap_ctx)
{
    H5VL_bypass_wrap_ctx_t *wrap_ctx = (H5VL_bypass_wrap_ctx_t *)_wrap_ctx;
    hid_t                   err_id = H5I_INVALID_HID;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL WRAP CTX Free\n");
#endif

    err_id = H5Eget_current_stack();

    /* Release underlying VOL ID and wrap context */
    if (wrap_ctx->under_wrap_ctx)
        H5VLfree_wrap_ctx(wrap_ctx->under_wrap_ctx, wrap_ctx->under_vol_id);
    H5Idec_ref(wrap_ctx->under_vol_id);

    H5Eset_current_stack(err_id);

    /* Free bypass wrap context object itself */
    free(wrap_ctx);

    return 0;
} /* end H5VL_bypass_free_wrap_ctx() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_create
 *
 * Purpose:     Creates an attribute on an object.
 *
 * Return:      Success:    Pointer to attribute object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id,
                        hid_t space_id, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *attr = NULL;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void          *under = NULL;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Create\n");
#endif

    under = H5VLattr_create(o->under_object, loc_params, o->under_vol_id, name, type_id, space_id, acpl_id,
                            aapl_id, dxpl_id, req);
    if (under) {
        attr = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if (req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        attr = NULL;

    return (void *)attr;
} /* end H5VL_bypass_attr_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_open
 *
 * Purpose:     Opens an attribute on an object.
 *
 * Return:      Success:    Pointer to attribute object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t aapl_id,
                      hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *attr = NULL;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void          *under = NULL;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Open\n");
#endif

    under = H5VLattr_open(o->under_object, loc_params, o->under_vol_id, name, aapl_id, dxpl_id, req);
    if (under) {
        attr = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if (req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        attr = NULL;

    return (void *)attr;
} /* end H5VL_bypass_attr_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_read
 *
 * Purpose:     Reads data from attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_read(void *attr, hid_t mem_type_id, void *buf, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)attr;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Read\n");
#endif

    ret_value = H5VLattr_read(o->under_object, o->under_vol_id, mem_type_id, buf, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_read() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_write
 *
 * Purpose:     Writes data to attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_write(void *attr, hid_t mem_type_id, const void *buf, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)attr;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Write\n");
#endif

    ret_value = H5VLattr_write(o->under_object, o->under_vol_id, mem_type_id, buf, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_write() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_get
 *
 * Purpose:     Gets information about an attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_get(void *obj, H5VL_attr_get_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Get\n");
#endif

    ret_value = H5VLattr_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_get() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_specific
 *
 * Purpose:     Specific operation on attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_attr_specific_args_t *args,
                          hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Specific\n");
#endif

    ret_value = H5VLattr_specific(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_optional
 *
 * Purpose:     Perform a connector-specific operation on an attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Optional\n");
#endif

    ret_value = H5VLattr_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_attr_optional() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_attr_close
 *
 * Purpose:     Closes an attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1, attr not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_attr_close(void *attr, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)attr;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL ATTRIBUTE Close\n");
#endif

    ret_value = H5VLattr_close(o->under_object, o->under_vol_id, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying attribute was closed */
    if (ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_attr_close() */

/* Retrieve the name of the file to which an object belong.  The OBJ_TYPE should
 * be the type of OBJ.  Valid types include H5I_FILE, H5I_GROUP, H5I_DATATYPE,
 * H5I_DATASET, or H5I_ATTR.  This function basically copies what H5Fget_name
 * does.
 */
static ssize_t
get_filename_helper(H5VL_bypass_t *obj, char *file_name, H5I_type_t obj_type, void **req)
{
    H5VL_file_get_args_t args;
    size_t               name_len  = 0; /* Length of file name */
    ssize_t              ret_value = -1;

    assert(obj);
    assert(obj->under_object);
    assert(file_name);

    args.op_type                     = H5VL_FILE_GET_NAME;
    args.args.get_name.type          = obj_type;
    args.args.get_name.buf_size      = BYPASS_NAME_SIZE_LONG;
    args.args.get_name.buf           = file_name;
    args.args.get_name.file_name_len = &name_len;

    if (H5VL_bypass_file_get(obj, &args, H5P_DEFAULT, req) < 0) {
        fprintf(stderr, "In %s of %s at line %d: H5VL_bypass_file_get failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    if (name_len == 0) {
        fprintf(stderr, "filename was unexpectedly empty\n");
        ret_value = -1;
        goto done;
    }

    if ((size_t)name_len != name_len) {
        fprintf(stderr, "filename length was too large\n");
        ret_value = -1;
        goto done;
    }

    ret_value = (ssize_t)name_len;

done:
    return ret_value;
} /* get_filename_helper */

static H5L_type_t
get_linkinfo_helper(void *obj, const char *name, void **req)
{
    H5VL_loc_params_t    loc_params;  /* Location parameters for object access */
    H5VL_link_get_args_t vol_cb_args; /* Arguments to VOL callback */
    H5L_info2_t          linfo;
    H5L_type_t           ret_value = H5L_TYPE_ERROR;

    /* Set up location struct */
    loc_params.type                         = H5VL_OBJECT_BY_NAME;
    loc_params.obj_type                     = H5I_FILE; // H5I_get_type(loc_id);
    loc_params.loc_data.loc_by_name.name    = name;
    loc_params.loc_data.loc_by_name.lapl_id = H5P_DEFAULT;

    /* Set up VOL callback arguments */
    vol_cb_args.op_type             = H5VL_LINK_GET_INFO;
    vol_cb_args.args.get_info.linfo = &linfo;

    /* Get the link information */
    // H5VL_bypass_link_get(void *obj, const H5VL_loc_params_t *loc_params,
    // H5VL_link_get_args_t *args, hid_t dxpl_id, void **req);
    if (H5VL_bypass_link_get(obj, &loc_params, &vol_cb_args, H5P_DATASET_XFER_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_get_link failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    ret_value = linfo.type;

done:
    return ret_value;
}

/* Get the necessary dataset information through the VOL layer */
static herr_t
dset_open_helper(H5VL_bypass_t *obj, hid_t dxpl_id, void **req) {
    herr_t ret_value = 0;
    H5VL_dataset_get_args_t get_args;
    Bypass_dataset_t *dset = NULL;

    assert(obj->type == H5I_DATASET);
    dset = &obj->u.dataset;

    /* Initialize values */
    dset->dcpl_id = H5I_INVALID_HID;
    dset->space_id = H5I_INVALID_HID;
    dset->num_filters = 0;
    dset->layout = H5D_LAYOUT_ERROR;
    dset->use_native = false;
    dset->use_native_checked = false;

    /* Retrieve dataset's DCPL, copied from H5Dget_create_plist */
    get_args.op_type               = H5VL_DATASET_GET_DCPL;
    get_args.args.get_dcpl.dcpl_id = H5I_INVALID_HID;

    if (H5VLdataset_get(obj->under_object, obj->under_vol_id, &get_args, dxpl_id, req) < 0) {
        fprintf(stderr, "unable to get opened dataset's DCPL\n");
        ret_value = -1;
        goto done;
    }

    dset->dcpl_id = get_args.args.get_dcpl.dcpl_id;

    /* Retrieve the dataset's datatype info */
    if (get_dtype_info(obj, dxpl_id, req) < 0) {
        fprintf(stderr, "unable to get dataset's datatype info\n");
        ret_value = -1;
        goto done;
    }

    /* Figure out the dataset's dataspace */
    get_args.op_type                 = H5VL_DATASET_GET_SPACE;
    get_args.args.get_space.space_id = H5I_INVALID_HID;

    /* Retrieve the dataset's dataspace ID */
    if (H5VLdataset_get(obj->under_object, obj->under_vol_id, &get_args, dxpl_id, req) < 0) {
        fprintf(stderr, "unable to get opened dataset's dataspace\n");
        ret_value = -1;
        goto done;
    }

    dset->space_id = get_args.args.get_space.space_id;

    if ((dset->num_filters = H5Pget_nfilters(dset->dcpl_id)) < 0) {
        fprintf(stderr, "unable to get opened dataset's number of filters\n");
        ret_value = -1;
        goto done;
    }

    /* Retrieve layout */
    if ((dset->layout = H5Pget_layout(dset->dcpl_id)) < 0) {
        fprintf(stderr, "unable to get dataset's layout\n");
        ret_value = -1;
        goto done;
    }

    if (dset->layout == H5D_LAYOUT_ERROR) {
        fprintf(stderr, "dataset has an invalid layout\n");
        ret_value = -1;
        goto done;
    }

done:
    if (ret_value < 0) {
        H5E_BEGIN_TRY {
        if (dset->dcpl_id > 0)
            H5Pclose(dset->dcpl_id);
        if (dset->space_id > 0)
            H5Sclose(dset->space_id);
        } H5E_END_TRY;
    }

    return ret_value;
} /* dset_open_helper */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_create
 *
 * Purpose:     Creates a dataset in a container
 *
 * Return:      Success:    Pointer to a dataset object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id,
                           hid_t type_id, hid_t space_id, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id,
                           void **req)
{
    H5VL_bypass_t *dset = NULL;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    H5VL_bypass_t *parent_file = NULL;
    void          *under = NULL;
    bool           req_created = false;
    bool           locked = false;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Create\n");
#endif

    if (pthread_mutex_lock(&mutex_local) < 0) {
        printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    locked = true;

    if ((under = H5VLdataset_create(o->under_object, loc_params, o->under_vol_id, name,
        lcpl_id, type_id, space_id, dcpl_id, dapl_id, dxpl_id, req)) == NULL) {

            fprintf(stderr, "In %s of %s at line %d: failed to create dataset in underlying connector\n",
                __func__, __FILE__, __LINE__);
            goto error;
    }

    if ((dset = H5VL_bypass_new_obj(under, o->under_vol_id)) == NULL) {
        fprintf(stderr, "failed to create bypass object\n");
        goto error;
    }

    dset->type = H5I_DATASET;

    /* Check the object type of the location and figure out the file to which this dataset belongs */
    if (o->type == H5I_FILE) {
        parent_file = o;
    } else if (o->type == H5I_GROUP) {
        parent_file = o->u.group.file;
    } else {
        fprintf(stderr, "invalid object type\n");
        goto error;
    }

    assert(parent_file->type == H5I_FILE);
    assert(parent_file->u.file.ref_count > 0);

    /* Increment the reference count of the file object */
    parent_file->u.file.ref_count++;
    dset->u.dataset.file = parent_file;

    if (dset_open_helper(dset, dxpl_id, req) < 0) {
        fprintf(stderr, "failed to get dataset info\n");
        goto error;
    }
    
    /* Check for async request */
    if (req && *req) {
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
        req_created = true;
    }

    if (locked && pthread_mutex_unlock(&mutex_local) != 0) {
        printf("In %s of %s at line %d: pthread_mutex_unlock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    return (void *)dset;

error:
    if (dset) {
        H5VL_bypass_free_obj(dset);

        if (req && *req && req_created)
            H5VL_bypass_free_obj(*req);
    }

    if (locked)
        pthread_mutex_unlock(&mutex_local);

    return NULL;
} /* end H5VL_bypass_dataset_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_open
 *
 * Purpose:     Opens a dataset in a container
 *
 * Return:      Success:    Pointer to a dataset object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t dapl_id,
                         hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *dset = NULL;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    H5VL_bypass_t *parent_file = NULL;
    void          *under = NULL;
    bool           req_created = false;
    bool           locked = false;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Open\n");
#endif

    if (pthread_mutex_lock(&mutex_local) < 0) {
        printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    locked = true;

    if ((under = H5VLdataset_open(o->under_object, loc_params, o->under_vol_id, name, dapl_id, dxpl_id, req)) == NULL) {
        fprintf(stderr, "unable to open dataset in underlying connector\n");
        goto error;
    }

    if ((dset = H5VL_bypass_new_obj(under, o->under_vol_id)) == NULL) {
        fprintf(stderr, "failed to create bypass object\n");
        goto error;
    }

    dset->type = H5I_DATASET;

    /* Check the object type of the location and figure out the file to which this dataset belongs */
    if (o->type == H5I_FILE) {
        parent_file = o;
    } else if (o->type == H5I_GROUP) {
        parent_file = o->u.group.file;
    } else {
        fprintf(stderr, "In %s of %s at line %d: invalid object type\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    assert(parent_file->type == H5I_FILE);
    assert(parent_file->u.file.ref_count > 0);

    /* Increment the reference count of the file object */
    parent_file->u.file.ref_count++;
    dset->u.dataset.file = parent_file;

    if (dset_open_helper(dset, dxpl_id, req) < 0) {
        fprintf(stderr, "failed to get dataset info\n");
        goto error;
    }

    /* Check for async request */
    if (req && *req) {
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
        req_created = true;
    }

    if (locked && pthread_mutex_unlock(&mutex_local) != 0) {
        printf("In %s of %s at line %d: pthread_mutex_unlock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    return (void *)dset;

error:
    if (under) {
        H5E_BEGIN_TRY {
            H5VLdataset_close(under, o->under_vol_id, dxpl_id, req);
        } H5E_END_TRY;
    }

    if (dset && H5VL_bypass_free_obj(dset) < 0)
            fprintf(stderr, "failed to clean up bypass dataset object after dset open failure\n");

    if (req && *req && req_created) {
        if (H5VL_bypass_free_obj(*req) < 0)
            fprintf(stderr, "failed to clean up bypass request object after dset open failure\n");
    }

    if (locked)
        pthread_mutex_unlock(&mutex_local);

    return NULL;
} /* end H5VL_bypass_dataset_open() */

static ssize_t
get_dset_name_helper(H5VL_bypass_t *dset, char *name, void **req)
{
    H5VL_object_get_args_t args;
    H5VL_loc_params_t      loc_params;
    size_t                 dset_name_len = 0; /* Length of file name */
    ssize_t                ret_value     = -1;

    /* Set location parameters */
    loc_params.type     = H5VL_OBJECT_BY_SELF;
    loc_params.obj_type = H5I_DATASET;

    /* Set up VOL callback arguments */
    args.op_type                = H5VL_OBJECT_GET_NAME;
    args.args.get_name.buf_size = 1024;
    args.args.get_name.buf      = name;
    args.args.get_name.name_len = &dset_name_len;

    if (H5VLobject_get(dset->under_object, &loc_params, dset->under_vol_id, &args, H5P_DATASET_XFER_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_object_get failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    if (dset_name_len == 0) {
        fprintf(stderr, "dataset name length is unexpectedly zero\n");
        ret_value = -1;
        goto done;
    }

    if ((ssize_t)dset_name_len != dset_name_len) {
        fprintf(stderr, "dataset name length is unexpectedly large\n");
        ret_value = -1;
        goto done;
    }

    ret_value = (ssize_t)dset_name_len;

done:
    return ret_value;
} /* get_dset_name_helper */

static herr_t
get_num_chunks_helper(H5VL_bypass_t *dset, hid_t file_space_id, hsize_t *nchunks, void **req)
{
    H5VL_optional_args_t                vol_cb_args;   /* Arguments to VOL callback */
    H5VL_native_dataset_optional_args_t dset_opt_args; /* Arguments for optional operation */
    herr_t                              ret_value = 0;

    if (NULL == dset) {
        printf("In %s of %s at line %d: dset parameter can't be a null pointer\n", __func__, __FILE__,
               __LINE__);
        ret_value = -1;
        goto done;
    }

    if (NULL == nchunks) {
        printf("In %s of %s at line %d: nchunks parameter can't be a null pointer\n", __func__, __FILE__,
               __LINE__);
        ret_value = -1;
        goto done;
    }

    /* Set up VOL callback arguments */
    dset_opt_args.get_num_chunks.space_id = file_space_id;
    dset_opt_args.get_num_chunks.nchunks  = nchunks;
    vol_cb_args.op_type                   = H5VL_NATIVE_DATASET_GET_NUM_CHUNKS;
    vol_cb_args.args                      = &dset_opt_args;

    /* Get the number of written chunks */
    if (H5VLdataset_optional(dset->under_object, dset->under_vol_id, &vol_cb_args, H5P_DEFAULT, req) < 0) {
        printf("In %s of %s at line %d: H5VL_bypass_dataset_optional failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

done:
    return ret_value;
} /* get_num_chunks_helper */

/* Figure out the data spaces in file and memory and make sure the selection is valid.
 * Below is the table borrowed from the reference manual entry for H5Dread:
 *
 * mem_space_id	file_space_id	Behavior
 *
 * valid ID	valid ID	mem_space_id specifies the memory dataspace and the
 * selection within it. file_space_id specifies the selection within the file
 * dataset's dataspace. H5S_ALL	valid ID	The file dataset's dataspace is
 * used for the memory dataspace and the selection specified with file_space_id
 * specifies the selection within it. The combination of the file dataset's
 * dataspace and the selection from file_space_id is used for memory also. valid
 * ID	H5S_ALL mem_space_id specifies the memory dataspace and the selection
 * within it. The selection within the file dataset's dataspace is set to the
 * "all" selection. H5S_ALL	H5S_ALL	        The file dataset's dataspace is
 * used for the memory dataspace and the selection within the memory dataspace
 * is set to the "all" selection. The selection within the file dataset's
 * dataspace is set to the "all" selection.
 */
static herr_t
check_dspaces_helper(hid_t dset_space_id, hid_t file_space_id, hid_t *file_space_id_copy, hid_t mem_space_id,
                     hid_t *mem_space_id_copy)
{
    herr_t ret_value = 0;
    htri_t select_valid = 0;

    /* Settle the file data space */
    if (H5S_ALL == file_space_id)
        /* Use the dataset's dataspace */
        *file_space_id_copy = dset_space_id;
    else
        /* Use the original data space passed in from H5Dread or H5Dread_multi */
        *file_space_id_copy = file_space_id;

    /* Settle the memory data space */
    if (H5S_ALL == mem_space_id)
        /* Use the file data space just settled above */
        *mem_space_id_copy = *file_space_id_copy;
    else
        /* Use the original data space passed in from H5Dread or H5Dread_multi */
        *mem_space_id_copy = mem_space_id;

    /* Make sure the selection + offset is within the extent of the dataspaces in file and memory */
    if ((select_valid = H5Sselect_valid(*file_space_id_copy)) < 0) {
        printf("In %s of %s at line %d: H5Sselect_valid failed for file space\n",
               __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    if (select_valid == 0) {
        printf("In %s of %s at line %d: file data space selection isn't valid\n",
               __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    if ((select_valid = H5Sselect_valid(*mem_space_id_copy)) < 0) {
        printf("In %s of %s at line %d: H5Sselect_valid failed for mem space\n",
               __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    if (select_valid == 0) {
        printf("In %s of %s at line %d: memory data space selection isn't valid\n",
               __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

done:
    return ret_value;
}

/* Break down into smaller sections if the data size is 2GB or bigger.  Need to change the type
 * of BUF to VOID to be more general */
static herr_t
read_big_data(int fd, int *buf, size_t size, off_t offset)
{
    herr_t ret_value = 0;

    while (size > 0) {
        int bytes_in   = 0;  /* # of bytes to read       */
        int bytes_read = -1; /* # of bytes actually read */

        /* Trying to read more bytes than the return type can handle is
         * undefined behavior in POSIX.
         */
        if (size > POSIX_MAX_IO_BYTES)
            bytes_in = POSIX_MAX_IO_BYTES;
        else
            bytes_in = (int) size;

        do {
            bytes_read = pread(fd, buf, bytes_in, offset);
            if (bytes_read > 0)
                offset += bytes_read;
            
            if (bytes_read == 0) {
                fprintf(stderr, "file read encountered EOF\n");
                ret_value = -1;
                goto done;
            }

            /* Error messages for unexpected values of errno */
            if (-1 == bytes_read) {
                switch (errno) {
                    case EAGAIN:
                    case EINTR:
                        break;
                    default:
                        fprintf(stderr, "pread failed with error: %s\n", strerror(errno));
                        ret_value = -1;
                        goto done;
                }
            }

        } while (-1 == bytes_read && EINTR == errno);



        if (bytes_read > 0) {
            size -= (size_t)bytes_read;
            buf = (int *)((char *)buf + bytes_read);
        }
    } /* end while */

done:
    return ret_value;
}

static void *
start_thread_for_pool(void *args)
{
    int thread_id = ((info_for_thread_t *)args)->thread_id;
    void    *ret_value = (void*) 0;
    Bypass_task_t **tasks = NULL; /* An array of tasks to be queued */
    int      local_count = 0;
    int      load_task_count = 0;
    int      i;

    // fprintf(stderr, "In start_thread_for_pool: %d\n", thread_id);

    if ((tasks = (Bypass_task_t **)malloc(nsteps_tpool * sizeof(Bypass_task_t*))) == NULL) {
        fprintf(stderr, "failed to allocate a squence of tasks\n");
        ret_value = (void*) -1;
        goto done;
    }

    /* stop_tpool is only turned on in H5VL_bypass_term when the application finishes */
    while (!stop_tpool) {
        if (pthread_mutex_lock(&mutex_local) != 0) {
            fprintf(stderr, "failed to lock mutex\n");
            ret_value = (void*) -1;
            goto done;
        }

	//fprintf(stderr, "\t%s: %d: thread %d before wait\n", __func__, __LINE__, thread_id);

	/* If no tasks are available to work on, just wait.  Not sure if stop_tpool is useful. */
        while (queue_for_tpool.tasks_in_queue == 0 && !stop_tpool) {
            pthread_cond_wait(&cond_local, &mutex_local);
        }

	//fprintf(stderr, "\t%s: %d: thread %d after wait\n", __func__, __LINE__, thread_id);

        /* tasks_in_queue can be smaller (LEFTOVER being passed into
         * submit_task() in process_vectors()) or larger (submit_task() keeps adding
         * more before they are processed) than nsteps_tpool. Choose the smaller
         * value */
        local_count = MIN(queue_for_tpool.tasks_in_queue, nsteps_tpool);

        for (i = 0; i < local_count; i++) {
            /* Get the task in queue */
            if ((tasks[i] = bypass_queue_pop(&queue_for_tpool, false)) == NULL) {
                fprintf(stderr, "failed to pop task from queue\n");
                ret_value = (void*) -1;
                goto done;
            }

            tasks[i]->file->u.file.num_reads++;
            tasks[i]->file->u.file.read_started = true;
        }

        if (pthread_mutex_unlock(&mutex_local) != 0) {
            fprintf(stderr, "failed to unlock mutex\n");
            ret_value = (void*) -1;
            goto done;
        }

	//fprintf(stderr, "\t%s: %d: thread %d before reading data, local_count = %d\n", __func__, __LINE__, thread_id, local_count);

	for (i = 0; i < local_count; i++) {
            if (read_big_data(tasks[i]->file->u.file.fd, tasks[i]->vec_buf, tasks[i]->size,
                          tasks[i]->addr) < 0)
            {
                fprintf(stderr, "read_big_data failed within file %s\n", tasks[i]->file->u.file.name);
                /* Return a failure code, but try to complete the rest of the read request.
                 * This is important to properly decrement the reference count/num_reads on the local file object */
                ret_value = (void *)-1;
            }

            if (pthread_mutex_lock(&mutex_local) != 0) {
                fprintf(stderr, "failed to lock mutex\n");
                ret_value = (void*) -1;
                goto done;
            }

            tasks[i]->file->u.file.num_reads--;

	    load_task_count = atomic_load(tasks[i]->task_count_ptr);

	    //fprintf(stderr, "\t%s: %d: thread %d, i = %d load_task_count = %d\n", __func__, __LINE__, thread_id, i, load_task_count);

	    /* Decrement the task count that this pointer points to */
	    atomic_fetch_sub(tasks[i]->task_count_ptr, 1);

	    load_task_count = atomic_load(tasks[i]->task_count_ptr);

	    /* When the task count drops to zerp, notify the invoking thread to finish the dataset
	     * read function.  Use the signal function instead of broadcast because it's notifying
	     * one thread only.
	     */
	    if (load_task_count == 0)
	        //pthread_cond_broadcast(tasks[i]->local_condition_ptr);
	        pthread_cond_signal(tasks[i]->local_condition_ptr);

	    //fprintf(stderr, "\t%s: %d: thread %d, i = %d, load_task_count = %d\n", __func__, __LINE__, thread_id, i, load_task_count);

            /* When there is no task left in the queue and all the reads finish for
             * the current file, signal the main process that this file can be closed.
             */
	    if ((queue_for_tpool.tasks_in_queue == 0) && tasks[i]->file->u.file.num_reads == 0) {
                /* There are currently no reads active on this file - it may be closed */
                tasks[i]->file->u.file.read_started = false;

                pthread_cond_broadcast(&(tasks[i]->file->u.file.close_ready));
            }

            if (pthread_mutex_unlock(&mutex_local) != 0) {
                fprintf(stderr, "failed to unlock mutex\n");
                ret_value = (void*) -1;
                goto done;
            }

            if (bypass_task_release(tasks[i]) < 0) {
                fprintf(stderr, "failed to release task\n");
                ret_value = (void*) -1;
                goto done;
            }

            tasks[i] = NULL;
        }
    }

done:
    /* Attempt to clean up queue memory that we took ownership of */
    if (ret_value < (void*) 0) {
        fprintf(stderr, "thread idx %d in pool failed\n", thread_id);

        for (i = 0; i < local_count; i++) {
            if (tasks[i] != NULL) {
                bypass_task_release(tasks[i]);
                tasks[i] = NULL;
            }
        }
    }

    free(tasks);

    return ret_value;
} /* end start_thread_for_pool() */

static herr_t
process_vectors(task_queue_t *task_queue, void *rbuf, sel_info_t *selection_info)
{
    hid_t      file_iter_id, mem_iter_id;
    size_t     file_seq_i, mem_seq_i, file_nseq, mem_nseq;
    hssize_t   hss_nelmts;
    size_t     nelmts; 
    size_t     seq_nelem;
    hsize_t    file_off[SEL_SEQ_LIST_LEN], mem_off[SEL_SEQ_LIST_LEN];
    size_t     file_len[SEL_SEQ_LIST_LEN], mem_len[SEL_SEQ_LIST_LEN];
    size_t     io_len;
    int        local_count_for_signal = 0;
    Bypass_task_t *task = NULL;
    haddr_t    task_addr   = HADDR_UNDEF;
    void       *task_buf    = NULL;
    herr_t     ret_value = 0;

    /* Contiguous is treated as a single chunk */
    if ((hss_nelmts = H5Sget_select_npoints(selection_info->file_space_id)) < 0) {
        fprintf(stderr, "H5Sget_select_npoints on filespace failed\n");
        ret_value = -1;
        goto done;
    }

    nelmts     = hss_nelmts;

    if ((hss_nelmts = H5Sget_select_npoints(selection_info->mem_space_id)) < 0) {
        fprintf(stderr, "H5Sget_select_npoints on memspace failed\n");
        ret_value = -1;
        goto done;
    }

    if (nelmts != hss_nelmts) {
        fprintf(stderr, "the number of selected elements in file (%ld) isn't equal to the number in memory (%" PRIdHSIZE")\n", nelmts,
               hss_nelmts);
        ret_value = -1;
        goto done;
    }

    if ((file_iter_id =
        H5Ssel_iter_create(selection_info->file_space_id, 
        selection_info->dtype_size, H5S_SEL_ITER_SHARE_WITH_DATASPACE)) < 0)
    {
        fprintf(stderr, "H5Ssel_iter_create on filespace failed\n");
        ret_value = -1;
        goto done;
    }

    if ((mem_iter_id =
        H5Ssel_iter_create(selection_info->mem_space_id, 
        selection_info->dtype_size, H5S_SEL_ITER_SHARE_WITH_DATASPACE)) < 0)
    {
        fprintf(stderr, "H5Ssel_iter_create on memspace failed\n");
        ret_value = -1;
        goto done;
    }

    /* Initialize values so sequence lists are retrieved on the first iteration */
    file_seq_i = mem_seq_i = SEL_SEQ_LIST_LEN;
    file_nseq = mem_nseq = 0;

    /* Loop until all elements are processed. The algorithm is copied from the
     * function H5FD__read_selection_translate in H5FDint.c, which is somewhat
     * similar to H5D__select_io in H5Dselect.c
     */
    while (file_seq_i < file_nseq || nelmts > 0) {
        if (file_seq_i == SEL_SEQ_LIST_LEN) {
            if (H5Ssel_iter_get_seq_list(file_iter_id, SEL_SEQ_LIST_LEN, SIZE_MAX, &file_nseq, &seq_nelem,
                                         file_off, file_len) < 0)
            {
                fprintf(stderr, "file sequence length retrieval failed\n");
                ret_value = -1;
                goto done;
            }

        if (file_nseq == 0) {
            fprintf(stderr, "no file sequences retrieved from iteration\n");
            ret_value = -1;
            goto done;
        }

	    nelmts -= seq_nelem;
	    file_seq_i = 0;
	}

        /* Fill/refill memory sequence list if necessary */
        if (mem_seq_i == SEL_SEQ_LIST_LEN) {
            if (H5Ssel_iter_get_seq_list(mem_iter_id, SEL_SEQ_LIST_LEN, SIZE_MAX, &mem_nseq, &seq_nelem,
                                         mem_off, mem_len) < 0)
            {
                fprintf(stderr, "memory sequence length retrieval failed\n");
                ret_value = -1;
                goto done;
            }

        if (mem_nseq == 0) {
            fprintf(stderr, "no memory sequences retrieved from iteration\n");
            ret_value = -1;
            goto done;
        }

	   mem_seq_i = 0;
	}

        /* Calculate length of this IO */
        io_len = MIN(file_len[file_seq_i], mem_len[mem_seq_i]);

        /* Make sure the data length isn't greater than user's input
         * (default to 1024 * 1024), mainly for contiguous datasets */
        io_len = MIN(io_len, nelmts_max);

	if (no_tpool) {
	    if ((task = malloc(sizeof(Bypass_task_t))) == NULL) {
		fprintf(stderr, "Failed to allocate memory for a task\n");
		ret_value = -1;
		goto done;
	    }

	    /* Populate task and append to queue */
	    task_addr = selection_info->chunk_addr + file_off[file_seq_i];
	    task_buf = (void *)((uint8_t *)rbuf + mem_off[mem_seq_i]);

	    if ((task = bypass_task_create(selection_info, task_addr, io_len, task_buf)) == NULL) {
		fprintf(stderr, "Failed to assemble task while processing vectors\n");
		ret_value = -1;
		goto done;
	    }

	    //printf("%s: %d: task_queue->tasks_in_queue = %d\n", __func__, __LINE__, task_queue->tasks_in_queue);

	    if (bypass_queue_push(task_queue, task, false) < 0) {
		fprintf(stderr, "Failed to push task to queue\n");
		ret_value = -1;
		goto done;
	    }
	} else {
	    /* Lock in order to append a task to the task queue */
	    if (pthread_mutex_lock(&mutex_local) != 0) {
		fprintf(stderr, "failed to lock local mutex\n");
		ret_value = -1;
		goto done;
	    }

	    if ((task = malloc(sizeof(Bypass_task_t))) == NULL) {
		fprintf(stderr, "Failed to allocate memory for a task\n");
		ret_value = -1;
		goto done;
	    }

	    /* Populate task and append to queue */
	    task_addr = selection_info->chunk_addr + file_off[file_seq_i];
	    task_buf = (void *)((uint8_t *)rbuf + mem_off[mem_seq_i]);

	    if ((task = bypass_task_create(selection_info, task_addr, io_len, task_buf)) == NULL) {
		fprintf(stderr, "Failed to assemble task while processing vectors\n");
		ret_value = -1;
		goto done;
	    }

	    if (bypass_queue_push(task_queue, task, false) < 0) {
		fprintf(stderr, "Failed to push task to queue\n");
		ret_value = -1;
		goto done;
	    }

	    local_count_for_signal++;

	    /* Let the queue accumulate nsteps_tpool entries then signal the thread pool
	     * to read them */
	    if (local_count_for_signal >= nsteps_tpool) {
		pthread_cond_broadcast(&cond_local);
		local_count_for_signal = 0;
	    }

	    if (pthread_mutex_unlock(&mutex_local) != 0) {
		fprintf(stderr, "failed to unlock local mutex\n");
		ret_value = -1;
		goto done;
	    }
	}

#ifdef TMP
        /* Save the info for the C log file */
        {
	    if (pthread_mutex_lock(&mutex_local) != 0) {
		fprintf(stderr, "failed to lock local mutex\n");
		ret_value = -1;
		goto done;
	    }

            /* Enlarge the size of the info for C and Re-allocate the memory if
             * necessary */
            if (info_count == info_size) {
                info_size *= 2;
                if ((info_stuff = (info_t *)realloc(info_stuff, info_size * sizeof(info_t))) == NULL) {
                    fprintf(stderr, "failed to reallocate info table\n");
                    ret_value = -1;
                    goto done;
                }
            }

            /* Save the info in the structure */
            strcpy(info_stuff[info_count].file_name, selection_info->file->u.file.name);
            strcpy(info_stuff[info_count].dset_name, selection_info->dset_name);
            info_stuff[info_count].dset_loc         = selection_info->chunk_addr;
            info_stuff[info_count].data_offset_file = file_off[file_seq_i] / selection_info->dtype_size;
            info_stuff[info_count].real_offset =
                info_stuff[info_count].dset_loc + info_stuff[info_count].data_offset_file;
            info_stuff[info_count].nelmts          = io_len / selection_info->dtype_size;
            info_stuff[info_count].data_offset_mem = mem_off[mem_seq_i] / selection_info->dtype_size;
            info_stuff[info_count].end_of_read     = false;

            // printf("%s: %d, info_count = %d, end_of_read = %d\n", __func__,
            // __LINE__, info_count, info_stuff[info_count].end_of_read);

            /* Increment the counter */
            info_count++;

	    if (pthread_mutex_unlock(&mutex_local) != 0) {
		fprintf(stderr, "failed to unlock local mutex\n");
		ret_value = -1;
		goto done;
	    }
	}
#endif

        /* Update file sequence */
        if (io_len == file_len[file_seq_i])
            file_seq_i++;
        else {
            file_off[file_seq_i] += io_len;
            file_len[file_seq_i] -= io_len;
        }

        /* Update memory sequence */
        if (io_len == mem_len[mem_seq_i])
            mem_seq_i++;
        else {
            mem_off[mem_seq_i] += io_len;
            mem_len[mem_seq_i] -= io_len;
        }
    }

    /* If there is any leftover entries in the queue, signal the thread pool to
     * read them.  The tasks have been enqueued earlier. */
    if (local_count_for_signal > 0 && local_count_for_signal < nsteps_tpool) {
        if (pthread_mutex_lock(&mutex_local) != 0) {
	    printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
	    ret_value = -1;
	    goto done;
        }

        pthread_cond_broadcast(&cond_local);

        if (pthread_mutex_unlock(&mutex_local) != 0) {
	    printf("In %s of %s at line %d: pthread_mutex_unlock failed\n", __func__, __FILE__, __LINE__);
	    ret_value = -1;
	    goto done;
        }
    }

    if (H5Ssel_iter_close(file_iter_id) < 0) {
        fprintf(stderr, "failed to close file sel iterator\n");
        ret_value = -1;
        goto done;
    }

    if (H5Ssel_iter_close(mem_iter_id) < 0) {
        fprintf(stderr, "failed to close mem sel iterator\n");
        ret_value = -1;
        goto done;
    }

done:
    return ret_value;
} /* end of process_vectors() */

static int
process_chunk_cb(const hsize_t *chunk_offsets, unsigned filter_mask,
    haddr_t chunk_addr, hsize_t chunk_size, void *op_data) {

    /* Set to H5_ITER_STOP in case of error here - see note in lib documentation on H5D_chunk_iter_op_t */
    herr_t ret_value = H5_ITER_CONT;
    chunk_cb_info_t *cb_info = (chunk_cb_info_t *)op_data;

    int select_npoints = 0;
    hid_t mem_selection_id = H5I_INVALID_HID;

    assert(cb_info);

    if (H5Sset_extent_simple(cb_info->file_space_copy, cb_info->dset_dim_rank, cb_info->dset_dims, NULL) < 0) {
        fprintf(stderr, "unable to set the extent of the file space\n");
        ret_value = H5_ITER_STOP;
        goto done;
    }

    /* Reset the file space copy to initial selection/extent */
    // TBD - This may only be necessary if the selection is a hyperslab
    if (H5Sselect_copy(cb_info->file_space_copy, cb_info->file_space) < 0) {
        fprintf(stderr, "unable to copy file space\n");
        ret_value = H5_ITER_STOP;
        goto done;
    }

    /* To calculate the intersection area in the next step, there must be a hyperslab selection to start
        * with. If there is no hyperslab selection in the file dataspace, select the whole dataspace. */
    if (H5S_SEL_HYPERSLABS != cb_info->select_type) {
        if (H5Sselect_hyperslab(cb_info->file_space_copy, H5S_SELECT_SET,
            ZERO_OFFSETS, NULL, cb_info->dset_dims, NULL) < 0) {

            printf("In %s of %s at line %d: H5Sselect_hyperslab failed\n", __func__, __FILE__, __LINE__);
            ret_value = H5_ITER_STOP;
            goto done;
        }
    }

    /* Get the intersection between file space selection and this chunk. In other words,
     * get the file space selection falling into this chunk. */
    if (H5Sselect_hyperslab(cb_info->file_space_copy, H5S_SELECT_AND, chunk_offsets, NULL, cb_info->chunk_dims, NULL) < 0) {
        printf("In %s of %s at line %d: H5Sselect_hyperslab failed\n", __func__, __FILE__, __LINE__);
        ret_value = H5_ITER_STOP;
        goto done;
    }

    if ((select_npoints = H5Sget_select_npoints(cb_info->file_space_copy)) < 0) {
        fprintf(stderr, "unable to get the number of points in file selection\n");
        ret_value = H5_ITER_STOP;
        goto done;
    }

    /* No selection overlap; move on to the next chunk */
    if (select_npoints == 0)
        goto done;

    /* Key function: get the data selection in memory which matches the file space selection falling
        * into this chunk */
    if ((mem_selection_id = H5Sselect_project_intersection(cb_info->file_space,
        cb_info->mem_space, cb_info->file_space_copy)) < 0) {
        fprintf(stderr, "unable to get projected dataspace intersection\n");
        ret_value = H5_ITER_STOP;
        goto done;
    }

    /* Move the file space selection in this chunk to upper-left corner and adjust (shrink) its extent
     * to the size of the chunk. In other words, the 'file_space_copy' that contains the data
     * selection in file which falls into the current chunk is adjusted from the size of the dataset
     * to the size of chunk which still contains the same data selection. 'chunk_addr' is the original
     * point for 'file_space_copy'.
     */
    if (H5Sselect_adjust(cb_info->file_space_copy, chunk_offsets) < 0) {
        printf("In %s of %s at line %d: H5Sselect_adjust failed\n", __func__, __FILE__, __LINE__);
        ret_value = H5_ITER_STOP;
        goto done;
    }

    if (H5Sset_extent_simple(cb_info->file_space_copy, cb_info->dset_dim_rank,
        cb_info->chunk_dims, cb_info->chunk_dims) < 0) {
        fprintf(stderr, "unable to set dataspace extent\n");
        ret_value = H5_ITER_STOP;
        goto done;
    }

    /* Save the information for this chunk */
    cb_info->selection_info->mem_space_id  = mem_selection_id;
    cb_info->selection_info->file_space_id = cb_info->file_space_copy;
    cb_info->selection_info->chunk_addr    = chunk_addr;

    /* Retrieve the pieces of data (vectors) from the chunk and put them into the memory */
    process_vectors(cb_info->task_queue, cb_info->rbuf, cb_info->selection_info);

done:
    /* Close the space ID for the memory selection */
    if (ret_value == H5_ITER_CONT) {
        if (mem_selection_id > 0 && H5Sclose(mem_selection_id) < 0) {
            fprintf(stderr, "unable to close memory selection\n");
            ret_value = H5_ITER_STOP;
        }
    } else {
        H5E_BEGIN_TRY {
            H5Sclose(mem_selection_id);
        } H5E_END_TRY;
    }

    return ret_value;
} /* process_chunk_cb */

static herr_t process_chunks(task_queue_t *task_queue, void *rbuf, void *dset, hid_t dcpl_id, hid_t dxpl_id,
               hid_t mem_space, hid_t file_space, sel_info_t *selection_info, void **req) {
    herr_t ret_value = 0;
    H5VL_bypass_t *dset_obj = (H5VL_bypass_t *)dset;
    H5VL_native_dataset_optional_args_t dset_opt_args;
    H5VL_optional_args_t opt_args;
    chunk_cb_info_t chunk_cb_info;

    assert(dset_obj);
    assert(dset_obj->under_object);

    if ((chunk_cb_info.dset_dim_rank = H5Sget_simple_extent_ndims(file_space)) < 0) {
        fprintf(stderr, "unable to get the file space rank of chunked dataset\n");
        ret_value = -1;
        goto done;
    }

    /* Get selection type */
    if ((chunk_cb_info.select_type = H5Sget_select_type(file_space)) < 0) {
        fprintf(stderr, "unable to get the selection type of file space\n");
        ret_value = -1;
        goto done;
    }

    /* Retrieve dataset dimensions */
    if (H5Sget_simple_extent_dims(file_space, chunk_cb_info.dset_dims, NULL) < 0) {
        fprintf(stderr, "failed to get dataset dimensions\n");
        ret_value = -1;
        goto done;
    }

    /* Retrieve chunk dimensions from DCPL */
    if (H5Pget_chunk(dcpl_id, chunk_cb_info.dset_dim_rank, chunk_cb_info.chunk_dims) < 0) {
        fprintf(stderr, "failed to get chunk dimensions from DCPL\n");
        ret_value = -1;
        goto done;
    }

    /* Create a temporary dataspace that will have its select/extent modified during each
     * chunk callback */
    if ((chunk_cb_info.file_space_copy = H5Scopy(file_space)) < 0) {
        fprintf(stderr, "failed to copy file space\n");
        ret_value = -1;
        goto done;
    }

    chunk_cb_info.file_space = file_space;
    chunk_cb_info.mem_space = mem_space;
    chunk_cb_info.selection_info = selection_info;
    chunk_cb_info.rbuf = rbuf;
    chunk_cb_info.task_queue = task_queue;

    dset_opt_args.chunk_iter.op = process_chunk_cb;
    dset_opt_args.chunk_iter.op_data = (void*)&chunk_cb_info;

    opt_args.args = (void*) &dset_opt_args;
    opt_args.op_type = H5VL_NATIVE_DATASET_CHUNK_ITER;

    if (H5VLdataset_optional(dset_obj->under_object, dset_obj->under_vol_id, &opt_args, dxpl_id, req) < 0) {
        fprintf(stderr, "failed to iterate over chunks for processing\n");
        ret_value = -1;
        goto done;
    }

done:
    if (H5Sclose(chunk_cb_info.file_space_copy) < 0) {
        fprintf(stderr, "failed to close file space copy\n");
        ret_value = -1;
        goto done;
    }

    return ret_value;
} /* end process_chunks() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_read
 *
 * Purpose:     Reads data elements from a dataset into a buffer.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_read(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[],
                         hid_t file_space_id[], hid_t plist_id, void *buf[], void **req)
{
    void        *o_arr[count]; /* Array of under objects */
    hid_t        under_vol_id = H5I_INVALID_HID; /* VOL ID for all objects */
    herr_t       ret_value   = 0;
    hid_t        file_space_id_copy = H5I_INVALID_HID;
    hid_t        mem_space_id_copy = H5I_INVALID_HID;
    H5VL_bypass_t *bypass_obj = NULL;
    Bypass_dataset_t *bypass_dset = NULL;
    sel_info_t   selection_info;
    int          j;
    bool         read_use_native   = false;
    H5S_sel_type mem_sel_type = H5S_SEL_ERROR;
    H5S_sel_type file_sel_type = H5S_SEL_ERROR;
    bool types_equal = false;
    bool must_block = false;
    bool locked = false;
    dtype_info_t mem_type_info;
    task_queue_t local_queue;
    H5D_space_status_t dset_space_status = H5D_SPACE_STATUS_ERROR;
    bool         has_global = false, acquired_global = false;
    unsigned int lock_count = 1;
    atomic_int   local_task_count = 0;
    int          load_task_count = 0;;
    pthread_cond_t  local_condition;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Read\n");
#endif

    pthread_cond_init(&local_condition, NULL);

    if (H5TShave_mutex(&has_global) < 0) {
        fprintf(stderr, "In %s of %s at line %d: H5TShave_mutex failed\n", __func__, __FILE__, __LINE__);
        ret_value = -1;
        goto done;
    }

    //fprintf(stderr, "In %s of %s at line %d: has_global = %d\n", __func__, __FILE__, __LINE__, has_global);

    /* Grab the global lock of the HDF5 library */
    if (!has_global) {
	/* TODO: Use a condition variable instead of this while loop */
        while (!acquired_global) {
            if (H5TSmutex_acquire(lock_count, &acquired_global) < 0) {
                fprintf(stderr, "In %s of %s at line %d: H5TSmutex_acquire failed\n", __func__, __FILE__, __LINE__);
                ret_value = -1;
                goto done;
	    }
        }
    }

    /* Loop through all datasets and process them individually */
    for (j = 0; j < count; j++) {
        /* Prevent information persisting between iterations */
        memset(&selection_info, 0, sizeof(sel_info_t));
        memset(&mem_type_info, 0, sizeof(dtype_info_t));
        read_use_native = false;
        mem_sel_type = H5S_SEL_ERROR;
        file_sel_type = H5S_SEL_ERROR;
        file_space_id_copy = H5I_INVALID_HID;
        mem_space_id_copy = H5I_INVALID_HID;

        bypass_obj = (H5VL_bypass_t*)dset[j];

        if (bypass_obj->type != H5I_DATASET) { 
            fprintf(stderr, "object provided for bypass dataset read is not a dataset\n");
            ret_value = -1;
            goto done;
        }

        bypass_dset = (Bypass_dataset_t*)&bypass_obj->u.dataset;
        // printf("\n%s: %d, count=%lu, dset_name = %s, file_name = %s, dtype_id = %llu, H5T_STD_REF_DSETREG =
        // %llu, H5T_NATIVE_INT = %llu, dcpl_id = %llu, H5I_INVALID_HID = %d\n", __func__, __LINE__, count,
        // dset_name, file_name, dset_dtype_id, H5T_STD_REF_DSETREG, H5T_NATIVE_INT, dcpl_id,
        // H5I_INVALID_HID);

        /* Let the native function handle datatype conversion.  Also check the flag for filters, virtual
         * dataset and reference datatype */
        if (!bypass_dset->use_native_checked && should_dset_use_native(bypass_dset) < 0) {
            fprintf(stderr, "failed to determine if native function should be used\n");
            ret_value = -1;
            goto done;
        }

        // fprintf(stderr, "%s at %d: file_name = %s\n", __func__, __LINE__, file_name);

        selection_info.file = bypass_dset->file;

        /* Check selection type */
        if (mem_space_id[j] == H5S_ALL) {
            mem_sel_type = H5S_SEL_ALL;
        } else if (mem_space_id[j] != H5S_BLOCK && mem_space_id[j] != H5S_PLIST &&
            (mem_sel_type = H5Sget_select_type(mem_space_id[j])) < 0) {
            fprintf(stderr, "failed to get selection type\n");
            ret_value = -1;
            goto done;
        }

        if (mem_sel_type == H5S_SEL_NONE)
            continue;

        if (file_space_id[j] == H5S_ALL) {
            file_sel_type = H5S_SEL_ALL;
        } else if (file_space_id[j] != H5S_BLOCK && file_space_id[j] != H5S_PLIST &&
            (file_sel_type = H5Sget_select_type(file_space_id[j])) < 0) {
            fprintf(stderr, "failed to get selection type\n");
            ret_value = -1;
            goto done;
        }

        if (file_sel_type == H5S_SEL_NONE)
            continue;

        if (get_dtype_info_helper(mem_type_id[j], &mem_type_info) < 0) {
            fprintf(stderr, "failed to get mem dtype info\n");
            ret_value = -1;
            goto done;
        }

        types_equal = bypass_types_equal(&bypass_dset->dtype_info, &mem_type_info);

        if ((dset_space_status = get_dset_space_status(dset[j], plist_id, req)) < 0) {
            fprintf(stderr, "failed to get dataset space status\n");
            ret_value = -1;
            goto done;
        }

        read_use_native = bypass_dset->use_native || !types_equal ||
            mem_sel_type == H5S_SEL_POINTS || file_sel_type == H5S_SEL_POINTS
            || dset_space_status != H5D_SPACE_STATUS_ALLOCATED || mem_space_id[j] == H5S_BLOCK
            || file_space_id[j] == H5S_BLOCK || mem_space_id[j] == H5S_PLIST || file_space_id[j] == H5S_PLIST;

        if (read_use_native) {

            /* Populate the array of under objects */
            under_vol_id = ((H5VL_bypass_t *)(dset[0]))->under_vol_id;

            o_arr[j] = ((H5VL_bypass_t *)(dset[j]))->under_object;
            assert(under_vol_id == ((H5VL_bypass_t *)(dset[j]))->under_vol_id);

            // printf("%s: %d, in bypass VOL, count = %lu\n", __func__, __LINE__, count);

            if ((ret_value =
                H5VLdataset_read(1, &(o_arr[j]),
                    under_vol_id, &(mem_type_id[j]), &(mem_space_id[j]),
                    &(file_space_id[j]), plist_id, &(buf[j]), req)) < 0) {

                    fprintf(stderr, "In %s of %s at line %d: H5VLdataset_read failed\n", __func__,
                            __FILE__, __LINE__);
                    goto done;
            }
        } else { /* Coming into Bypass VOL when no data conversion and filter */
            if (get_dset_location(dset[j], plist_id, req, &selection_info.chunk_addr) < 0) {
                fprintf(stderr, "failed to get file location of contiguous dataset\n");
                ret_value = -1;
                goto done;
            }

            /* Decide the dataspaces in memory and file */
            if (check_dspaces_helper(bypass_dset->space_id, file_space_id[j], &file_space_id_copy, mem_space_id[j],
                                     &mem_space_id_copy) < 0) {
                printf("In %s of %s at line %d: can't figure out the data space in file or memory\n",
                       __func__, __FILE__, __LINE__);
                ret_value = -1;
                goto done;
            }

            /* Reset for the next H5Dread */
            pthread_mutex_lock(&mutex_local);
            locked = true;

            /* Future optimization: Only flush when a write has been performed.
             * Currently, some tests (specifically H5TEST-objcopy, likely others) fail because the Bypass VOL
             * attempts to read a dataset under the assumption that all written data has been flushed to file,
             * when in reality it is still cached. At the low level, this manifested as pread() being passed
             * write requests that went out of bounds of the posix file.  By flushing before each file before
             * read, we make sure that all previous writes are reflected in the filesystem.
             */
            if (flush_containing_file((H5VL_bypass_t *)dset[j]) < 0) {
                fprintf(stderr, "failed to flush dataset\n");
                ret_value = -1;
                goto done;
            }

            /* At least one read is being done through the Bypass VOL.
             * We should block until all tasks are complete. */
            must_block = true;
            pthread_mutex_unlock(&mutex_local);
            locked = false;

            /* Initialize data selection info */
            if (get_dset_name_helper((H5VL_bypass_t *)(dset[j]), selection_info.dset_name, req) < 0) {
                fprintf(stderr, "failed to retrieve dataset name\n");
                ret_value = -1;
                goto done;
            }

            load_task_count = atomic_load(&local_task_count);
            //printf("%s: %d: load_task_count = %d\n", __func__, __LINE__, load_task_count);

            selection_info.dtype_size = bypass_dset->dtype_info.size;

	    /* This pointer keeps track of the number of tasks in the queue for the current thread */
	    selection_info.task_count_ptr = &local_task_count;

	    /* This pointer passes the local condition variable for the current thread to the thread pool */
	    selection_info.local_condition_ptr = &local_condition;

            if (H5D_CHUNKED == bypass_dset->layout) {
                /* Iterate through all chunks and map the data selection in each chunk to the memory.
                 * Put the selections into a queue for the thread pool to read the data */
                if (no_tpool) {
                    //printf("%s: %d, in bypass VOL\n", __func__, __LINE__);

                    /* Make sure no garbage in any field */
                    memset(&local_queue, 0, sizeof(task_queue_t));

                    process_chunks(&local_queue, buf[j], dset[j], bypass_dset->dcpl_id, plist_id, mem_space_id_copy, file_space_id_copy,
                                   &selection_info, req);
                } else {
                    process_chunks(&queue_for_tpool, buf[j], dset[j], bypass_dset->dcpl_id, plist_id, mem_space_id_copy, file_space_id_copy,
                                   &selection_info, req);
                }
            } else if (H5D_CONTIGUOUS == bypass_dset->layout) {
                selection_info.file_space_id = file_space_id_copy;
                selection_info.mem_space_id  = mem_space_id_copy;

                /* Handles the hyperslab selection and read the data */
                if (no_tpool) {
                    /* Make sure no garbage in any field */
                    memset(&local_queue, 0, sizeof(task_queue_t));

                    /* Each thread reads puts tasks into its own queue */
		    if (process_vectors(&local_queue, buf[j], &selection_info) < 0) {
			fprintf(stderr, "failed to insert vectors into queue\n");
			ret_value = -1;
			goto done;
		    }
                } else {
                    /* Use the global instance of task_queue_t for thread pool */
		    if (process_vectors(&queue_for_tpool, buf[j], &selection_info) < 0) {
			fprintf(stderr, "failed to insert vectors into queue\n");
			ret_value = -1;
			goto done;
		    }

                    load_task_count = atomic_load(&local_task_count);
	            //fprintf(stderr, "%s: %d: load_task_count = %d\n", __func__, __LINE__, load_task_count);

                }
            } else {
                fprintf(stderr, "unsupported dataset layout\n");
                ret_value = -1;
                goto done;
            }

            /* Let go the global lock of the HDF5 library */
	    if (acquired_global && H5TSmutex_release(&lock_count) != 0) {
		fprintf(stderr, "In %s of %s at line %d: H5TSmutex_release failed\n", __func__, __FILE__, __LINE__);
		ret_value = -1;
		goto done;
	    }

	    acquired_global = false;

            //printf("%s: %d\n", __func__, __LINE__);

            /* Each thread reads from its own task queue if 'BYPASS_VOL_NO_TPOOL' environment variable is set */
            if (no_tpool) {
                Bypass_task_t *task = NULL;

                while (local_queue.tasks_in_queue) {
		    if ((task = bypass_queue_pop(&local_queue, false)) == NULL) {
			fprintf(stderr, "failed to pop task from queue\n");
			ret_value = -1;
			goto done;
		    }

		    if (read_big_data(task->file->u.file.fd, task->vec_buf, task->size, task->addr) < 0) {
			fprintf(stderr, "read_big_data failed within file %s\n", task->file->u.file.name);
			/* Return a failure code, but try to complete the rest of the read request.
			 * This is important to properly decrement the reference count/num_reads on the local file object */
			ret_value = -1;
		    }

		    if (task != NULL) {
			bypass_task_release(task);
			task = NULL;
		    }
                }
            }

#ifdef TMP
            /* Save the info for the C log file */
            {
		if (pthread_mutex_lock(&mutex_local) != 0) {
		    fprintf(stderr, "failed to lock local mutex\n");
		    ret_value = -1;
		    goto done;
		}

                /* Enlarge the size of the info for C and Re-allocate the memory if necessary */
                if (info_count == info_size) {
                    info_size  *= 2;
                    info_stuff = (info_t *)realloc(info_stuff, info_size * sizeof(info_t));
                }

                /* Save the info in the structure */
                info_stuff[info_count].end_of_read = true;

                //printf("%s: %d, info_count = %d, end_of_read = %d\n", __func__, __LINE__, info_count, info_stuff[info_count].end_of_read);

                /* Increment the counter */
                info_count++;

		if (pthread_mutex_unlock(&mutex_local) != 0) {
		    fprintf(stderr, "failed to unlock local mutex\n");
		    ret_value = -1;
		    goto done;
		}
            }
#endif

        }

    }

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    if (!no_tpool) {
	/* Do not return until the thread pool finishes the read */
	/* TBD: Enforcing this will become more complicated once multiple
	 * application threads making concurrent H5Dread() calls is supported. */
	if (pthread_mutex_lock(&mutex_local) < 0) {
	    printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
	    ret_value = -1;
	    goto done;
	}

	locked = true;

	/* Only finish H5Dread() once all threads are inactive and no tasks are undone */
	/* Only block here if Bypass VOL was used for the read */
	if (must_block) {
	    load_task_count = atomic_load(&local_task_count);
	    //fprintf(stderr, "%s: %d: load_task_count = %d\n", __func__, __LINE__, load_task_count);

	    while (load_task_count > 0) {
		pthread_cond_wait(&local_condition, &mutex_local);
		load_task_count = atomic_load(&local_task_count);
	        //fprintf(stderr, "%s: %d: load_task_count = %d\n", __func__, __LINE__, load_task_count);
            }
	}

	//fprintf(stderr, "%s: %d: load_task_count = %d\n", __func__, __LINE__, load_task_count);

	if (pthread_mutex_unlock(&mutex_local) < 0) {
	    printf("In %s of %s at line %d: pthread_mutex_unlock failed\n", __func__, __FILE__, __LINE__);
	    ret_value = -1;
	    goto done;
	}

	locked = false;
    }

    pthread_cond_destroy(&local_condition);

    //fprintf(stderr, "%s: %d\n", __func__, __LINE__);

done:
    if (locked)
        pthread_mutex_unlock(&mutex_local);

    /* Let go the global lock of the HDF5 library */
    if (acquired_global && H5TSmutex_release(&lock_count) != 0) {
	fprintf(stderr, "In %s of %s at line %d: H5TSmutex_release failed\n", __func__, __FILE__, __LINE__);
	ret_value = -1;
    }

    acquired_global = false;

    return ret_value;
} /* end H5VL_bypass_dataset_read() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_write
 *
 * Purpose:     Writes data elements from a buffer into a dataset.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_write(size_t count, void *dset[], hid_t mem_type_id[], hid_t mem_space_id[],
                          hid_t file_space_id[], hid_t plist_id, const void *buf[], void **req)
{
    void  *o_arr[count]; /* Array of under objects */
    hid_t  under_vol_id; /* VOL ID for all objects */
    herr_t ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Write\n");
#endif

    /* Populate the array of under objects */
    under_vol_id = ((H5VL_bypass_t *)(dset[0]))->under_vol_id;
    for (size_t u = 0; u < count; u++) {
        o_arr[u] = ((H5VL_bypass_t *)(dset[u]))->under_object;
        assert(under_vol_id == ((H5VL_bypass_t *)(dset[u]))->under_vol_id);
    }

    ret_value = H5VLdataset_write(count, o_arr, under_vol_id, mem_type_id, mem_space_id, file_space_id,
                                  plist_id, buf, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_dataset_write() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_get
 *
 * Purpose:     Gets information about a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_get(void *dset, H5VL_dataset_get_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)dset;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Get\n");
#endif

    ret_value = H5VLdataset_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_dataset_get() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_specific
 *
 * Purpose:     Specific operation on a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_specific(void *obj, H5VL_dataset_specific_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    hid_t under_vol_id;
    herr_t ret_value = 0;
    bool req_created = false;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL H5Dspecific\n");
#endif
    assert(o->type == H5I_DATASET);

    // Save copy of underlying VOL connector ID and prov helper, in case of
    // refresh destroying the current object
    under_vol_id = o->under_vol_id;

    if (H5VLdataset_specific(o->under_object, o->under_vol_id, args, dxpl_id, req) < 0) {
        fprintf(stderr, "H5VLdataset_specific failed\n");
        ret_value = -1;
        goto done;
    }

    /* Check for async request */
    if (req && *req) {
        *req = H5VL_bypass_new_obj(*req, under_vol_id);
        req_created = true;
    }

    /* If dataspace was changed, update the stored dataspace */
    if (args->op_type == H5VL_DATASET_SET_EXTENT) {
        H5VL_dataset_get_args_t get_args;        

        if (H5Sclose(o->u.dataset.space_id) < 0) {
            fprintf(stderr, "unable to close old dataspace\n");
            ret_value = -1;
            goto done;
        }
        
        o->u.dataset.space_id = H5I_INVALID_HID;

        /* Figure out the dataset's dataspace */
        get_args.op_type                 = H5VL_DATASET_GET_SPACE;
        get_args.args.get_space.space_id = H5I_INVALID_HID;

        /* Retrieve the dataset's dataspace ID */
        if (H5VLdataset_get(o->under_object, o->under_vol_id, &get_args, dxpl_id, req) < 0) {
            fprintf(stderr, "unable to get opened dataset's dataspace\n");
            ret_value = -1;
            goto done;
        }

        if (get_args.args.get_space.space_id == H5I_INVALID_HID) {
            fprintf(stderr, "retrieved invalid dataspace for dataset\n");
            ret_value = -1;
            goto done;
        }

        o->u.dataset.space_id = get_args.args.get_space.space_id;
    }

done:
    if (ret_value < 0) {
        if (req_created)
            H5VL_bypass_free_obj(*req);
    }

    return ret_value;
} /* end H5VL_bypass_dataset_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_optional
 *
 * Purpose:     Perform a connector-specific operation on a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Optional\n");
#endif

    ret_value = H5VLdataset_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_dataset_optional() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_dataset_close
 *
 * Purpose:     Closes a dataset.
 *
 * Return:      Success:    0
 *              Failure:    -1, dataset not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_dataset_close(void *dset, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)dset;
    herr_t         ret_value = 0;

    
#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATASET Close\n");
#endif

    /* Avoid assertion here because the dataset may not be opened with the dataset functions
    assert(o->type == H5I_DATASET);
    assert(o->u.dataset.file);
    assert(o->u.dataset.file->u.file.ref_count > 0);
    */

    if (H5VLdataset_close(o->under_object, o->under_vol_id, dxpl_id, req) < 0) {
        fprintf(stderr, "Failed to close dataset in underlying connectors\n");
        ret_value = -1;
        goto done;
    }

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying dataset was closed */
    if (ret_value >= 0)
        H5VL_bypass_free_obj(o);

done:
    return ret_value;
} /* end H5VL_bypass_dataset_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_commit
 *
 * Purpose:     Commits a datatype inside a container.
 *
 * Return:      Success:    Pointer to datatype object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id,
                            hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *dt = NULL;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void          *under = NULL;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Commit\n");
#endif

    under = H5VLdatatype_commit(o->under_object, loc_params, o->under_vol_id, name, type_id, lcpl_id, tcpl_id,
                                tapl_id, dxpl_id, req);
    if (under) {
        dt = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if (req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        dt = NULL;

    return (void *)dt;
} /* end H5VL_bypass_datatype_commit() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_open
 *
 * Purpose:     Opens a named datatype inside a container.
 *
 * Return:      Success:    Pointer to datatype object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t tapl_id,
                          hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *dt = NULL;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void          *under = NULL;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Open\n");
#endif

    under = H5VLdatatype_open(o->under_object, loc_params, o->under_vol_id, name, tapl_id, dxpl_id, req);
    if (under) {
        dt = H5VL_bypass_new_obj(under, o->under_vol_id);

        /* Check for async request */
        if (req && *req)
            *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
    } /* end if */
    else
        dt = NULL;

    return (void *)dt;
} /* end H5VL_bypass_datatype_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_get
 *
 * Purpose:     Get information about a datatype
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_datatype_get(void *dt, H5VL_datatype_get_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)dt;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Get\n");
#endif

    ret_value = H5VLdatatype_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_datatype_get() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_specific
 *
 * Purpose:     Specific operations for datatypes
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_datatype_specific(void *obj, H5VL_datatype_specific_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    hid_t          under_vol_id = H5I_INVALID_HID;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Specific\n");
#endif

    // Save copy of underlying VOL connector ID and prov helper, in case of
    // refresh destroying the current object
    under_vol_id = o->under_vol_id;

    ret_value = H5VLdatatype_specific(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_datatype_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_optional
 *
 * Purpose:     Perform a connector-specific operation on a datatype
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_datatype_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Optional\n");
#endif

    ret_value = H5VLdatatype_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_datatype_optional() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_datatype_close
 *
 * Purpose:     Closes a datatype.
 *
 * Return:      Success:    0
 *              Failure:    -1, datatype not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_datatype_close(void *dt, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)dt;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL DATATYPE Close\n");
#endif

    assert(o->under_object);

    ret_value = H5VLdatatype_close(o->under_object, o->under_vol_id, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying datatype was closed */
    if (ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_datatype_close() */

static herr_t
c_file_open_helper(H5VL_bypass_t *obj, const char *name)
{
    herr_t ret_value = 0;
    Bypass_file_t *file = NULL;
    struct rlimit limit;

    assert(obj);
    assert(obj->type == H5I_FILE);
    assert(name);

    file = &obj->u.file;

    /* Open the file with the system's function */
    if ((file->fd = open(name, O_RDONLY)) < 0) {
        fprintf(stderr, "failed to open file descriptor: %s\n", strerror(errno));

        /* If the number of files being opened exceeds the system limit, print out the corresponding error message */
	if (errno == EMFILE) {
	    if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
	        printf("Maximal number of files exceeded. Use the shell command 'ulimit -n' to check (current limit is %ld).\n", (long)limit.rlim_cur);
                printf("Either set this limit to a higher number or reduce the number of files to be opened\n");
            } else
                printf("In %s of %s at line %d: getrlimit() failed\n", __func__, __FILE__, __LINE__);
	}

        ret_value = -1;
        goto done;
    }

    /* Initialize the reference count for this file */
    file->ref_count = 1;

    strcpy(file->name, name);

    file->num_reads    = 0;
    file->read_started = false;

    /* Initialize the condition variable for file closing. */
    pthread_cond_init(&(file->close_ready), NULL);

done:
    return ret_value;
} /* c_file_open_helper */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_create
 *
 * Purpose:     Creates a container using this connector
 *
 * Return:      Success:    Pointer to a file object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id,
                        void **req)
{
    H5VL_bypass_info_t *info = NULL;
    H5VL_bypass_t      *file = NULL;
    hid_t               under_fapl_id = H5I_INVALID_HID;
    void               *under = NULL;
    bool                locked = false;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Create\n");
#endif

    if (pthread_mutex_lock(&mutex_local) < 0) {
        printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    locked = true;

    /* Get copy of our VOL info from FAPL */
    if (H5Pget_vol_info(fapl_id, (void **)&info) < 0) {
        fprintf(stderr, "error while getting VOL info from FAPL\n");
        goto error;
    }

    /* Make sure we have info about the underlying VOL to be used */
    if (!info) {
        fprintf(stderr, "VOL info not found in FAPL\n");
        goto error;
    }

    /* Copy the FAPL */
    if ((under_fapl_id = H5Pcopy(fapl_id)) < 0) {
        fprintf(stderr, "error while copying FAPL\n");
        goto error;
    }

    /* Set the VOL ID and info for the underlying FAPL */
    if (H5Pset_vol(under_fapl_id, info->under_vol_id, info->under_vol_info) < 0) {
        fprintf(stderr, "error while setting VOL info in FAPL\n");
        goto error;
    }

    /* Open the file with the underlying VOL connector */
    if ((under = H5VLfile_create(name, flags, fcpl_id, under_fapl_id, dxpl_id, req)) == NULL) {
        fprintf(stderr, "error while opening file with underlying VOL\n");
        goto error;
    }

    if ((file = H5VL_bypass_new_obj(under, info->under_vol_id)) == NULL) {
        fprintf(stderr, "error while creating bypass file object\n");
        goto error;
    }

    file->type = H5I_FILE;

    /* Check for async request */
    if (req && *req)
        if ((*req = H5VL_bypass_new_obj(*req, info->under_vol_id)) == NULL) {
            fprintf(stderr, "error while creating bypass async request\n");
            goto error;
        }

    /* Close underlying FAPL */
    if (H5Pclose(under_fapl_id) < 0) {
        fprintf(stderr, "error while closing underlying FAPL\n");
        goto error;
    }

    under_fapl_id = H5I_INVALID_HID;

    /* Release copy of our VOL info */
    if (H5VL_bypass_info_free(info) < 0) {
        fprintf(stderr, "error while releasing VOL info\n");
        goto error;
    }
    
    info = NULL;

    /* Open the C file and set the fields for the file_t structure */
    if (c_file_open_helper(file, name) < 0) {
        fprintf(stderr, "error while opening c file\n");
        goto error;
    }

    if (locked && pthread_mutex_unlock(&mutex_local) != 0) {
        printf("In %s of %s at line %d: pthread_mutex_unlock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    return (void *)file;

error:
    H5E_BEGIN_TRY {
        if (under)
            H5VLfile_close(under, under_fapl_id, dxpl_id, req);
        if (file)
            H5VL_bypass_free_obj(file);
        if (under_fapl_id > 0)
            H5Pclose(under_fapl_id);
        if (info)
            H5VLfree_connector_info(info->under_vol_id, info);
    } H5E_END_TRY;

    if (locked)
        pthread_mutex_unlock(&mutex_local);

    return NULL;

} /* end H5VL_bypass_file_create() */

#ifdef TMP
static void
get_vfd_handle_helper(H5VL_bypass_t *obj, void **file_handle, void **req)
{
    H5VL_optional_args_t             vol_cb_args;   /* Arguments to VOL callback */
    H5VL_native_file_optional_args_t file_opt_args; /* Arguments for optional operation */

    /* Set up VOL callback arguments */
    file_opt_args.get_vfd_handle.fapl_id     = H5P_DEFAULT;
    file_opt_args.get_vfd_handle.file_handle = file_handle;
    vol_cb_args.op_type                      = H5VL_NATIVE_FILE_GET_VFD_HANDLE;
    vol_cb_args.args                         = &file_opt_args;

    if (H5VL_bypass_file_optional(obj, &vol_cb_args, H5P_DEFAULT, req) < 0)
        puts("unable to get VFD file handle");
}
#endif

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_open
 *
 * Purpose:     Opens a container created with this connector
 *
 * Return:      Success:    Pointer to a file object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_info_t *info = NULL;
    H5VL_bypass_t      *file = NULL;
    hid_t               under_fapl_id = H5I_INVALID_HID;
    void               *under = NULL;
    bool                req_created = false;
    bool                locked = false;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Open\n");
#endif

    if (pthread_mutex_lock(&mutex_local) < 0) {
        printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    locked = true;

    /* Get copy of our VOL info from FAPL */
    if (H5Pget_vol_info(fapl_id, (void **)&info) < 0) {
        fprintf(stderr, "unable to retrieve vol info\n");
        goto error;
    }

    /* Make sure we have info about the underlying VOL to be used */
    if (!info) {
        fprintf(stderr, "retrieved VOL info was empty\n");
        goto error;
    }

    /* Copy the FAPL */
    if ((under_fapl_id = H5Pcopy(fapl_id)) < 0) {
        fprintf(stderr, "unable to copy FAPL\n");
        goto error;
    }

    /* Set the VOL ID and info for the underlying FAPL */
    if (H5Pset_vol(under_fapl_id, info->under_vol_id, info->under_vol_info) < 0) {
        fprintf(stderr, "unable to set VOL info in FAPL\n");
        goto error;
    }

    /* Open the file with the underlying VOL connector */
    if ((under = H5VLfile_open(name, flags, under_fapl_id, dxpl_id, req)) == NULL) {
        fprintf(stderr, "error while openning file with underlying VOL\n");
        goto error;
    }

    if ((file = H5VL_bypass_new_obj(under, info->under_vol_id)) == NULL) {
	fprintf(stderr, "unable to create bypass file object\n");
	goto error;
    }

    /* Check for async request */
    if (req && *req) {
	*req = H5VL_bypass_new_obj(*req, info->under_vol_id);
	req_created = true;
    }

    /* Close underlying FAPL */
    if (H5Pclose(under_fapl_id) < 0) {
        fprintf(stderr, "unable to closed underlying FAPL\n");
        goto error;
    }

    under_fapl_id = H5I_INVALID_HID;

    /* Release copy of our VOL info */
    if (H5VL_bypass_info_free(info) < 0) {
        fprintf(stderr, "unable to free underlying VOL info\n");
        goto error;
    }

    info = NULL;

    file->type = H5I_FILE;

    /* Open the C file and set the fields for the file_t structure */
    if (c_file_open_helper(file, name) < 0) {
        fprintf(stderr, "unable to open c file\n");
        goto error;
    }

    if (locked)
        pthread_mutex_unlock(&mutex_local);

    return (void *)file;

error:
    H5E_BEGIN_TRY {
        if (under_fapl_id != H5I_INVALID_HID)
            H5Pclose(under_fapl_id);
        if (under)
            H5VLfile_close(under, under_fapl_id, dxpl_id, req);
        if (file)
            H5VL_bypass_free_obj(file);
        if (req && *req && req_created)
            H5VL_bypass_free_obj(*req);
    } H5E_END_TRY;

    if (locked)
        pthread_mutex_unlock(&mutex_local);

    return NULL;
} /* end H5VL_bypass_file_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_get
 *
 * Purpose:     Get info about a file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_file_get(void *file, H5VL_file_get_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)file;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Get\n");
#endif
    assert(o->under_object);

    if (H5VLfile_get(o->under_object, o->under_vol_id, args, dxpl_id, req) < 0) {
        fprintf(stderr, "unable to get file info\n");
        ret_value = -1;
        goto done;
    }

    /* Check for async request */
    if (req && *req) {
        if ((*req = H5VL_bypass_new_obj(*req, o->under_vol_id)) == NULL) {
            fprintf(stderr, "unable to create bypass file object\n");
            ret_value = -1;
            goto done;
        }
    }

done:
    return ret_value;
} /* end H5VL_bypass_file_get() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_specific
 *
 * Purpose:     Specific operation on file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_file_specific(void *file, H5VL_file_specific_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t             *o = (H5VL_bypass_t *)file;
    H5VL_bypass_t             *new_o;
    H5VL_file_specific_args_t  my_args;
    H5VL_file_specific_args_t *new_args;
    H5VL_bypass_info_t        *info;
    hid_t                      under_vol_id = -1;
    herr_t                     ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Specific\n");
#endif

    assert(o == NULL || o->type == H5I_FILE);

    /* Check for 'is accessible' operation */
    if (args->op_type == H5VL_FILE_IS_ACCESSIBLE) {
        /* Make a (shallow) copy of the arguments */
        memcpy(&my_args, args, sizeof(my_args));

        /* Set up the new FAPL for the updated arguments */

        /* Get copy of our VOL info from FAPL */
        H5Pget_vol_info(args->args.is_accessible.fapl_id, (void **)&info);

        /* Make sure we have info about the underlying VOL to be used */
        if (!info)
            return (-1);

        /* Keep the correct underlying VOL ID for later */
        under_vol_id = info->under_vol_id;

        /* Copy the FAPL */
        my_args.args.is_accessible.fapl_id = H5Pcopy(args->args.is_accessible.fapl_id);

        /* Set the VOL ID and info for the underlying FAPL */
        H5Pset_vol(my_args.args.is_accessible.fapl_id, info->under_vol_id, info->under_vol_info);

        /* Set argument pointer to new arguments */
        new_args = &my_args;

        /* Set object pointer for operation */
        new_o = NULL;
    } /* end else-if */
    /* Check for 'delete' operation */
    else if (args->op_type == H5VL_FILE_DELETE) {
        /* Make a (shallow) copy of the arguments */
        memcpy(&my_args, args, sizeof(my_args));

        /* Set up the new FAPL for the updated arguments */

        /* Get copy of our VOL info from FAPL */
        H5Pget_vol_info(args->args.del.fapl_id, (void **)&info);

        /* Make sure we have info about the underlying VOL to be used */
        if (!info)
            return (-1);

        /* Keep the correct underlying VOL ID for later */
        under_vol_id = info->under_vol_id;

        /* Copy the FAPL */
        my_args.args.del.fapl_id = H5Pcopy(args->args.del.fapl_id);

        /* Set the VOL ID and info for the underlying FAPL */
        H5Pset_vol(my_args.args.del.fapl_id, info->under_vol_id, info->under_vol_info);

        /* Set argument pointer to new arguments */
        new_args = &my_args;

        /* Set object pointer for operation */
        new_o = NULL;
    } /* end else-if */
    else {
        /* Keep the correct underlying VOL ID for later */
        under_vol_id = o->under_vol_id;

        /* Set argument pointer to current arguments */
        new_args = args;

        /* Ray: for reopenning file, new_o will be created in the end */
        /* Set object pointer for operation */
        new_o = o->under_object;
    } /* end else */

    ret_value = H5VLfile_specific(new_o, under_vol_id, new_args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    /* Check for 'is accessible' operation */
    if (args->op_type == H5VL_FILE_IS_ACCESSIBLE) {
        /* Close underlying FAPL */
        H5Pclose(my_args.args.is_accessible.fapl_id);

        /* Release copy of our VOL info */
        H5VL_bypass_info_free(info);
    } /* end else-if */
    /* Check for 'delete' operation */
    else if (args->op_type == H5VL_FILE_DELETE) {
        /* Close underlying FAPL */
        H5Pclose(my_args.args.del.fapl_id);

        /* Release copy of our VOL info */
        H5VL_bypass_info_free(info);
    } /* end else-if */
    else if (args->op_type == H5VL_FILE_REOPEN) {
        if (args->args.reopen.file) {
            pthread_mutex_lock(&mutex_local);

            new_o = H5VL_bypass_new_obj(*args->args.reopen.file, o->under_vol_id);
            new_o->type = H5I_FILE;

            *args->args.reopen.file = new_o;

            assert(o->u.file.name);

            if (c_file_open_helper(new_o, o->u.file.name) < 0) {
                fprintf(stderr, "error while opening c file\n");
                goto error;
            }

            pthread_mutex_unlock(&mutex_local);
        }
    } /* end else */

    return ret_value;

error:
    if (new_o)
        H5VL_bypass_free_obj(new_o);

    return -1;
} /* end H5VL_bypass_file_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_optional
 *
 * Purpose:     Perform a connector-specific operation on a file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_file_optional(void *file, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)file;
    herr_t         ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL File Optional\n");
#endif

    ret_value = H5VLfile_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_file_optional() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_file_close
 *
 * Purpose:     Closes a file.
 *
 * Return:      Success:    0
 *              Failure:    -1, file not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_file_close(void *file, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)file;
    herr_t         ret_value;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL FILE Close\n");
#endif

    assert(o->type == H5I_FILE);
    assert(o->u.file.ref_count > 0);

    /* Release our wrapper, if underlying file was closed */
    pthread_mutex_lock(&mutex_local);

    /* Pass close request to underlying VOL connector */
    if ((ret_value = H5VLfile_close(o->under_object, o->under_vol_id, dxpl_id, req)) < 0) {
        fprintf(stderr, "Failed to close file in underlying VOL connectors\n");
        goto done;
    }

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    if (H5VL_bypass_free_obj(o) < 0) {
        fprintf(stderr, "Unable to free file object on close\n");
        ret_value = -1;
        goto done;
    }

done:
    pthread_mutex_unlock(&mutex_local);

    return ret_value;
} /* end H5VL_bypass_file_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_create
 *
 * Purpose:     Creates a group inside a container
 *
 * Return:      Success:    Pointer to a group object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id,
                         hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *group = NULL;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void          *under = NULL;
    H5VL_bypass_t *parent_file = NULL;
    bool           locked = false;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Create\n");
#endif

    if (pthread_mutex_lock(&mutex_local) < 0) {
        printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    locked = true;

    if ((under = H5VLgroup_create(o->under_object, loc_params,
         o->under_vol_id, name, lcpl_id, gcpl_id, gapl_id, dxpl_id, req)) == NULL) {
        fprintf(stderr, "Failed to create group in underlying VOL connectors\n");
        goto error;
    }

    group = H5VL_bypass_new_obj(under, o->under_vol_id);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    group->type = H5I_GROUP;

    /* Check the object type of the location and figure out the file to which this dataset belongs */
    if (o->type == H5I_FILE) {
        parent_file = o;
    } else if (o->type == H5I_GROUP) {
        parent_file = o->u.group.file;
    } else {
        fprintf(stderr, "invalid object type\n");
        goto error;
    }

    assert(parent_file->type == H5I_FILE);
    assert(parent_file->u.file.ref_count > 0);

    /* Increment the reference count of the file object */
    parent_file->u.file.ref_count++;
    group->u.group.file = parent_file;

    if (locked && pthread_mutex_unlock(&mutex_local) != 0) {
        printf("In %s of %s at line %d: pthread_mutex_unlock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    return (void *)group;
error:
    if (locked)
        pthread_mutex_unlock(&mutex_local);

    return NULL;
} /* end H5VL_bypass_group_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_open
 *
 * Purpose:     Opens a group inside a container
 *
 * Return:      Success:    Pointer to a group object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t gapl_id,
                       hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *group = NULL;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    void          *under = NULL;
    H5VL_bypass_t *parent_file = NULL;
    bool           locked = false;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Open\n");
#endif

    if (pthread_mutex_lock(&mutex_local) < 0) {
        printf("In %s of %s at line %d: pthread_mutex_lock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    locked = true;

    if ((under = H5VLgroup_open(o->under_object,
        loc_params, o->under_vol_id, name, gapl_id, dxpl_id, req)) == NULL) {
        fprintf(stderr, "Failed to open group in underlying VOL connectors\n");
        goto error;
    }

    group = H5VL_bypass_new_obj(under, o->under_vol_id);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    group->type = H5I_GROUP;

    /* Check the object type of the location and figure out the file to which this dataset belongs */
    if (o->type == H5I_FILE) {
        parent_file = o;
    } else if (o->type == H5I_GROUP) {
        parent_file = o->u.group.file;
    } else {
        fprintf(stderr, "invalid object type\n");
        goto error;
    }

    assert(parent_file->type == H5I_FILE);
    assert(parent_file->u.file.ref_count > 0);

    /* Increment the reference count of the file object */
    parent_file->u.file.ref_count++;
    group->u.group.file = parent_file;

    if (locked && pthread_mutex_unlock(&mutex_local) != 0) {
        printf("In %s of %s at line %d: pthread_mutex_unlock failed\n", __func__, __FILE__, __LINE__);
        goto error;
    }

    return (void *)group;
error:
    if (locked)
        pthread_mutex_unlock(&mutex_local);

    return NULL;
} /* end H5VL_bypass_group_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_get
 *
 * Purpose:     Get info about a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_group_get(void *obj, H5VL_group_get_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Get\n");
#endif

    ret_value = H5VLgroup_get(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_group_get() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_specific
 *
 * Purpose:     Specific operation on a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_group_specific(void *obj, H5VL_group_specific_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t              *o = (H5VL_bypass_t *)obj;
    H5VL_group_specific_args_t  my_args;
    H5VL_group_specific_args_t *new_args = NULL;
    hid_t                       under_vol_id = H5I_INVALID_HID;
    herr_t                      ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Specific\n");
#endif

    // Save copy of underlying VOL connector ID and prov helper, in case of
    // refresh destroying the current object
    under_vol_id = o->under_vol_id;

    /* Unpack arguments to get at the child file pointer when mounting a file */
    if (args->op_type == H5VL_GROUP_MOUNT) {

        /* Make a (shallow) copy of the arguments */
        memcpy(&my_args, args, sizeof(my_args));

        /* Set the object for the child file */
        my_args.args.mount.child_file = ((H5VL_bypass_t *)args->args.mount.child_file)->under_object;

        /* Point to modified arguments */
        new_args = &my_args;
    } /* end if */
    else
        new_args = args;

    ret_value = H5VLgroup_specific(o->under_object, under_vol_id, new_args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_group_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_optional
 *
 * Purpose:     Perform a connector-specific operation on a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_group_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Optional\n");
#endif

    ret_value = H5VLgroup_optional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_group_optional() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_group_close
 *
 * Purpose:     Closes a group.
 *
 * Return:      Success:    0
 *              Failure:    -1, group not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_group_close(void *grp, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)grp;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL GROUP Close\n");
#endif

    /* Avoid assertion here because the group may not be opened with the group functions
    assert(o->type == H5I_GROUP);
    assert(o->u.group.file);
    assert(o->u.group.file->u.file.ref_count > 0);
    */

    ret_value = H5VLgroup_close(o->under_object, o->under_vol_id, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    /* Release our wrapper, if underlying file was closed */
    if (ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_group_close() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_create
 *
 * Purpose:     Creates a hard / soft / UD / external link.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_create(H5VL_link_create_args_t *args, void *obj, const H5VL_loc_params_t *loc_params,
                        hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_link_create_args_t  my_args;
    H5VL_link_create_args_t *new_args = NULL;
    H5VL_bypass_t           *o            = (H5VL_bypass_t *)obj;
    hid_t                    under_vol_id = -1;
    herr_t                   ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Create\n");
#endif

    /* Try to retrieve the "under" VOL id */
    if (o)
        under_vol_id = o->under_vol_id;

    /* Fix up the link target object for hard link creation */
    if (H5VL_LINK_CREATE_HARD == args->op_type) {
        /* If it's a non-NULL pointer, find the 'under object' and re-set the args
         */
        if (args->args.hard.curr_obj) {
            /* Make a (shallow) copy of the arguments */
            memcpy(&my_args, args, sizeof(my_args));

            /* Check if we still need the "under" VOL ID */
            if (under_vol_id < 0)
                under_vol_id = ((H5VL_bypass_t *)args->args.hard.curr_obj)->under_vol_id;

            /* Set the object for the link target */
            my_args.args.hard.curr_obj = ((H5VL_bypass_t *)args->args.hard.curr_obj)->under_object;

            /* Set argument pointer to modified parameters */
            new_args = &my_args;
        } /* end if */
        else
            new_args = args;
    } /* end if */
    else
        new_args = args;

    /* Re-issue 'link create' call, possibly using the unwrapped pieces */
    ret_value = H5VLlink_create(new_args, (o ? o->under_object : NULL), loc_params, under_vol_id, lcpl_id,
                                lapl_id, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_create() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_copy
 *
 * Purpose:     Renames an object within an HDF5 container and copies it to a
 *new group.  The original name SRC is unlinked from the group graph and then
 *inserted with the new name DST (which can specify a new path for the object)
 *as an atomic operation. The names are interpreted relative to SRC_LOC_ID and
 *              DST_LOC_ID, which are either file IDs or group ID.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                      const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id,
                      void **req)
{
    H5VL_bypass_t *o_src        = (H5VL_bypass_t *)src_obj;
    H5VL_bypass_t *o_dst        = (H5VL_bypass_t *)dst_obj;
    hid_t          under_vol_id = -1;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Copy\n");
#endif

    /* Retrieve the "under" VOL id */
    if (o_src)
        under_vol_id = o_src->under_vol_id;
    else if (o_dst)
        under_vol_id = o_dst->under_vol_id;
    assert(under_vol_id > 0);

    ret_value =
        H5VLlink_copy((o_src ? o_src->under_object : NULL), loc_params1, (o_dst ? o_dst->under_object : NULL),
                      loc_params2, under_vol_id, lcpl_id, lapl_id, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_copy() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_move
 *
 * Purpose:     Moves a link within an HDF5 file to a new group.  The original
 *              name SRC is unlinked from the group graph
 *              and then inserted with the new name DST (which can specify a
 *              new path for the object) as an atomic operation. The names
 *              are interpreted relative to SRC_LOC_ID and
 *              DST_LOC_ID, which are either file IDs or group ID.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj,
                      const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id,
                      void **req)
{
    H5VL_bypass_t *o_src        = (H5VL_bypass_t *)src_obj;
    H5VL_bypass_t *o_dst        = (H5VL_bypass_t *)dst_obj;
    hid_t          under_vol_id = -1;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Move\n");
#endif

    /* Retrieve the "under" VOL id */
    if (o_src)
        under_vol_id = o_src->under_vol_id;
    else if (o_dst)
        under_vol_id = o_dst->under_vol_id;
    assert(under_vol_id > 0);

    ret_value =
        H5VLlink_move((o_src ? o_src->under_object : NULL), loc_params1, (o_dst ? o_dst->under_object : NULL),
                      loc_params2, under_vol_id, lcpl_id, lapl_id, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_move() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_get
 *
 * Purpose:     Get info about a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_args_t *args,
                     hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Get\n");
#endif

    ret_value = H5VLlink_get(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_get() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_specific
 *
 * Purpose:     Specific operation on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_specific_args_t *args,
                          hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Specific\n");
#endif

    ret_value = H5VLlink_specific(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_link_optional
 *
 * Purpose:     Perform a connector-specific operation on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_link_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args,
                          hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL LINK Optional\n");
#endif

    ret_value = H5VLlink_optional(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_link_optional() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_open
 *
 * Purpose:     Opens an object inside a container.
 *
 * Return:      Success:    Pointer to object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_bypass_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type,
                        hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *new_obj = false;
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    H5VL_bypass_t *parent_file = NULL;
    void          *under = NULL;
    bool           req_created = false;
#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Open\n");
#endif

    if ((under = H5VLobject_open(o->under_object, loc_params, o->under_vol_id, opened_type, dxpl_id, req)) == NULL) {
        fprintf(stderr, "failed to open object in underlying connector\n");
        goto error;
    }

    if ((new_obj = H5VL_bypass_new_obj(under, o->under_vol_id)) == NULL) {
        fprintf(stderr, "failed to create bypass object\n");
        goto error;
    }

    new_obj->type = *opened_type;

    /* Check the object type of the location and figure out the file to which this object belongs */
    if (o->type == H5I_FILE) {
        parent_file = o;
    } else if (o->type == H5I_GROUP) {
        parent_file = o->u.group.file;
    } else {
        fprintf(stderr, "invalid object type\n");
        goto error;
    }

    assert(parent_file->u.file.ref_count > 0);

    switch (new_obj->type) {
        case H5I_DATASET: {
            if (dset_open_helper(new_obj, dxpl_id, req) < 0) {
                fprintf(stderr, "failed to populate bypass object\n");
                goto error;
            }

            fprintf(stderr, "Bypass VOL file up due to object (dset) open: %d -> %d\n", parent_file->u.file.ref_count, parent_file->u.file.ref_count + 1);
            parent_file->u.file.ref_count++;
            new_obj->u.dataset.file = parent_file;
            break;
        }

        case H5I_GROUP: {
            fprintf(stderr, "Bypass VOL file up due to object (group) open: %d -> %d\n", parent_file->u.file.ref_count, parent_file->u.file.ref_count + 1);
            parent_file->u.file.ref_count++;
            new_obj->u.group.file = parent_file;
            break;
        }

        default: {
            break;
        }
    }

    /* Check for async request */
    if (req && *req) {
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);
        req_created = true;
    }

    return (void *)new_obj;

error:
    if (new_obj)
        H5VL_bypass_free_obj(new_obj);
    if (req && *req && req_created)
        H5VL_bypass_free_obj(*req);

    return NULL;
} /* end H5VL_bypass_object_open() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_copy
 *
 * Purpose:     Copies an object inside a container.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_object_copy(void *src_obj, const H5VL_loc_params_t *src_loc_params, const char *src_name,
                        void *dst_obj, const H5VL_loc_params_t *dst_loc_params, const char *dst_name,
                        hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o_src = (H5VL_bypass_t *)src_obj;
    H5VL_bypass_t *o_dst = (H5VL_bypass_t *)dst_obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Copy\n");
#endif

    ret_value =
        H5VLobject_copy(o_src->under_object, src_loc_params, src_name, o_dst->under_object, dst_loc_params,
                        dst_name, o_src->under_vol_id, ocpypl_id, lcpl_id, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o_src->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_object_copy() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_get
 *
 * Purpose:     Get info about an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_args_t *args,
                       hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Get\n");
#endif

    ret_value = H5VLobject_get(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_object_get() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_specific
 *
 * Purpose:     Specific operation on an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_object_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_specific_args_t *args,
                            hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    hid_t          under_vol_id = H5I_INVALID_HID;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Specific\n");
#endif

    // Save copy of underlying VOL connector ID and prov helper, in case of
    // refresh destroying the current object
    under_vol_id = o->under_vol_id;

    ret_value = H5VLobject_specific(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, under_vol_id);

    return ret_value;
} /* end H5VL_bypass_object_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_object_optional
 *
 * Purpose:     Perform a connector-specific operation for an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_object_optional(void *obj, const H5VL_loc_params_t *loc_params, H5VL_optional_args_t *args,
                            hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL OBJECT Optional\n");
#endif

    ret_value = H5VLobject_optional(o->under_object, loc_params, o->under_vol_id, args, dxpl_id, req);

    /* Check for async request */
    if (req && *req)
        *req = H5VL_bypass_new_obj(*req, o->under_vol_id);

    return ret_value;
} /* end H5VL_bypass_object_optional() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_introspect_get_conn_clss
 *
 * Purpose:     Query the connector class.
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_introspect_get_conn_cls(void *obj, H5VL_get_conn_lvl_t lvl, const H5VL_class_t **conn_cls)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INTROSPECT GetConnCls\n");
#endif

    /* Check for querying this connector's class */
    if (H5VL_GET_CONN_LVL_CURR == lvl) {
        *conn_cls = &H5VL_bypass_g;
        ret_value = 0;
    } /* end if */
    else
        ret_value = H5VLintrospect_get_conn_cls(o->under_object, o->under_vol_id, lvl, conn_cls);

    return ret_value;
} /* end H5VL_bypass_introspect_get_conn_cls() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_introspect_get_cap_flags
 *
 * Purpose:     Query the capability flags for this connector and any
 *              underlying connector(s).
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_introspect_get_cap_flags(const void *_info, uint64_t *cap_flags)
{
    const H5VL_bypass_info_t *info = (const H5VL_bypass_info_t *)_info;
    herr_t                    ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INTROSPECT GetCapFlags\n");
#endif

    /* Invoke the query on the underlying VOL connector */
    ret_value = H5VLintrospect_get_cap_flags(info->under_vol_info, info->under_vol_id, cap_flags);

    /* Bitwise OR our capability flags in */
    if (ret_value >= 0)
        *cap_flags |= H5VL_bypass_g.cap_flags;

    return ret_value;
} /* end H5VL_bypass_introspect_ext_get_cap_flags() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_introspect_opt_query
 *
 * Purpose:     Query if an optional operation is supported by this connector
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_introspect_opt_query(void *obj, H5VL_subclass_t cls, int op_type, uint64_t *flags)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL INTROSPECT OptQuery\n");
#endif

    ret_value = H5VLintrospect_opt_query(o->under_object, o->under_vol_id, cls, op_type, flags);

    return ret_value;
} /* end H5VL_bypass_introspect_opt_query() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_wait
 *
 * Purpose:     Wait (with a timeout) for an async operation to complete
 *
 * Note:        Releases the request if the operation has completed and the
 *              connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_wait(void *obj, uint64_t timeout, H5VL_request_status_t *status)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Wait\n");
#endif

    ret_value = H5VLrequest_wait(o->under_object, o->under_vol_id, timeout, status);

    return ret_value;
} /* end H5VL_bypass_request_wait() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_notify
 *
 * Purpose:     Registers a user callback to be invoked when an asynchronous
 *              operation completes
 *
 * Note:        Releases the request, if connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Notify\n");
#endif

    ret_value = H5VLrequest_notify(o->under_object, o->under_vol_id, cb, ctx);

    return ret_value;
} /* end H5VL_bypass_request_notify() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_cancel
 *
 * Purpose:     Cancels an asynchronous operation
 *
 * Note:        Releases the request, if connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_cancel(void *obj, H5VL_request_status_t *status)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Cancel\n");
#endif

    ret_value = H5VLrequest_cancel(o->under_object, o->under_vol_id, status);

    return ret_value;
} /* end H5VL_bypass_request_cancel() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_specific
 *
 * Purpose:     Specific operation on a request
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_specific(void *obj, H5VL_request_specific_args_t *args)
{
    H5VL_bypass_t *o         = (H5VL_bypass_t *)obj;
    herr_t         ret_value = -1;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Specific\n");
#endif

    ret_value = H5VLrequest_specific(o->under_object, o->under_vol_id, args);

    return ret_value;
} /* end H5VL_bypass_request_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_optional
 *
 * Purpose:     Perform a connector-specific operation for a request
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_optional(void *obj, H5VL_optional_args_t *args)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Optional\n");
#endif

    ret_value = H5VLrequest_optional(o->under_object, o->under_vol_id, args);

    return ret_value;
} /* end H5VL_bypass_request_optional() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_request_free
 *
 * Purpose:     Releases a request, allowing the operation to complete without
 *              application tracking
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_request_free(void *obj)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL REQUEST Free\n");
#endif

    ret_value = H5VLrequest_free(o->under_object, o->under_vol_id);

    if (ret_value >= 0)
        H5VL_bypass_free_obj(o);

    return ret_value;
} /* end H5VL_bypass_request_free() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_blob_put
 *
 * Purpose:     Handles the blob 'put' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_blob_put(void *obj, const void *buf, size_t size, void *blob_id, void *ctx)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL BLOB Put\n");
#endif

    ret_value = H5VLblob_put(o->under_object, o->under_vol_id, buf, size, blob_id, ctx);

    return ret_value;
} /* end H5VL_bypass_blob_put() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_blob_get
 *
 * Purpose:     Handles the blob 'get' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_blob_get(void *obj, const void *blob_id, void *buf, size_t size, void *ctx)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL BLOB Get\n");
#endif

    ret_value = H5VLblob_get(o->under_object, o->under_vol_id, blob_id, buf, size, ctx);

    return ret_value;
} /* end H5VL_bypass_blob_get() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_blob_specific
 *
 * Purpose:     Handles the blob 'specific' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_blob_specific(void *obj, void *blob_id, H5VL_blob_specific_args_t *args)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL BLOB Specific\n");
#endif

    ret_value = H5VLblob_specific(o->under_object, o->under_vol_id, blob_id, args);

    return ret_value;
} /* end H5VL_bypass_blob_specific() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_blob_optional
 *
 * Purpose:     Handles the blob 'optional' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_blob_optional(void *obj, void *blob_id, H5VL_optional_args_t *args)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL BLOB Optional\n");
#endif

    ret_value = H5VLblob_optional(o->under_object, o->under_vol_id, blob_id, args);

    return ret_value;
} /* end H5VL_bypass_blob_optional() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_token_cmp
 *
 * Purpose:     Compare two of the connector's object tokens, setting
 *              *cmp_value, following the same rules as strcmp().
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_token_cmp(void *obj, const H5O_token_t *token1, const H5O_token_t *token2, int *cmp_value)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL TOKEN Compare\n");
#endif

    /* Sanity checks */
    assert(obj);
    assert(token1);
    assert(token2);
    assert(cmp_value);

    ret_value = H5VLtoken_cmp(o->under_object, o->under_vol_id, token1, token2, cmp_value);

    return ret_value;
} /* end H5VL_bypass_token_cmp() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_token_to_str
 *
 * Purpose:     Serialize the connector's object token into a string.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_token_to_str(void *obj, H5I_type_t obj_type, const H5O_token_t *token, char **token_str)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL TOKEN To string\n");
#endif

    /* Sanity checks */
    assert(obj);
    assert(token);
    assert(token_str);

    ret_value = H5VLtoken_to_str(o->under_object, obj_type, o->under_vol_id, token, token_str);

    return ret_value;
} /* end H5VL_bypass_token_to_str() */

/*---------------------------------------------------------------------------
 * Function:    H5VL_bypass_token_from_str
 *
 * Purpose:     Deserialize the connector's object token from a string.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_bypass_token_from_str(void *obj, H5I_type_t obj_type, const char *token_str, H5O_token_t *token)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS  VOL TOKEN From string\n");
#endif

    /* Sanity checks */
    assert(obj);
    assert(token);
    assert(token_str);

    ret_value = H5VLtoken_from_str(o->under_object, obj_type, o->under_vol_id, token_str, token);

    return ret_value;
} /* end H5VL_bypass_token_from_str() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_bypass_optional
 *
 * Purpose:     Handles the generic 'optional' callback
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5VL_bypass_optional(void *obj, H5VL_optional_args_t *args, hid_t dxpl_id, void **req)
{
    H5VL_bypass_t *o = (H5VL_bypass_t *)obj;
    herr_t         ret_value = 0;

#ifdef ENABLE_BYPASS_LOGGING
    printf("------- BYPASS VOL generic Optional\n");
#endif

    ret_value = H5VLoptional(o->under_object, o->under_vol_id, args, dxpl_id, req);

    return ret_value;
} /* end H5VL_bypass_optional() */

static herr_t
should_dset_use_native(Bypass_dataset_t* dset) {
    int num_ext_files = 0;
    herr_t ret_value = 0;

    assert(dset);

    if (dset->num_filters > 0) {
        dset->use_native = true;
        dset->use_native_checked = true;
        goto done;
    }
        
    if (H5D_VIRTUAL == dset->layout) {
        dset->use_native = true;
        dset->use_native_checked = true;

        goto done;
    }

    if (H5T_INTEGER != dset->dtype_info.class) {
        dset->use_native = true;
        dset->use_native_checked = true;

        goto done;
    }

    /* Some non-numeric library types (e.g. H5T_NATIVE_UCHAR) use the H5T_INTEGER class.
     * This is a hack to avoid using the Bypass VOL for datatypes that are not equivalent to H5T_NATIVE_INT */
    if (dset->dtype_info.size != sizeof(int)) {
        dset->use_native = true;
        dset->use_native_checked = true;

        goto done;
    }

    /* For now, the Bypass VOL only supports signed integers */
    if (dset->dtype_info.sign != H5T_SGN_2) {
        dset->use_native = true;
        dset->use_native_checked = true;

        goto done;
    }

    if (dset->layout == H5D_COMPACT) {
        dset->use_native = true;
        dset->use_native_checked = true;

        goto done;
    }

    /* If external link is used, let the native library handle it */
    if ((num_ext_files = H5Pget_external_count(dset->dcpl_id)) < 0) {
        fprintf(stderr, "failed to get external file count\n");
        ret_value = -1;
        goto done;
    }

    if (num_ext_files > 0) {
        dset->use_native = true;
        dset->use_native_checked = true;

        goto done;
    }

done:
    return ret_value;
} /* should_dset_use_native */

static herr_t
release_dset_info(Bypass_dataset_t *dset) {
    herr_t ret_value = 0;

    assert(dset);

    /* Decrement the ref count of the corresponding Bypass VOL file object */
    //if (H5VL_bypass_file_close((void*) dset->file, H5P_DEFAULT, NULL) < 0) {
    if (H5VL_bypass_free_obj(dset->file) < 0) {
        fprintf(stderr, "Failed to close parent file object\n");
        ret_value = -1;
        goto done;
    }

    if (dset->dcpl_id > 0 && H5Pclose(dset->dcpl_id) < 0) {
        fprintf(stderr, "unable to decrement ref count of DCPL\n");
        ret_value = -1;
        goto done;
    }

    dset->dcpl_id = H5I_INVALID_HID;

    if (dset->space_id > 0 && H5Sclose(dset->space_id) < 0) {
        fprintf(stderr, "unable to decrement ref count of dataspace\n");
        ret_value = -1;
        goto done;
    }

    dset->space_id = H5I_INVALID_HID;

    dset->num_filters = 0;
    dset->layout = H5D_LAYOUT_ERROR;

done:
    if (ret_value < 0) {
        H5E_BEGIN_TRY {
            if (dset->dcpl_id > 0)
                H5Pclose(dset->dcpl_id);
            if (dset->space_id > 0)
                H5Sclose(dset->space_id);
        } H5E_END_TRY;
    }

    return ret_value;
}

static herr_t
release_file_info(Bypass_file_t *file) {
    herr_t ret_value = 0;

    assert(file);

    /* Wait until all thread in the thread pool finish reading the data before
     * closing the C file */
    pthread_mutex_lock(&mutex_local);

    if (file->read_started) {
        while (file->num_reads)
            pthread_cond_wait(&(file->close_ready), &mutex_local);
    }

    pthread_mutex_unlock(&mutex_local);

    /* Clean up the file object */
    if (close(file->fd) < 0) {
        fprintf(stderr, "failed to close file descriptor: %s\n", strerror(errno));
        ret_value = -1;
        goto done;
    }

    file->fd = -1;

    pthread_cond_destroy(&(file->close_ready));

done:
    return ret_value;
}

static herr_t
release_group_info(Bypass_group_t *group) {
    herr_t ret_value = 0;

    assert(group);

    /* Decrement the ref count of the corresponding Bypass VOL file object. */
    if (H5VL_bypass_free_obj(group->file) < 0) {
        fprintf(stderr, "Failed to close parent file object\n");
        ret_value = -1;
        goto done;
    }

done:
    return ret_value;
}

static herr_t
get_dset_location(H5VL_bypass_t *dset_obj, hid_t dxpl_id, void **req, haddr_t *location) {
    herr_t ret_value = 0;
    H5VL_optional_args_t                opt_args;
    H5VL_native_dataset_optional_args_t dset_opt_args;

    assert(dset_obj);
    assert(location);

    dset_opt_args.get_offset.offset = location;
    opt_args.op_type                = H5VL_NATIVE_DATASET_GET_OFFSET;
    opt_args.args                   = &dset_opt_args;

    if (H5VLdataset_optional(dset_obj->under_object, dset_obj->under_vol_id, &opt_args, dxpl_id, req) < 0) {
        fprintf(stderr, "unable to get opened dataset's location\n");
        ret_value = -1;
        goto done;
    }
done:
    return ret_value;
}

static herr_t
get_dtype_info(H5VL_bypass_t *dset_obj, hid_t dxpl_id, void **req) {
    herr_t ret_value = 0;
    dtype_info_t *type_info = NULL;
    H5VL_dataset_get_args_t get_args;

    assert(dset_obj);
    assert(dset_obj->type == H5I_DATASET);

    type_info = &dset_obj->u.dataset.dtype_info;

    /* Retrieve the dataset's datatype */
    get_args.op_type               = H5VL_DATASET_GET_TYPE;
    get_args.args.get_type.type_id = H5I_INVALID_HID;

    if (H5VLdataset_get(dset_obj->under_object, dset_obj->under_vol_id, &get_args, dxpl_id, req) < 0) {
        fprintf(stderr, "unable to get dataset's datatype\n");
        ret_value = -1;
        goto done;
    }

    if (get_args.args.get_type.type_id < 0) {
        fprintf(stderr, "retrieved datatype is invalid\n");
        ret_value = -1;
        goto done;
    }

    if (get_dtype_info_helper(get_args.args.get_type.type_id, type_info) < 0) {
        fprintf(stderr, "unable to get dataset's datatype info\n");
        ret_value = -1;
        goto done;
    }

done:
    if (get_args.args.get_type.type_id > 0)
        if (H5Tclose(get_args.args.get_type.type_id) < 0) {
            fprintf(stderr, "unable to close datatype\n");
            ret_value = -1;
            goto done;
        }

    return ret_value;
}

static herr_t
get_dtype_info_helper(hid_t type_id, dtype_info_t *type_info_out) {
    herr_t ret_value = 0;

    if ((type_info_out->class = H5Tget_class(type_id)) < 0) {
        fprintf(stderr, "unable to get dataset's datatype class\n");
        ret_value = -1;
        goto done;
    }

    if ((type_info_out->size = H5Tget_size(type_id)) < 0) {
        fprintf(stderr, "unable to get dataset's datatype size\n");
        ret_value = -1;
        goto done;
    }

    if ((type_info_out->order = H5Tget_order(type_id)) < 0) {
        fprintf(stderr, "unable to get dataset's datatype order\n");
        ret_value = -1;
        goto done;
    }

    if (type_info_out->class == H5T_INTEGER) {
        if ((type_info_out->sign = H5Tget_sign(type_id)) < 0) {
            fprintf(stderr, "unable to get dataset's datatype sign\n");
            ret_value = -1;
            goto done;
        }
    } else {
        type_info_out->sign = H5T_SGN_ERROR;
    }
done:
    return ret_value;
}

static bool bypass_types_equal(dtype_info_t *type_info1, dtype_info_t *type_info2) {
    bool ret_value = true;

    if (type_info1->class != type_info2->class)
        ret_value = false;
    
    if (type_info1->size != type_info2->size)
        ret_value = false;

    if (type_info1->order != type_info2->order)
        ret_value = false;

    if (type_info1->sign != type_info2->sign)
        ret_value = false;

    return ret_value;
}

static H5D_space_status_t
get_dset_space_status(H5VL_bypass_t *dset_obj, hid_t dxpl_id, void **req) {
    H5D_space_status_t ret_value = H5D_SPACE_STATUS_ERROR;
    H5VL_dataset_get_args_t get_args;

    assert(dset_obj);
    assert(dset_obj->type == H5I_DATASET);

    get_args.op_type               = H5VL_DATASET_GET_SPACE_STATUS;
    get_args.args.get_space_status.status = &ret_value;

    if (H5VLdataset_get(dset_obj->under_object, dset_obj->under_vol_id, &get_args, dxpl_id, req) < 0) {
        fprintf(stderr, "unable to get dataset's space status\n");
        ret_value = H5D_SPACE_STATUS_ERROR;
        goto done;
    }

done:
    return ret_value;
}

/* Flush the file containing the provided dataset */
static herr_t
flush_containing_file(H5VL_bypass_t *dset) {
    herr_t ret_value = 0;
    H5VL_file_specific_args_t args;

    args.op_type = H5VL_FILE_FLUSH;
    args.args.flush.obj_type = H5I_DATASET;
    args.args.flush.scope = H5F_SCOPE_LOCAL;

    if ((H5VLfile_specific((void*) dset->under_object, dset->under_vol_id, &args, H5P_DEFAULT, NULL) < 0)) {
        fprintf(stderr, "unable to flush file\n");
        ret_value = -1;
        goto done;
    }

done:
    return ret_value;
}

static herr_t
bypass_queue_destroy(task_queue_t *queue, bool need_mutex) {
    herr_t ret_value = 0;
    bool locked = false;
    Bypass_task_t *curr = NULL;
    Bypass_task_t *next = NULL;

    if (!need_mutex) {
	if (pthread_mutex_lock(&mutex_local) != 0) {
	    fprintf(stderr, "pthread_mutex_lock failed\n");
	    ret_value = -1;
	    goto done;
	}

	locked = true;
    }

    if (queue->bypass_queue_head_g == NULL) {
        assert(queue->bypass_queue_tail_g == NULL);
        assert(queue->tasks_in_queue == 0);
        goto done;
    }

    curr = queue->bypass_queue_head_g;

    while (curr != NULL) {
        next = curr->next;
        free(curr);
        curr = next;
    }

    queue->bypass_queue_head_g = NULL;
    queue->bypass_queue_tail_g = NULL;
    queue->tasks_in_queue = 0;

done:
    if (locked && pthread_mutex_unlock(&mutex_local) != 0) {
        fprintf(stderr, "pthread_mutex_unlock failed\n");
        ret_value = -1;
    }

    return ret_value;
}

static herr_t
bypass_queue_push(task_queue_t *queue, Bypass_task_t *task, bool need_mutex) {
    herr_t ret_value = 0;
    bool locked = false;

    if (need_mutex) {
	if (pthread_mutex_lock(&mutex_local) != 0) {
	    fprintf(stderr, "pthread_mutex_lock failed\n");
	    ret_value = -1;
	    goto done;
	}

	locked = true;
    }

    /* Increment the task count that this pointer points to */
    atomic_fetch_add(task->task_count_ptr, 1);

    if (queue->bypass_queue_head_g == NULL) {
        /* Queue is empty */
        assert(queue->bypass_queue_tail_g == NULL);
        assert(queue->tasks_in_queue == 0);

        queue->bypass_queue_head_g = task;
        queue->bypass_queue_tail_g = task;
        queue->tasks_in_queue = 1;
    } else {
        assert(queue->bypass_queue_tail_g != NULL);
        assert(queue->tasks_in_queue > 0);

        queue->bypass_queue_tail_g->next = task;
        queue->bypass_queue_tail_g = task;
        queue->tasks_in_queue++;

        assert(queue->bypass_queue_head_g != queue->bypass_queue_tail_g);
    }

done:
    if (locked && pthread_mutex_unlock(&mutex_local) != 0) {
        fprintf(stderr, "pthread_mutex_unlock failed\n");
        ret_value = -1;
    }

    return ret_value;
}

/* Retrieve a task from a queue.
 * Task must be released by caller.*/
static Bypass_task_t *
bypass_queue_pop(task_queue_t *queue, bool need_mutex) {
    Bypass_task_t *ret_value = NULL;
    bool locked = false;

    if (need_mutex) {
	if (pthread_mutex_lock(&mutex_local) != 0) {
	    fprintf(stderr, "pthread_mutex_lock failed\n");
	    goto done;
	}

	locked = true;
    }

    /* Queue is empty */
    if (queue->bypass_queue_head_g == NULL) {
        assert(queue->bypass_queue_tail_g == NULL);
        assert(queue->tasks_in_queue == 0);
        ret_value = NULL;
    } else if (queue->bypass_queue_head_g == queue->bypass_queue_tail_g) {
        /* Queue has only one element */
        assert(queue->tasks_in_queue == 1);
        ret_value = queue->bypass_queue_head_g;

        queue->bypass_queue_head_g = NULL;
        queue->bypass_queue_tail_g = NULL;
        queue->tasks_in_queue = 0;
    } else {
        /* Queue has >= 2 elements */
        assert(queue->tasks_in_queue > 1);
        assert(queue->bypass_queue_head_g->next != NULL);

        ret_value = queue->bypass_queue_head_g;

        queue->bypass_queue_head_g = queue->bypass_queue_head_g->next;
        queue->tasks_in_queue--;
    }

done:
    if (locked && pthread_mutex_unlock(&mutex_local) != 0) {
        fprintf(stderr, "pthread_mutex_unlock failed\n");
        ret_value = NULL;
    }

    return ret_value;
}

static Bypass_task_t *
bypass_task_create(sel_info_t *sel_info, haddr_t addr, size_t size, void *buf) {
    Bypass_task_t *ret_value = NULL;

    if ((ret_value = (Bypass_task_t *)malloc(sizeof(Bypass_task_t))) == NULL) {
        fprintf(stderr, "failed to allocate space for new read task\n");
        goto done;
    }

    ret_value->file = sel_info->file;
    ret_value->addr = addr;
    ret_value->size = size;
    ret_value->vec_buf = buf;
    ret_value->task_count_ptr = sel_info->task_count_ptr;
    ret_value->local_condition_ptr = sel_info->local_condition_ptr;

    /* Will be populated after this task is inserted into queue */
    ret_value->next = NULL;

done:
    return ret_value;
}

static herr_t
bypass_task_release(Bypass_task_t *task) {
    herr_t ret_value = 0;

    free(task);

    return ret_value;
}
