#include <stdio.h>
#include <stdlib.h>
#include "hdf5.h"
#include "new_h5api.h"

#define H5FILE_NAME     "testfile.h5"
#define GROUPNAME       "GroupA"
#define DATASETNAME     "IntArray"
#define NX     5                      /* dataset dimensions */
#define NY     6
#define RANK   2

int main()
{
    hid_t       file, dataset;          /* file and dataset handles */
    hid_t       group;                  /* group handle */
    hid_t       datatype, dataspace;    /* handles */
    hsize_t     dimsf[2];               /* dataset dimensions */
    int         data[NX][NY];           /* data to write */
    int         i, j;
    double      dval;
    unsigned    uval;

    /*
     * Data  and output buffer initialization.
     */
    for(j = 0; j < NX; j++)
	for(i = 0; i < NY; i++)
	    data[j][i] = i + j;
    /*
     * 0 1 2 3 4 5
     * 1 2 3 4 5 6
     * 2 3 4 5 6 7
     * 3 4 5 6 7 8
     * 4 5 6 7 8 9
     */

    /*
     * Create a new file using H5F_ACC_TRUNC access,
     * default file creation properties, and default file
     * access properties.
     */
    file = H5Fcreate(H5FILE_NAME, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

    /*
     * Describe the size of the array and create the data space for fixed
     * size dataset.
     */
    dimsf[0] = NX;
    dimsf[1] = NY;
    dataspace = H5Screate_simple(RANK, dimsf, NULL);

    /*
     * Define datatype for the data in the file.
     * We will store little endian INT numbers.
     */
    datatype = H5Tcopy(H5T_NATIVE_INT);
    H5Tset_order(datatype, H5T_ORDER_LE);

    /*
     * Create a new dataset within the file using defined dataspace and
     * datatype and default dataset creation properties.
     */
    dataset = H5Dcreate2(file, DATASETNAME, datatype, dataspace,
			H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    /*
     * Write the data to the dataset using default transfer properties.
     */
    H5Dwrite(dataset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);


    /* Invoke the new "foo" operation, in the VOL connector */
    H5Dfoo(dataset, H5P_DEFAULT, (int)10, (double)3.14);

    /* Invoke the new "bar" operation, in the VOL connector */
    dval = 0.1;
    uval = 10;
    H5Dbar(dataset, H5P_DEFAULT, &dval, &uval);
    printf("dval = %f, uval = %u\n", dval, uval);

    /*
     * Close/release dataset resources.
     */
    H5Sclose(dataspace);
    H5Tclose(datatype);
    H5Dclose(dataset);

    /* Create a group */
    group = H5Gcreate2(file, GROUPNAME, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    /* Invoke the new "fiddle" operation, in the VOL connector */
    H5Gfiddle(group, H5P_DEFAULT);

    /* Close group */
    H5Gclose(group);

    /* Close file */
    H5Fclose(file);

    exit(0);
}

