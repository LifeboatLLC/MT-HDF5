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
 * Purpose:	The public header file for the pass-through VOL connector.
 */

#ifndef _new_h5api_H
#define _new_h5api_H

/* Include HDF5 header files */
#include "hdf5.h"


#ifdef __cplusplus
extern "C" {
#endif

/* New "public" API routines */
herr_t H5Dfoo(const char *app_file, const char *app_func, unsigned app_line,
              hid_t dset_id, hid_t dxpl_id, int i, double d);
herr_t H5Dfoo_async(const char *app_file, const char *app_func, unsigned app_line,
                    hid_t dset_id, hid_t dxpl_id, int i, double d, hid_t es_id);
herr_t H5Dbar(const char *app_file, const char *app_func, unsigned app_line,
              hid_t dset_id, hid_t dxpl_id, double *dp, unsigned *up);
herr_t H5Dbar_async(const char *app_file, const char *app_func, unsigned app_line,
                    hid_t dset_id, hid_t dxpl_id, double *dp, unsigned *up, hid_t es_id);
herr_t H5Gfiddle(const char *app_file, const char *app_func, unsigned app_line,
                 hid_t group_id, hid_t dxpl_id);
herr_t H5Gfiddle_async(const char *app_file, const char *app_func, unsigned app_line,
                        hid_t group_id, hid_t dxpl_id, hid_t es_id);

/* API Wrappers for new "API" routines */
/* (Must be defined _after_ the function prototype) */
/* (And must only defined when included in application code, not the implementation) */
#ifndef NEW_H5API_IMPL
#define H5Dfoo(...)             H5Dfoo(__FILE__, __func__, __LINE__, __VA_ARGS__)
#define H5Dfoo_async(...)       H5Dfoo_async(__FILE__, __func__, __LINE__, __VA_ARGS__)
#define H5Dbar(...)             H5Dbar(__FILE__, __func__, __LINE__, __VA_ARGS__)
#define H5Dbar_async(...)       H5Dbar_async(__FILE__, __func__, __LINE__, __VA_ARGS__)
#define H5Gfiddle(...)          H5Gfiddle(__FILE__, __func__, __LINE__, __VA_ARGS__)
#define H5Gfiddle_async(...)    H5Gfiddle_async(__FILE__, __func__, __LINE__, __VA_ARGS__)
#endif /* NEW_H5API_IMPL */

#ifdef __cplusplus
}
#endif

#endif /* _new_h5api_H */

