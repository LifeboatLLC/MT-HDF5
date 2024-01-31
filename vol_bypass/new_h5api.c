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
 * Purpose:	The new public API routines, dynamically registered by VOL
 *              connector.
 */

/* Headers needed */
#include "assert.h"

/* Define macro that can be check in header file, to disable "user" macros" */
#define NEW_H5API_IMPL

/* Public prototypes for the new API routines, etc. */
#include "new_h5api.h"

/* This connector's private header */
#include "H5VLpassthru_ext_private.h"


/* Operation values for new "API" routines */
/* These are initialized in the VOL connector's 'init' callback at runtime.
 *      It's good practice to reset them back to -1 in the 'atclose' callback.
 */
static int H5VL_new_api_dataset_foo_op_g = -1;
static int H5VL_new_api_dataset_bar_op_g = -1;
static int H5VL_new_api_group_fiddle_op_g = -1;

static void
ext_passthru_reset(void *_ctx)
{
    /* Reset the operation IDs, just for completeness */
    H5VL_new_api_dataset_foo_op_g = -1;
    H5VL_new_api_dataset_bar_op_g = -1;
    H5VL_new_api_group_fiddle_op_g = -1;
}

static int
ext_passthru_setup(void)
{
    /* Look up all the operation values */
    if(H5VLfind_opt_operation(H5VL_SUBCLS_DATASET, H5VL_PASSTHRU_EXT_DYN_FOO, &H5VL_new_api_dataset_foo_op_g) < 0)
        return(-1);
    if(H5VLfind_opt_operation(H5VL_SUBCLS_DATASET, H5VL_PASSTHRU_EXT_DYN_BAR, &H5VL_new_api_dataset_bar_op_g) < 0)
        return(-1);
    if(H5VLfind_opt_operation(H5VL_SUBCLS_GROUP, H5VL_PASSTHRU_EXT_DYN_FIDDLE, &H5VL_new_api_group_fiddle_op_g) < 0)
        return(-1);

    /* Register callback for library shutdown, to release resources */
    if (H5atclose(ext_passthru_reset, NULL) < 0) {
        fprintf(stderr, "H5atclose failed\n");
        return(-1);
    }

    return 0;
}


/*-------------------------------------------------------------------------
 * Function:    H5Dfoo
 *
 * Purpose:     Performs the 'foo' operation on a dataset, using the
 *              dataset 'optional' VOL callback.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dfoo(const char *app_file, const char *app_func, unsigned app_line,
    hid_t dset_id, hid_t dxpl_id, int i, double d)
{
    H5VL_optional_args_t vol_cb_args;                   /* Wrapper for invoking optional operation */
    H5VL_passthru_ext_dataset_foo_args_t foo_args;      /* Parameters for 'foo' operation */

    /* Set up the operation value, if it hasn't already been retrieved */
    if(-1 == H5VL_new_api_dataset_foo_op_g)
        if(ext_passthru_setup() < 0)
            return(-1);
    assert(H5VL_new_api_dataset_foo_op_g > 0);

    /* Set up args for invoking optional callback */
    foo_args.i = i;
    foo_args.d = d;
    vol_cb_args.op_type = H5VL_new_api_dataset_foo_op_g;
    vol_cb_args.args = &foo_args;

    /* Call the VOL dataset optional routine, requesting 'foo' occur */
    if(H5VLdataset_optional_op_wrap(app_file, app_func, app_line, dset_id, &vol_cb_args, dxpl_id, H5ES_NONE) < 0)
        return(-1);

    return 0;
} /* end H5Dfoo() */


/*-------------------------------------------------------------------------
 * Function:    H5Dfoo_async
 *
 * Purpose:     Asynchronously performs the 'foo' operation on a dataset, using
 *              the dataset 'optional' VOL callback.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dfoo_async(const char *app_file, const char *app_func, unsigned app_line,
    hid_t dset_id, hid_t dxpl_id, int i, double d, hid_t es_id)
{
    H5VL_optional_args_t vol_cb_args;                   /* Wrapper for invoking optional operation */
    H5VL_passthru_ext_dataset_foo_args_t foo_args;      /* Parameters for 'foo' operation */

    /* Set up the operation value, if it hasn't already been retrieved */
    if(-1 == H5VL_new_api_dataset_foo_op_g)
        if(ext_passthru_setup() < 0)
            return(-1);
    assert(H5VL_new_api_dataset_foo_op_g > 0);

    /* Set up args for invoking optional callback */
    foo_args.i = i;
    foo_args.d = d;
    vol_cb_args.op_type = H5VL_new_api_dataset_foo_op_g;
    vol_cb_args.args = &foo_args;

    /* Call the VOL dataset optional routine, requesting 'foo' occur */
    if(H5VLdataset_optional_op_wrap(app_file, app_func, app_line, dset_id, &vol_cb_args, dxpl_id, es_id) < 0)
        return(-1);

    return 0;
} /* end H5Dfoo() */


/*-------------------------------------------------------------------------
 * Function:    H5Dbar
 *
 * Purpose:     Performs the 'bar' operation on a dataset, using the
 *              dataset 'optional' VOL callback.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dbar(const char *app_file, const char *app_func, unsigned app_line,
    hid_t dset_id, hid_t dxpl_id, double *dp, unsigned *up)
{
    H5VL_optional_args_t vol_cb_args;                   /* Wrapper for invoking optional operation */
    H5VL_passthru_ext_dataset_bar_args_t bar_args;      /* Parameters for 'bar' operation */

    /* Set up the operation value, if it hasn't already been retrieved */
    if(-1 == H5VL_new_api_dataset_bar_op_g)
        if(ext_passthru_setup() < 0)
            return(-1);
    assert(H5VL_new_api_dataset_bar_op_g > 0);

    /* Set up args for invoking optional callback */
    bar_args.dp = dp;
    bar_args.up = up;
    vol_cb_args.op_type = H5VL_new_api_dataset_bar_op_g;
    vol_cb_args.args = &bar_args;

    /* Call the VOL dataset optional routine, requesting 'bar' occur */
    if(H5VLdataset_optional_op_wrap(app_file, app_func, app_line, dset_id, &vol_cb_args, dxpl_id, H5ES_NONE) < 0)
        return(-1);

    return 0;
} /* end H5Dbar() */


/*-------------------------------------------------------------------------
 * Function:    H5Dbar_async
 *
 * Purpose:     Asynchronously performs the 'bar' operation on a dataset, using
 *              the dataset 'optional' VOL callback.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Dbar_async(const char *app_file, const char *app_func, unsigned app_line,
    hid_t dset_id, hid_t dxpl_id, double *dp, unsigned *up, hid_t es_id)
{
    H5VL_optional_args_t vol_cb_args;                   /* Wrapper for invoking optional operation */
    H5VL_passthru_ext_dataset_bar_args_t bar_args;      /* Parameters for 'bar' operation */

    /* Set up the operation value, if it hasn't already been retrieved */
    if(-1 == H5VL_new_api_dataset_bar_op_g)
        if(ext_passthru_setup() < 0)
            return(-1);
    assert(H5VL_new_api_dataset_bar_op_g > 0);

    /* Set up args for invoking optional callback */
    bar_args.dp = dp;
    bar_args.up = up;
    vol_cb_args.op_type = H5VL_new_api_dataset_bar_op_g;
    vol_cb_args.args = &bar_args;

    /* Call the VOL dataset optional routine, requesting 'bar' occur */
    if(H5VLdataset_optional_op_wrap(app_file, app_func, app_line, dset_id, &vol_cb_args, dxpl_id, es_id) < 0)
        return(-1);

    return 0;
} /* end H5Dbar() */


/*-------------------------------------------------------------------------
 * Function:    H5Gfiddle
 *
 * Purpose:     Performs the 'fiddle' operation on a group, using the
 *              group 'optional' VOL callback.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Gfiddle(const char *app_file, const char *app_func, unsigned app_line,
    hid_t group_id, hid_t dxpl_id)
{
    H5VL_optional_args_t vol_cb_args;                   /* Wrapper for invoking optional operation */

    /* Set up the operation value, if it hasn't already been retrieved */
    if(-1 == H5VL_new_api_group_fiddle_op_g)
        if(ext_passthru_setup() < 0)
            return(-1);
    assert(H5VL_new_api_group_fiddle_op_g > 0);

    /* Set up args for invoking optional callback */
    vol_cb_args.op_type = H5VL_new_api_group_fiddle_op_g;
    vol_cb_args.args = NULL;

    /* Call the VOL group optional routine, requesting 'fiddle' occur */
    if(H5VLgroup_optional_op_wrap(app_file, app_func, app_line, group_id, &vol_cb_args, dxpl_id, H5ES_NONE) < 0)
        return(-1);

    return 0;
} /* end H5Gfiddle() */


/*-------------------------------------------------------------------------
 * Function:    H5Gfiddle_async
 *
 * Purpose:     Asynchronously ePrforms the 'fiddle' operation on a group, using
 *              the group 'optional' VOL callback.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Gfiddle_async(const char *app_file, const char *app_func, unsigned app_line,
    hid_t group_id, hid_t dxpl_id, hid_t es_id)
{
    H5VL_optional_args_t vol_cb_args;                   /* Wrapper for invoking optional operation */

    /* Set up the operation value, if it hasn't already been retrieved */
    if(-1 == H5VL_new_api_group_fiddle_op_g)
        if(ext_passthru_setup() < 0)
            return(-1);
    assert(H5VL_new_api_group_fiddle_op_g > 0);

    /* Set up args for invoking optional callback */
    vol_cb_args.op_type = H5VL_new_api_group_fiddle_op_g;
    vol_cb_args.args = NULL;

    /* Call the VOL group optional routine, requesting 'fiddle' occur */
    if(H5VLgroup_optional_op_wrap(app_file, app_func, app_line, group_id, &vol_cb_args, dxpl_id, es_id) < 0)
        return(-1);

    return 0;
} /* end H5Gfiddle() */

