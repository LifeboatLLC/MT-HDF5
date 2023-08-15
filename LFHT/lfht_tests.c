#include <sys/time.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include "lfht.h"
#include "lfht.c"

#define MAX_NUM_THREADS         32

void lfht_verify_list_lens(struct lfht_t * lfht_ptr);
void lfht_dump_interesting_stats(struct lfht_t * lfht_ptr);

void lfht_hash_fcn_test(void);
void lfht_hash_to_index_test(void);

void lfht_lfsll_serial_test_1(void);
void lfht_lfsll_serial_test_2(void);
void lfht_lfsll_serial_test_3(void);

void lfht_serial_test_1(void);
void lfht_serial_test_2(void);
void lfht_serial_test_3(void);

void * lfht_mt_test_fcn_1(void * args);
void * lfht_mt_test_fcn_2(void * args);

void lfht_lfsll_mt_test_fcn_1__serial_test();
void lfht_lfsll_mt_test_fcn_2__serial_test();

void lfht_mt_test_fcn_1__serial_test();
void lfht_mt_test_fcn_2__serial_test();

void lfht_lfsll_mt_test_1(int nthreads);
void lfht_lfsll_mt_test_2(int nthreads);
void lfht_lfsll_mt_test_3(int nthreads);

void lfht_mt_test_1(int run, int nthreads);
void lfht_mt_test_2(int run, int nthreads);
void lfht_mt_test_3(int run, int nthreads);


/***********************************************************************************
 *
 * lfht_verify_list_lens()()
 *
 *     For the supplied instance of lfht_t, verify that the lfsll_phys_len and 
 *     fl_len fields are correct.
 *
 *     Any discrepency will trigger an assertion.
 *
 *                                                   JRM -- 7/7/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_verify_list_lens(struct lfht_t * lfht_ptr)
{
    bool marked_for_deletion;
    int lfsll_log_len = 0;
    int lfsll_phys_len = 0;
    int num_sentinels = 0;
    int fl_len = 0;
    struct lfht_node_t * node_ptr;
    struct lfht_node_t * next;
    struct lfht_fl_node_t * fl_node_ptr;
#if LFHT__USE_SPTR
    struct lfht_flsptr_t fl_shead;
    struct lfht_flsptr_t fl_stail;
    struct lfht_flsptr_t snext;
#endif /* LFHT__USE_SPTR */

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);

    node_ptr = atomic_load(&(lfht_ptr->lfsll_root));

    while ( node_ptr ) {

        lfsll_phys_len++;

        next = atomic_load(&(node_ptr->next));

        marked_for_deletion = ( 1 == (((unsigned long long int)(next)) & 0x01ULL) );

        if ( node_ptr->sentinel ) {

            num_sentinels++;
            assert(!marked_for_deletion);

        } else if ( ! marked_for_deletion ) {

            lfsll_log_len++;
        }

        node_ptr = (struct lfht_node_t *)(((unsigned long long)(next)) & (~0x01ULL));
    }

    assert(num_sentinels  == atomic_load(&(lfht_ptr->buckets_initialized)) + 1);
    assert(lfsll_log_len  == atomic_load(&(lfht_ptr->lfsll_log_len)));
    assert(lfsll_phys_len == atomic_load(&(lfht_ptr->lfsll_phys_len)));

#if LFHT__USE_SPTR
    fl_shead = atomic_load(&(lfht_ptr->fl_shead));
    fl_stail = atomic_load(&(lfht_ptr->fl_stail));
    fl_node_ptr = fl_shead.ptr;

    if ( fl_shead.sn > fl_stail.sn ) {
        fprintf(stdout, "\n fl_shead.sn = %lld, fl_stail.sh = %lld\n", fl_shead.sn, fl_stail.sn);
    }

    while( fl_node_ptr ) {

        assert(0ULL == atomic_load(&(fl_node_ptr->ref_count)));
        fl_len++;
        snext = atomic_load(&(fl_node_ptr->snext));
        fl_node_ptr = snext.ptr;
    }
#else /* LFHT__USE_SPTR */
    fl_node_ptr = atomic_load(&(lfht_ptr->fl_head));

    while( fl_node_ptr ) {

        fl_len++;
        fl_node_ptr = atomic_load(&(fl_node_ptr->next));
    }
#endif /* LFHT__USE_SPTR */

    assert(fl_len == atomic_load(&(lfht_ptr->fl_len)));

    return;

} /* lfht_verify_list_lens() */


/***********************************************************************************
 *
 * lfht_dump_interesting_stats()()
 *
 *     For the supplied instance of lfht_t, test to see if any of the "interesting"
 *     stats are non-zero, and if so, display the value.
 *
 *                                                   JRM -- 7/12/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_dump_interesting_stats(struct lfht_t * lfht_ptr)
{
    bool marked_for_deletion;
    unsigned long long int buckets_defined_update_cols;
    unsigned long long int buckets_defined_update_retries;
    unsigned long long int bucket_init_cols;
    unsigned long long int bucket_init_col_sleeps;
    unsigned long long int recursive_bucket_inits;
    unsigned long long int sentinels_traversed;

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);

    buckets_defined_update_cols    = atomic_load(&(lfht_ptr->buckets_defined_update_cols));
    buckets_defined_update_retries = atomic_load(&(lfht_ptr->buckets_defined_update_retries));
    bucket_init_cols               = atomic_load(&(lfht_ptr->bucket_init_cols));
    bucket_init_col_sleeps         = atomic_load(&(lfht_ptr->bucket_init_col_sleeps));
    recursive_bucket_inits         = atomic_load(&(lfht_ptr->recursive_bucket_inits));
    sentinels_traversed            = atomic_load(&(lfht_ptr->sentinels_traversed));

    if ( ( buckets_defined_update_cols > 0 ) ||
         ( buckets_defined_update_retries > 0 ) ) {

        fprintf(stdout, "\n\n");

        if ( ( buckets_defined_update_cols > 0 ) || ( buckets_defined_update_retries ) ) {

            fprintf(stdout, "buckets_defined update cols / retries = %lld / %lld.\n", 
                    buckets_defined_update_cols, buckets_defined_update_retries);
        }

        if ( ( bucket_init_cols ) || ( bucket_init_col_sleeps ) ) {

            fprintf(stdout, "bucket init cols / bucket init col sleeps = %lld / %lld.\n", 
                    bucket_init_cols, bucket_init_col_sleeps);
        }
#if 0
        if ( ( recursive_bucket_inits ) || ( sentinels_traversed ) ) {

            fprintf(stdout, "recursive bucket inits / sentinels traversed = %lld / %lld.\n", 
                    recursive_bucket_inits, sentinels_traversed);
        }
#endif
    }

    return;

} /* lfht_verify_list_lens() */


/***********************************************************************************
 *
 * lfht_hash_fcn_test()
 *
 *     Verify that lfht_id_to_hash() generates the correct results.
 *
 *     Any failure should trigger an assertion.
 *
 *     Note that the ids, and the expected values for the regular and sentinel hashes 
 *     depend on the value of LFHT__NUM_HASH_BITS -- and will have to be adjusted if 
 *     this constant changes.
 *
 *                                                   JRM -- 6/17/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_hash_fcn_test(void)
{
    int i;
    int num_tests = 17;
#if ( LFHT__NUM_HASH_BITS == 48 )
    unsigned long long int ids[] = { 
        0x0000000000000ULL, 0x0000000000001ULL, 0x0000000000002ULL, 0x0000000000003ULL,
        0x0000000000004ULL, 0x0000000000005ULL, 0x0000000000006ULL, 0x0000000000007ULL,
        0x0000000000008ULL, 0x0000000000009ULL, 0x000000000000AULL, 0x000000000000BULL,
        0x000000000000CULL, 0x000000000000DULL, 0x000000000000EULL, 0x000000000000FULL,
        0x0FFFFFFFFFFFFULL };
    unsigned long long int regular_hashes[] = { 
        0x0000000000001ULL, 0x1000000000001ULL, 0x0800000000001ULL, 0x1800000000001ULL,
        0x0400000000001ULL, 0x1400000000001ULL, 0x0C00000000001ULL, 0x1C00000000001ULL,
        0x0200000000001ULL, 0x1200000000001ULL, 0x0A00000000001ULL, 0x1A00000000001ULL,
        0x0600000000001ULL, 0x1600000000001ULL, 0x0E00000000001ULL, 0x1E00000000001ULL,
        0x1FFFFFFFFFFFFULL };
    unsigned long long int sentinel_hashes[] = { 
        0x0000000000000ULL, 0x1000000000000ULL, 0x0800000000000ULL, 0x1800000000000ULL,
        0x0400000000000ULL, 0x1400000000000ULL, 0x0C00000000000ULL, 0x1C00000000000ULL,
        0x0200000000000ULL, 0x1200000000000ULL, 0x0A00000000000ULL, 0x1A00000000000ULL,
        0x0600000000000ULL, 0x1600000000000ULL, 0x0E00000000000ULL, 0x1E00000000000ULL,
        0x1FFFFFFFFFFFEULL };
#else /* LFHT__NUM_HASH_BITS == 57 */
    unsigned long long int ids[] = { 
        0x0000000000000000ULL, 0x0000000000000001ULL, 0x0000000000000002ULL, 0x0000000000000003ULL,
        0x0000000000000004ULL, 0x0000000000000005ULL, 0x0000000000000006ULL, 0x0000000000000007ULL,
        0x0000000000000008ULL, 0x0000000000000009ULL, 0x000000000000000AULL, 0x000000000000000BULL,
        0x000000000000000CULL, 0x000000000000000DULL, 0x000000000000000EULL, 0x000000000000000FULL,
        0x01FFFFFFFFFFFFFFULL };
    unsigned long long int regular_hashes[] = { 
        0x0000000000000001ULL, 0x0200000000000001ULL, 0x0100000000000001ULL, 0x0300000000000001ULL,
        0x0080000000000001ULL, 0x0280000000000001ULL, 0x0180000000000001ULL, 0x0380000000000001ULL,
        0x0040000000000001ULL, 0x0240000000000001ULL, 0x0140000000000001ULL, 0x0340000000000001ULL,
        0x00c0000000000001ULL, 0x02C0000000000001ULL, 0x01C0000000000001ULL, 0x03C0000000000001ULL,
        0x03FFFFFFFFFFFFFFULL };
    unsigned long long int sentinel_hashes[] = { 
        0x0000000000000000ULL, 0x0200000000000000ULL, 0x0100000000000000ULL, 0x0300000000000000ULL,
        0x0080000000000000ULL, 0x0280000000000000ULL, 0x0180000000000000ULL, 0x0380000000000000ULL,
        0x0040000000000000ULL, 0x0240000000000000ULL, 0x0140000000000000ULL, 0x0340000000000000ULL,
        0x00c0000000000000ULL, 0x02C0000000000000ULL, 0x01C0000000000000ULL, 0x03C0000000000000ULL,
        0x03FFFFFFFFFFFFFEULL };

    assert(LFHT__NUM_HASH_BITS == 57);
#endif /* LFHT__NUM_HASH_BITS == 57 */

    fprintf(stdout, "LFHT hash function test ...");

    for (i = 0; i < num_tests; i++ ) {

        if ( regular_hashes[i] != lfht_id_to_hash(ids[i], false) ) {

            fprintf(stdout, "\nhash test %d: regular hassh of 0x%llx = 0x%llx (0x%llx expected)\n",
                    i, ids[i], lfht_id_to_hash(ids[i], false), regular_hashes[i]);
        }

        if ( sentinel_hashes[i] != lfht_id_to_hash(ids[i], true) ) {

            fprintf(stdout, "\nhash test %d: sentinel hassh of 0x%llx = 0x%llx (0x%llx expected)\n",
                    i, ids[i], lfht_id_to_hash(ids[i], true), sentinel_hashes[i]);
        }
    }

    assert(LFHT__MAX_HASH == lfht_id_to_hash(LFHT__MAX_ID, false));

    fprintf(stdout, " Done.\n");

    return;

} /* lfht_hash_fcn_test() */


/***********************************************************************************
 *
 * lfht_hash_to_index_test()
 *
 *     Test functioning of the hash to index function
 *     
 *     Compute the hash values for ids 0 to 1023 and store in an array.
 *
 *     For each hash value, and for index_bits 0 to 3, compute the index of 
 *     the hash bucket each hash value maps to.
 *
 *     Verify that the computed index values are expected.
 *
 *     Take down the lfsll and return.
 *
 *     Any failure should trigger an assertion.
 *
 *                                                   JRM -- 7/3/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_hash_to_index_test(void)
{
    void * value = NULL;
    unsigned long long int i;
    int index_bits; 
    unsigned long long int hash[16];
    unsigned long long int index[4][16];
    unsigned long long int expected_index[4][16] = 
    {
       /* 0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15 */
        { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
        { 0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1,  0,  1 },
        { 0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2,  3 },
        { 0,  1,  2,  3,  4,  5,  6,  7,  0,  1,  2,  3,  4,  5,  6,  7 }
    };

    fprintf(stdout, "LFHT hash to index test ...");

    fflush(stdout);

    for ( i = 0; i < 16; i++ ) {

        hash[i] = lfht_id_to_hash(i, false);
    }

    for ( index_bits = 0; index_bits < 4; index_bits++ ) {

        for ( i = 0; i < 16; i++ ) {

            index[index_bits][i] = lfht_hash_to_idx(hash[i], index_bits);
        }
    }

#if 0
    /* print tables */

    fprintf(stdout, "\n\nHASH to index by index_bits\n");
    for ( i = 0; i < 16; i++ ) {

        fprintf(stdout, "0x%013llx: ", hash[i]);

        for ( index_bits = 0; index_bits < 4; index_bits++ ) {

            fprintf(stdout, "%lld ", index[index_bits][i]);
        }

        fprintf(stdout, "\n");
    }
#endif
 
    for ( i = 0; i < 16; i++ ) {
        for ( index_bits = 0; index_bits < 4; index_bits++ ) {
            assert(index[index_bits][i] == expected_index[index_bits][i]);
        }
    }

    fprintf(stdout, " Done.\n");

    return;

} /* lfht_hash_to_index_test() */


/***********************************************************************************
 *
 * lfht_lfsll_serial_test_1()
 *
 *     Initial smoke check.  
 *
 *     Setup the hash table, but set max_index_bits to zero, which has the effect
 *     of converting the lock free hash table into a lock free singly linked list.
 *
 *     Insert a node and verify that the inserion succeeds.  Do it again and
 *     verify that it fails.
 *
 *     Search for the node just inserted, and verify that it succeeds.  Search 
 *     for a non-existant node and verify that the search fails.
 *
 *     Search for the node just inserted by value and verify that it succeeds.
 *     Search again for the value of a non-existant node and verify that it fails.
 *
 *     Do a value swap on the node just inserted -- verify that it succeeds.
 *
 *     Start an itteration on the hash table -- verify that the id and value 
 *     (after the swap) are returned.
 *
 *     Attempt to get the next node in the itteration -- verity that it fails.
 *
 *     Delete a non-existant node and verify that it fails.
 *
 *     Delete a real node and verify that it succeeds.  Do it again and verify 
 *     that it fails.
 *
 *     Check statistics, and verify that they are as expected.
 *
 *     Take down the lfsll and return.
 *
 *     Any failure should trigger an assertion.
 *
 *                                                   JRM -- 7/3/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_lfsll_serial_test_1(void)
{
    unsigned long long int id;
    void * value = NULL;
    struct lfht_t lfht;

    fprintf(stdout, "LFHT LFSLL serial test 1 ...");

    fflush(stdout);

    lfht_init(&lfht);

    /* set lfht.max_index_bits to zero -- which forces the lock free hash table 
     * to funtion as a lock free singly linked list, as it forces all entries 
     * into a single hash bucket.
     */
    lfht.max_index_bits = 0;


    /* insert 1 -- should succeed */
    assert(lfht_add(&lfht, 0x01ULL, (void *)(0x01ULL)));

    /* insert 1 again -- should fail */
    assert(!lfht_add(&lfht, 0x01ULL, (void *)(0x01ULL)));

    /* attempt to find 1 -- should succeed */
    assert(lfht_find(&lfht, 0x01ULL, &value));
    assert((void *)(0x01ULL) == value);

    /* attempt to find 2 -- should fail */
    assert(!lfht_find(&lfht, 0x02ULL, &value));

    /* Search for 1 by value -- should succeed. */
    assert(lfht_find_id_by_value(&lfht, &id, (void *)(0x01ULL)));
    assert(0x01ULL == id);

    /* Search for 2 by value -- should fail */
    assert(!lfht_find_id_by_value(&lfht, &id, (void *)(0x02ULL)));

    /* Do a value swap on 1 -- should succeed */
    assert(lfht_swap_value(&lfht, 0x01ULL, (void *)(0x011ULL), &value));
    assert((void *)(0x01ULL) == value);

    /* get first -- should succeed and return 1 with its new value */
    assert(lfht_get_first(&lfht, &id, &value));
    assert(0x01ULL == id);
    assert((void *)(0x011ULL) == value);

    /* get next -- should fail */
    assert(!lfht_get_next(&lfht, 0x01ULL, &id, &value));
 
    /* attempt to delete 2 -- should fail */
    assert(!lfht_delete(&lfht, 0x02ULL));

    /* attempt to delete 1 -- should succeed */
    assert(lfht_delete(&lfht, 0x01ULL));

    /* attempt to delete 1 -- should fail */
    assert(!lfht_delete(&lfht, 0x01ULL));

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);

    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(2 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(1 == atomic_load(&(lfht.insertions)));
    assert(1 == atomic_load(&(lfht.insertion_failures)));
    assert(0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));
    assert(0 == atomic_load(&(lfht.ins_deletion_completions)));
    assert(1 == atomic_load(&(lfht.nodes_visited_during_ins)));

    assert(3 == atomic_load(&(lfht.deletion_attempts)));
    assert(2 == atomic_load(&(lfht.deletion_failures)));
    assert(1 == atomic_load(&(lfht.deletion_starts)));
    assert(0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(1 == atomic_load(&(lfht.del_deletion_completions)));
    assert(0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));
    assert(2 == atomic_load(&(lfht.nodes_visited_during_dels)));

    assert(2 == atomic_load(&(lfht.searches)));
    assert(1 == atomic_load(&(lfht.successful_searches)));
    assert(1 == atomic_load(&(lfht.failed_searches)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_succ_searches)));
    assert(1 == atomic_load(&(lfht.unmarked_nodes_visited_in_succ_searches)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_failed_searches)));
    assert(1 == atomic_load(&(lfht.unmarked_nodes_visited_in_failed_searches)));

    assert(1 == atomic_load(&(lfht.value_swaps)));
    assert(1 == atomic_load(&(lfht.successful_val_swaps)));
    assert(0 == atomic_load(&(lfht.failed_val_swaps)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_succ_val_swaps)));
    assert(1 == atomic_load(&(lfht.unmarked_nodes_visited_in_succ_val_swaps)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_failed_val_swaps)));
    assert(0 == atomic_load(&(lfht.unmarked_nodes_visited_in_failed_val_swaps)));

    assert(2 == atomic_load(&(lfht.value_searches)));
    assert(1 == atomic_load(&(lfht.successful_val_searches)));
    assert(1 == atomic_load(&(lfht.failed_val_searches)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_val_searches)));
    assert(2 == atomic_load(&(lfht.unmarked_nodes_visited_in_val_searches)));
    assert(3 == atomic_load(&(lfht.sentinels_traversed_in_val_searches)));

    assert(1 == atomic_load(&(lfht.itter_inits)));
    assert(0 == atomic_load(&(lfht.itter_nexts)));
    assert(1 == atomic_load(&(lfht.itter_ends)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_itters)));
    assert(2 == atomic_load(&(lfht.unmarked_nodes_visited_in_itters)));
    assert(3 == atomic_load(&(lfht.sentinels_traversed_in_itters)));

    lfht_clear(&lfht);

    fprintf(stdout, " Done.\n");

    return;

} /* lfht_lfsll_serial_test_1() */


/***********************************************************************************
 *
 * lfht_lfsll_serial_test_2()
 *
 *     A more extensive smoke check.  
 *
 *     Setup the hash table, but set max_index_bits to zero, which has the effect
 *     of converting the lock free hash table into a lock free singly linked list.
 *
 *     Insert values [0,99] into the lfht's lfsll in increasing order, and verify 
 *     that the insertions succeed.
 *
 *     Delete value [0,99] from the lfht's lfsll in decreasing order, and verify 
 *     that the deletions succeed.
 *
 *     Insert values [100,199 in decreasing order, and verify that the 
 *     insertions succeed.
 *
 *     Search for values [0,199] in the lfht's lfsll in increasing order.  Verify 
 *     that the searches of [0,99] fail, and that the searches for [100,199]
 *     succeed.
 *
 *     Insert value [0,199] into the lfht's lfsll in increasing order.  Verify 
 *     that the insertions of [0,99] succeed and that the insertions of 
 *     [100,199] fail.  
 *
 *     Itterate through the hash table.  For each id found, set its value to
 *     its current value + 1000.
 *
 *     Search for the the odd values in the interval [1000,1199] in decreasing 
 *     order, and verify that the search return the odd ids in the range [0,199].
 *     Then delete the odd numbered values in the interval [0,199] in decreasing
 *     order, and verify that the deletions succeed.
 *
 *     Search for the values [0,199] in increasing order and verify that 
 *     searches for odd numbers fail and that searches for even numbers 
 *     succeed.
 *
 *     Insert the odd numbered values in the interval [0,199] in decreasing
 *     order, and verify that the insertions succeed.
 *
 *     Delete the odd numbered values in the interval [0,199] in decreasing
 *     order (a second time), and verify that the deletions succeed.
 *
 *     Delete the even numbered values in the interval [0,199] in increasing
 *     order, and verify that the deletions succeed.
 *
 *     Check statistics, and verify that they are as expected.
 *
 *     Take down the lfht and return.
 *
 *     Any failure should trigger an assertion.
 *
 *                                                   JRM -- 7/2/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_lfsll_serial_test_2(void)
{
    long long int i;
    unsigned long long int id;
    void * value;
    struct lfht_t lfht;

    fprintf(stdout, "LFHT LFSLL serial test 2 ...");

    fflush(stdout);

    lfht_init(&lfht);

    /* set lfht.max_index_bits to zero -- which forces the lock free hash table 
     * to funtion as a lock free singly linked list, as it forces all entries 
     * into a single hash bucket.
     */
    lfht.max_index_bits = 0;


    for ( i = 0; i < 100; i++ ) {

        assert(lfht_add(&lfht, i, (void *)i));
    }

    for ( i = 99; i >= 0 ; i-- ) {

        assert(lfht_delete(&lfht, i));
    }

    for ( i = 199; i > 99; i-- ) {

        assert(lfht_add(&lfht, i, (void *)i));
    }

    for ( i = 0; i < 200; i++ ) {

        if ( i < 100 ) {

            assert(!lfht_find(&lfht, i, &value));

        } else {

            assert(lfht_find(&lfht, i, &value));
            assert((void *)i == value);
        }
    }

    for ( i = 0; i < 200; i++ ) {

        if ( i < 100 ) {

            assert(lfht_add(&lfht, i, (void *)i));

        } else {

            assert(!lfht_add(&lfht, i, (void *)i));
        }
    }

    assert(lfht_get_first(&lfht, &id, &value));
    do {

        assert(lfht_swap_value(&lfht, id, (void *)(((unsigned long long int)value) + 1000ULL), &value));
        assert((void *)id == value);
    }
    while (lfht_get_next(&lfht, id, &id, &value));

    for ( i = 199; i >= 0 ; i -= 2 ) {

        assert(!lfht_find_id_by_value(&lfht, &id, (void *)(i)));
        assert(lfht_find_id_by_value(&lfht, &id, (void *)(i + 1000ULL)));
        assert(id == i);
        assert(lfht_delete(&lfht, i));
    }

    for ( i = 0; i < 200; i++ ) {

        if ( i % 2 == 1 ) {

            assert(!lfht_find(&lfht, i, &value));

        } else {

            assert(lfht_find(&lfht, i, &value));
            assert((void *)(i + 1000ULL) == value);
        }
    }

    for ( i = 199; i >= 0 ; i -= 2 ) {

        assert(lfht_add(&lfht, i, (void *)i));
    }

    for ( i = 199; i >= 0 ; i -= 2 ) {

        assert(lfht_delete(&lfht, i));
    }

    for ( i = 0; i < 200  ; i += 2 ) {

        assert(lfht_delete(&lfht, i));
    }

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);

    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(3 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(  400 == atomic_load(&(lfht.insertions)));
    assert(  100 == atomic_load(&(lfht.insertion_failures)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));
    assert(    2 == atomic_load(&(lfht.ins_deletion_completions)));
    assert(35024 == atomic_load(&(lfht.nodes_visited_during_ins)));

    assert(  400 == atomic_load(&(lfht.deletion_attempts)));
    assert(    0 == atomic_load(&(lfht.deletion_failures)));
    assert(  400 == atomic_load(&(lfht.deletion_starts)));
    assert(    0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(  397 == atomic_load(&(lfht.del_deletion_completions)));
    assert(    0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));
    assert(30901 == atomic_load(&(lfht.nodes_visited_during_dels)));

    assert(  400 == atomic_load(&(lfht.searches)));
    assert(  200 == atomic_load(&(lfht.successful_searches)));
    assert(  200 == atomic_load(&(lfht.failed_searches)));
    assert(    0 == atomic_load(&(lfht.marked_nodes_visited_in_succ_searches)));
    assert(10100 == atomic_load(&(lfht.unmarked_nodes_visited_in_succ_searches)));
    assert(   99 == atomic_load(&(lfht.marked_nodes_visited_in_failed_searches)));
    assert(15078 == atomic_load(&(lfht.unmarked_nodes_visited_in_failed_searches)));

    assert(  200 == atomic_load(&(lfht.value_swaps)));
    assert(  200 == atomic_load(&(lfht.successful_val_swaps)));
    assert(    0 == atomic_load(&(lfht.failed_val_swaps)));
    assert(    0 == atomic_load(&(lfht.marked_nodes_visited_in_succ_val_swaps)));
    assert(20100 == atomic_load(&(lfht.unmarked_nodes_visited_in_succ_val_swaps)));
    assert(    0 == atomic_load(&(lfht.marked_nodes_visited_in_failed_val_swaps)));
    assert(    0 == atomic_load(&(lfht.unmarked_nodes_visited_in_failed_val_swaps)));

    assert(  200 == atomic_load(&(lfht.value_searches)));
    assert(  100 == atomic_load(&(lfht.successful_val_searches)));
    assert(  100 == atomic_load(&(lfht.failed_val_searches)));
    assert(  405 == atomic_load(&(lfht.marked_nodes_visited_in_val_searches)));
    assert(27727 == atomic_load(&(lfht.unmarked_nodes_visited_in_val_searches)));
    assert(  300 == atomic_load(&(lfht.sentinels_traversed_in_val_searches)));

    assert(    1 == atomic_load(&(lfht.itter_inits)));
    assert(  199 == atomic_load(&(lfht.itter_nexts)));
    assert(    1 == atomic_load(&(lfht.itter_ends)));
    assert(    0 == atomic_load(&(lfht.marked_nodes_visited_in_itters)));
    assert(20300 == atomic_load(&(lfht.unmarked_nodes_visited_in_itters)));
    assert(  202 == atomic_load(&(lfht.sentinels_traversed_in_itters)));

    lfht_clear(&lfht);

    fprintf(stdout, " Done.\n");

    return;

} /* lfht_lfsll_serial_test_2() */


/***********************************************************************************
 *
 * lfht_lfsll_serial_test_3()
 *
 *     A yet more extensive smoke check.
 *
 *     Setup the hash table, but set max_index_bits to zero, which has the effect
 *     of converting the lock free hash table into a lock free singly linked list.
 *
 *     For each id in [0,9999]:
 *
 *        0) Attempt to insert the id into the LFHT's LFSLL -- should succeed
 *        1) Attempt to find the id in the LFHT's LFSLL -- should succeed
 *        2) Attempt to find the id by value in the LFHT's LFSLL -- should succeed
 *        3) Attempt to delete the id from the LFHT's LFSLL -- should succeed
 *        4) Attempt to find the id in the LFHT's LFSLL -- should fail
 *        5) Attempt to delete the id from the LFHT's LFSLL -- should fail
 *        6) Attempt to insert the id into the LFHT's LFSLL -- should succeed
 *        7) Attempt to find the id in the LFHT's LFSLL -- should succeed
 *        8) Attempt to insert the id into the LFHT's LFSLL -- should fail
 *        9) Attempt to delete the id from the LFHT's LFSLL -- should succeed
 *
 *     in the order given.  However, randomly intersperse the list of 
 *     operations on any given id with the same lists of operations on 
 *     other id.
 *
 *     Half way through the above, scan through the id's and swap the values.
 *     Then iterate through the entries in the entries in the hash table 
 *     and swap the values back.
 *
 *     Check statistics, and verify that they are as expected.
 *
 *     Take down the lfht and return.
 *
 *     Any failure should trigger an assertion.
 *
 *                                                   JRM -- 7/3/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_lfsll_serial_test_3(void)
{
    bool first_pass = true;
    long long int i;
    unsigned long long int id;
    unsigned int seed;
    int count = 0;
    int log[10000];
    void * value;
    struct timeval t;
    struct lfht_t lfht;

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT LFSLL serial test 3 (seed = 0x%x) ...", seed);

    fflush(stdout);

    lfht_init(&lfht);

    /* set lfht.max_index_bits to zero -- which forces the lock free hash table 
     * to funtion as a lock free singly linked list, as it forces all entries 
     * into a single hash bucket.
     */
    lfht.max_index_bits = 0;


    for (i = 0; i < 10000; i++)
        log[i] = 0;

    while ( count < 100000 ) {

        i = rand() % 10000;

        switch ( log[i] ) {

            case 0:
                assert(lfht_add(&lfht, i, (void *)i));
                log[i]++;
                count++;
                break;

            case 1:
                assert(lfht_find(&lfht, i, &value));
                assert((void *)i == value);
                log[i]++;
                count++;
                break;

            case 2:
                assert(lfht_find_id_by_value(&lfht, &id, (void *)i));
                assert(i == id);
                log[i]++;
                count++;
                break;

            case 3: 
                assert(lfht_delete(&lfht, i));
                log[i]++;
                count++;
                break;

            case 4:
                assert(!lfht_find(&lfht, i, &value));
                log[i]++;
                count++;
                break;

            case 5: 
                assert(!lfht_delete(&lfht, i));
                log[i]++;
                count++;
                break;

            case 6:
                assert(lfht_add(&lfht, i, (void *)i));
                log[i]++;
                count++;
                break;

            case 7:
                assert(lfht_find(&lfht, i, &value));
                assert((void *)i == value);
                log[i]++;
                count++;
                break;

            case 8:
                assert(!lfht_add(&lfht, i, (void *)i));
                log[i]++;
                count++;
                break;

            case 9: 
                assert(lfht_delete(&lfht, i));
                log[i]++;
                count++;
                break;

            default:
                /* do nothing */
                break;
        }


        /* count can be 50000 for several itterations through the while
         * loop.  Use the first_pass variable to ensure that the enclosed
         * block of code is only executed onec.
         */
        if ( ( count == 50000 ) && ( first_pass ) ) {

            int counter_1 = 0;
            int counter_2 = 0;
            int counter_3 = 0;

            first_pass = false;

            for (i = 0; i < 10000; i++) {

                if ( lfht_swap_value(&lfht, (unsigned long long int)i, (void *)(i + 10000LL), &value) ) {

                    counter_1++;
                    assert((long long int)value == i);
                } else {

                    counter_3++;
                }
            }
            assert(counter_1 + counter_3 == 10000);

            assert(lfht_get_first(&lfht, &id, &value));
            do {

                assert(lfht_swap_value(&lfht, id, (void *)(id), &value));
                assert((void *)(id + 10000ULL) == value);
                counter_2++;
            }
            while (lfht_get_next(&lfht, id, &id, &value));

            assert(counter_1 == counter_2);
            assert(counter_1 == atomic_load(&(lfht.lfsll_log_len)));
            assert(counter_3 == atomic_load(&(lfht.failed_val_swaps)));
            assert(counter_1 + counter_2 == atomic_load(&(lfht.successful_val_swaps)));
            assert(counter_1 + counter_2 + counter_3 == atomic_load(&(lfht.value_swaps)));
        }
    }

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);

    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(3 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(20000 == atomic_load(&(lfht.insertions)));
    assert(10000 == atomic_load(&(lfht.insertion_failures)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));

    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    assert(30000 == atomic_load(&(lfht.deletion_attempts)));
    assert(10000 == atomic_load(&(lfht.deletion_failures)));
    assert(20000 == atomic_load(&(lfht.deletion_starts)));
    assert(    0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(    0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));

    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert(atomic_load(&(lfht.ins_deletion_completions)) + 
           atomic_load(&(lfht.del_deletion_completions)) +
           atomic_load(&(lfht.lfsll_phys_len)) - 2 == 20000);

    assert(30000 == atomic_load(&(lfht.searches)));
    assert(20000 == atomic_load(&(lfht.successful_searches)));
    assert(10000 == atomic_load(&(lfht.failed_searches)));

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_lfsll_serial_test_3() */


/***********************************************************************************
 *
 * lfht_serial_test_1()
 *
 *     Initial smoke check.  
 *
 *     Setup the hash table.
 *
 *     Insert a node and verify that the inserion succeeds.  Do it again and
 *     verify that it fails.
 *
 *     Search for the node just inserted, and verify that it succeeds.  Search 
 *     for a non-existant node and verify that the search fails.
 *
 *     Search for the node just inserted by value and verify that it succeeds.
 *     Search again for the value of a non-existant node and verify that it fails.
 *
 *     Do a value swap on the node just inserted -- verify that it succeeds.
 *
 *     Start an itteration on the hash table -- verify that the id and value 
 *     (after the swap) are returned.
 *
 *     Attempt to get the next node in the itteration -- verity that it fails.
 *
 *     Delete a non-existant node and verify that it fails.
 *
 *     Delete a real node and verify that it succeeds.  Do it again and verify 
 *     that it fails.
 *
 *     Check statistics, and verify that they are as expected.
 *
 *     Take down the lfht and return.
 *
 *     Any failure should trigger an assertion.
 *
 *                                                   JRM -- 7/3/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_serial_test_1(void)
{
    unsigned long long int id;
    void * value = NULL;
    struct lfht_t lfht;

    fprintf(stdout, "LFHT serial test 1 ...");

    fflush(stdout);

    lfht_init(&lfht);


    /* insert 1 -- should succeed */
    assert(lfht_add(&lfht, 0x01ULL, (void *)(0x01ULL)));

    /* insert 1 again -- should fail */
    assert(!lfht_add(&lfht, 0x01ULL, (void *)(0x01ULL)));

    /* attempt to find 1 -- should succeed */
    assert(lfht_find(&lfht, 0x01ULL, &value));
    assert((void *)(0x01ULL) == value);

    /* attempt to find 2 -- should fail */
    assert(!lfht_find(&lfht, 0x02ULL, &value));

    /* Search for 1 by value -- should succeed. */
    assert(lfht_find_id_by_value(&lfht, &id, (void *)(0x01ULL)));
    assert(0x01ULL == id);

    /* Search for 2 by value -- should fail */
    assert(!lfht_find_id_by_value(&lfht, &id, (void *)(0x02ULL)));

    /* Do a value swap on 1 -- should succeed */
    assert(lfht_swap_value(&lfht, 0x01ULL, (void *)(0x011ULL), &value));
    assert((void *)(0x01ULL) == value);

    /* get first -- should succeed and return 1 with its new value */
    assert(lfht_get_first(&lfht, &id, &value));
    assert(0x01ULL == id);
    assert((void *)(0x011ULL) == value);

    /* get next -- should fail */
    assert(!lfht_get_next(&lfht, 0x01ULL, &id, &value));

    /* attempt to delete 2 -- should fail */
    assert(!lfht_delete(&lfht, 0x02ULL));

    /* attempt to delete 1 -- should succeed */
    assert(lfht_delete(&lfht, 0x01ULL));

    /* attempt to delete 1 -- should fail */
    assert(!lfht_delete(&lfht, 0x01ULL));

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);

    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(2 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(1 == atomic_load(&(lfht.insertions)));
    assert(1 == atomic_load(&(lfht.insertion_failures)));
    assert(0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));
    assert(0 == atomic_load(&(lfht.ins_deletion_completions)));
    assert(1 == atomic_load(&(lfht.nodes_visited_during_ins)));

    assert(3 == atomic_load(&(lfht.deletion_attempts)));
    assert(2 == atomic_load(&(lfht.deletion_failures)));
    assert(1 == atomic_load(&(lfht.deletion_starts)));
    assert(0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(1 == atomic_load(&(lfht.del_deletion_completions)));
    assert(0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));
    assert(2 == atomic_load(&(lfht.nodes_visited_during_dels)));

    assert(2 == atomic_load(&(lfht.searches)));
    assert(1 == atomic_load(&(lfht.successful_searches)));
    assert(1 == atomic_load(&(lfht.failed_searches)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_succ_searches)));
    assert(1 == atomic_load(&(lfht.unmarked_nodes_visited_in_succ_searches)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_failed_searches)));
    assert(1 == atomic_load(&(lfht.unmarked_nodes_visited_in_failed_searches)));

    assert(1 == atomic_load(&(lfht.value_swaps)));
    assert(1 == atomic_load(&(lfht.successful_val_swaps)));
    assert(0 == atomic_load(&(lfht.failed_val_swaps)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_succ_val_swaps)));
    assert(1 == atomic_load(&(lfht.unmarked_nodes_visited_in_succ_val_swaps)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_failed_val_swaps)));
    assert(0 == atomic_load(&(lfht.unmarked_nodes_visited_in_failed_val_swaps)));

    assert(2 == atomic_load(&(lfht.value_searches)));
    assert(1 == atomic_load(&(lfht.successful_val_searches)));
    assert(1 == atomic_load(&(lfht.failed_val_searches)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_val_searches)));
    assert(2 == atomic_load(&(lfht.unmarked_nodes_visited_in_val_searches)));
    assert(3 == atomic_load(&(lfht.sentinels_traversed_in_val_searches)));

    assert(1 == atomic_load(&(lfht.itter_inits)));
    assert(0 == atomic_load(&(lfht.itter_nexts)));
    assert(1 == atomic_load(&(lfht.itter_ends)));
    assert(0 == atomic_load(&(lfht.marked_nodes_visited_in_itters)));
    assert(2 == atomic_load(&(lfht.unmarked_nodes_visited_in_itters)));
    assert(3 == atomic_load(&(lfht.sentinels_traversed_in_itters)));

    lfht_clear(&lfht);

    fprintf(stdout, " Done.\n");

    return;

} /* lfht_serial_test_1() */


/***********************************************************************************
 *
 * lfht_serial_test_2()
 *
 *     A more extensive smoke check.  
 *
 *     Setup the hash table.
 *
 *     Insert values [0,99] into the lfht in increasing order, and verify 
 *     that the insertions succeed.
 *
 *     Delete value [0,99] from the lfht in decreasing order, and verify 
 *     that the deletions succeed.
 *
 *     Insert values [100,199 in decreasing order, and verify that the 
 *     insertions succeed.
 *
 *     Search for values [0,199] in the lfht in increasing order.  Verify 
 *     that the searches of [0,99] fail, and that the searches for [100,199]
 *     succeed.
 *
 *     Insert value [0,199] into the lfht in increasing order.  Verify 
 *     that the insertions of [0,99] succeed and that the insertions of 
 *     [100,199] fail.  
 *
 *     Itterate through the hash table.  For each id found, set its value to
 *     its current value + 1000.
 *
 *     Search for the the odd values in the interval [1000,1199] in decreasing 
 *     order, and verify that the search return the odd ids in the range [0,199].
 *     Then delete the odd numbered values in the interval [0,199] in decreasing
 *     order, and verify that the deletions succeed.
 *
 *     Search for the values [0,199] in increasing order and verify that 
 *     searches for odd numbers fail and that searches for even numbers 
 *     succeed.
 *
 *     Insert the odd numbered values in the interval [0,199] in decreasing
 *     order, and verify that the insertions succeed.
 *
 *     Delete the odd numbered values in the interval [0,199] in decreasing
 *     order (a second time), and verify that the deletions succeed.
 *
 *     Delete the even numbered values in the interval [0,199] in increasing
 *     order, and verify that the deletions succeed.
 *
 *     Check statistics, and verify that they are as expected.
 *
 *     Take down the lfht and return.
 *
 *     Any failure should trigger an assertion.
 *
 *                                                   JRM -- 7/2/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_serial_test_2(void)
{
    long long int i;
    unsigned long long int id;
    void * value;
    struct lfht_t lfht;

    fprintf(stdout, "LFHT serial test 2 ...");

    fflush(stdout);

    lfht_init(&lfht);


    for ( i = 0; i < 100; i++ ) {

        assert(lfht_add(&lfht, i, (void *)i));
    }

    for ( i = 99; i >= 0 ; i-- ) {

        assert(lfht_delete(&lfht, i));
    }

    for ( i = 199; i > 99; i-- ) {

        assert(lfht_add(&lfht, i, (void *)i));
    }

    for ( i = 0; i < 200; i++ ) {

        if ( i < 100 ) {

            assert(!lfht_find(&lfht, i, &value));

        } else {

            assert(lfht_find(&lfht, i, &value));
            assert((void *)i == value);
        }
    }

    for ( i = 0; i < 200; i++ ) {

        if ( i < 100 ) {

            assert(lfht_add(&lfht, i, (void *)i));

        } else {

            assert(!lfht_add(&lfht, i, NULL));
        }
    }

    assert(lfht_get_first(&lfht, &id, &value));
    do {

        assert(lfht_swap_value(&lfht, id, (void *)(((unsigned long long int)value) + 1000ULL), &value));
        assert((void *)id == value);
    }
    while (lfht_get_next(&lfht, id, &id, &value));

    for ( i = 199; i >= 0 ; i -= 2 ) {

        assert(!lfht_find_id_by_value(&lfht, &id, (void *)(i)));
        assert(lfht_find_id_by_value(&lfht, &id, (void *)(i + 1000ULL)));
        assert(id == i);
        assert(lfht_delete(&lfht, i));
    }

    for ( i = 0; i < 200; i++ ) {

        if ( i % 2 == 1 ) {

            assert(!lfht_find(&lfht, i, &value));

        } else {

            assert(lfht_find(&lfht, i, &value));
            assert((void *)(i + 1000ULL) == value);
        }
    }

    for ( i = 199; i >= 0 ; i -= 2 ) {

        assert(lfht_add(&lfht, i, NULL));
    }

    for ( i = 199; i >= 0 ; i -= 2 ) {

        assert(lfht_delete(&lfht, i));
    }

    for ( i = 0; i < 200  ; i += 2 ) {

        assert(lfht_delete(&lfht, i));
    }

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);

    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert( 0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(65 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(  400 == atomic_load(&(lfht.insertions)));
    assert(  100 == atomic_load(&(lfht.insertion_failures)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));
    assert(   32 == atomic_load(&(lfht.ins_deletion_completions)));
    assert( 1389 == atomic_load(&(lfht.nodes_visited_during_ins)));

    assert(  400 == atomic_load(&(lfht.deletion_attempts)));
    assert(    0 == atomic_load(&(lfht.deletion_failures)));
    assert(  400 == atomic_load(&(lfht.deletion_starts)));
    assert(    0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(  336 == atomic_load(&(lfht.del_deletion_completions)));
    assert(    0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));
    assert( 1344 == atomic_load(&(lfht.nodes_visited_during_dels)));

    assert(  400 == atomic_load(&(lfht.searches)));
    assert(  200 == atomic_load(&(lfht.successful_searches)));
    assert(  200 == atomic_load(&(lfht.failed_searches)));
    assert(    0 == atomic_load(&(lfht.marked_nodes_visited_in_succ_searches)));
    assert(  728 == atomic_load(&(lfht.unmarked_nodes_visited_in_succ_searches)));
    assert(   84 == atomic_load(&(lfht.marked_nodes_visited_in_failed_searches)));
    assert(  440 == atomic_load(&(lfht.unmarked_nodes_visited_in_failed_searches)));

    assert(  200 == atomic_load(&(lfht.value_swaps)));
    assert(  200 == atomic_load(&(lfht.successful_val_swaps)));
    assert(    0 == atomic_load(&(lfht.failed_val_swaps)));
    assert(    0 == atomic_load(&(lfht.marked_nodes_visited_in_succ_val_swaps)));
    assert(  728 == atomic_load(&(lfht.unmarked_nodes_visited_in_succ_val_swaps)));
    assert(    0 == atomic_load(&(lfht.marked_nodes_visited_in_failed_val_swaps)));
    assert(    0 == atomic_load(&(lfht.unmarked_nodes_visited_in_failed_val_swaps)));

    assert(  200 == atomic_load(&(lfht.value_searches)));
    assert(  100 == atomic_load(&(lfht.successful_val_searches)));
    assert(  100 == atomic_load(&(lfht.failed_val_searches)));
    assert( 2948 == atomic_load(&(lfht.marked_nodes_visited_in_val_searches)));
    assert(27727 == atomic_load(&(lfht.unmarked_nodes_visited_in_val_searches)));
    assert( 5744 == atomic_load(&(lfht.sentinels_traversed_in_val_searches)));

    assert(    1 == atomic_load(&(lfht.itter_inits)));
    assert(  199 == atomic_load(&(lfht.itter_nexts)));
    assert(    1 == atomic_load(&(lfht.itter_ends)));
    assert(    0 == atomic_load(&(lfht.marked_nodes_visited_in_itters)));
    assert(  928 == atomic_load(&(lfht.unmarked_nodes_visited_in_itters)));
    assert(  233 == atomic_load(&(lfht.sentinels_traversed_in_itters)));


    lfht_clear(&lfht);

    fprintf(stdout, " Done.\n");

    return;

} /* lfht_serial_test_2() */


/***********************************************************************************
 *
 * lfht_serial_test_3()
 *
 *     A yet more extensive smoke check.
 *
 *     Setup the hash table.
 *
 *     For each id in [0,9999]:
 *
 *        0) Attempt to insert the id into the LFHTL -- should succeed
 *        1) Attempt to find the id in the LFHT -- should succeed
 *        2) Attempt to find the id by value in the LFHT -- should succeed
 *        3) Attempt to delete the id from the LFHT -- should succeed
 *        4) Attempt to find the id in the LFHT -- should fail
 *        5) Attempt to delete the id from the LFHT -- should fail
 *        6) Attempt to insert the id into the LFHT -- should succeed
 *        7) Attempt to find the id in the LFHT -- should succeed
 *        8) Attempt to insert the id into the LFHT -- should fail
 *        9) Attempt to delete the id from the LFHT -- should succeed
 *
 *     in the order given.  However, randomly intersperse the list of 
 *     operations on any given id with the same lists of operations on 
 *     other id.
 *
 *     Half way through the above, scan through the id's and swap the values.
 *     Then iterate through the entries in the entries in the hash table 
 *     and swap the values back.
 *
 *     Check statistics, and verify that they are as expected.
 *
 *     Take down the lfht and return.
 *
 *     Any failure should trigger an assertion.
 *
 *                                                   JRM -- 7/3/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_serial_test_3(void)
{
    bool first_pass = true;
    long long int i;
    unsigned long long int id;
    unsigned int seed;
    int count = 0;
    int log[10000];
    void * value;
    struct timeval t;
    struct lfht_t lfht;

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT serial test 3 (seed = 0x%x) ...", seed);

    fflush(stdout);

    lfht_init(&lfht);


    for (i = 0; i < 10000; i++)
        log[i] = 0;

    while ( count < 100000 ) {

        i = rand() % 10000;

        switch ( log[i] ) {

            case 0:
                assert(lfht_add(&lfht, i, (void *)i));
                log[i]++;
                count++;
                break;

            case 1:
                assert(lfht_find(&lfht, i, &value));
                assert((void *)i == value);
                log[i]++;
                count++;
                break;

            case 2:
                assert(lfht_find_id_by_value(&lfht, &id, (void *)i));
                assert(i == id);
                log[i]++;
                count++;
                break;

            case 3: 
                assert(lfht_delete(&lfht, i));
                log[i]++;
                count++;
                break;

            case 4:
                assert(!lfht_find(&lfht, i, &value));
                log[i]++;
                count++;
                break;

            case 5: 
                assert(!lfht_delete(&lfht, i));
                log[i]++;
                count++;
                break;

            case 6:
                assert(lfht_add(&lfht, i, (void *)i));
                log[i]++;
                count++;
                break;

            case 7:
                assert(lfht_find(&lfht, i, &value));
                assert((void *)i == value);
                log[i]++;
                count++;
                break;

            case 8:
                assert(!lfht_add(&lfht, i, (void *)i));
                log[i]++;
                count++;
                break;

            case 9: 
                assert(lfht_delete(&lfht, i));
                log[i]++;
                count++;
                break;

            default:
                /* do nothing */
                break;
        }

        /* count can be 50000 for several itterations through the while
         * loop.  Use the first_pass variable to ensure that the enclosed
         * block of code is only executed onec.
         */
        if ( ( count == 50000 ) && ( first_pass ) ) {

            int counter_1 = 0;
            int counter_2 = 0;
            int counter_3 = 0;

            first_pass = false;

            for (i = 0; i < 10000; i++) {

                if ( lfht_swap_value(&lfht, (unsigned long long int)i, (void *)(i + 10000LL), &value) ) {

                    counter_1++;
                    assert((long long int)value == i);
                } else {

                    counter_3++;
                }
            }
            assert(counter_1 + counter_3 == 10000);

            assert(lfht_get_first(&lfht, &id, &value));
            do {

                assert(lfht_swap_value(&lfht, id, (void *)(id), &value));
                assert((void *)(id + 10000ULL) == value);
                counter_2++;
            }
            while (lfht_get_next(&lfht, id, &id, &value));

            assert(counter_1 == counter_2);
            assert(counter_1 == atomic_load(&(lfht.lfsll_log_len)));
            assert(counter_3 == atomic_load(&(lfht.failed_val_swaps)));
            assert(counter_1 + counter_2 == atomic_load(&(lfht.successful_val_swaps)));
            assert(counter_1 + counter_2 + counter_3 == atomic_load(&(lfht.value_swaps)));
        }
    }

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);

    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(   0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(2049 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(20000 == atomic_load(&(lfht.insertions)));
    assert(10000 == atomic_load(&(lfht.insertion_failures)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));

    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    assert(30000 == atomic_load(&(lfht.deletion_attempts)));
    assert(10000 == atomic_load(&(lfht.deletion_failures)));
    assert(20000 == atomic_load(&(lfht.deletion_starts)));
    assert(    0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(    0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));

    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert(atomic_load(&(lfht.ins_deletion_completions)) + 
           atomic_load(&(lfht.del_deletion_completions)) +
           atomic_load(&(lfht.lfsll_phys_len)) - 
           atomic_load(&(lfht.buckets_initialized)) - 1 == 20000);

    assert(30000 == atomic_load(&(lfht.searches)));
    assert(20000 == atomic_load(&(lfht.successful_searches)));
    assert(10000 == atomic_load(&(lfht.failed_searches)));

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_serial_test_3() */


/***********************************************************************************
 *
 * struct lfht_mt_test_params_t
 *
 * Structure used to pass control information into and results out of LFHT 
 * multi-thread test functions.  The individual fields in this structure are 
 * discussed below.
 *
 * lfht_ptr:    Pointer of the instance of lfht_t that forms the root of the 
 *              target LFHT
 *
 * start_id:    When scanning through a list of ids and performing a set 
 *              of operations on each element of this list, this is the 
 *              first id, which must be non-negative.
 *
 * step:        When scanning through a list of ids and performing a set
 *              of operations on each element of this list, this is the difference
 *              between each id in the list.  While the absolute value of 
 *              step must be greater than or equal to 1, step may be either 
 *              positive or negative.  
 *
 *              Note, however, that the resulting ids must be non-negative.
 *
 *              Not always used.
 *
 * num_vals:    When scanning through a list of ids and performing a set
 *              of operations on each element of this list, this is the number
 *              of ids in the list.
 *
 * itterations: Number of times the test function is to repeat.  
 *
 *              Not always used.
 *
 * ins_fails:   Long long int used to report the number of failed insertions
 *              reported by lfht_add().
 *
 * del_fails:   Long long int used to report the number of failed deletions 
 *              reported by lfht_delete().
 *
 * search_fails: Long long int used to report the number of failed searches
 *              reported by lfht_find().  
 *
 * search_by_val_fails: Long long int used to report the number of failed searches
 *              by value reported by lfht_find_id_by_value().
 *
 * swap_val_fails: Long long int used to report the number of faild value swaps
 *              reported by lfht_swap_value().
 *
 * ins_successes: Long long int used to report the number of successful insertions
 *              reported by lfht_add().  
 *
 *              Not always used.
 *
 * del_successes: Long long int used to report the number of successful deletions 
 *              reported by lfht_delete().  
 *
 *              Not always used.
 *
 * search_successes: Long long int used to report the number of successful searches
 *              reported by lfht_find().  
 *
 *              Not always used.
 *
 * search_by_val_successes: Long long int used to report the number of successful 
 *              searches by value reported by lfht_find_id_by_value().  
 *
 *              Not always used.
 *
 * swap_val_successes: Long long int used to report the number of successful 
 *              value swaps reported by lfht_swap_value().
 *
 *              Not always used.
 *
 * itter_inits: Long long int used to report the number of calls to lfht_get_first().
 *
 * itter_nexts: Long long int used to report the number of calls to lfht_get_next()
 *              that return true.
 *
 * itter_ends:  Long long int used to report the number of calls to lfht_get_next()
 *              that return false.
 *
 ***********************************************************************************/

struct lfht_mt_test_params_t {

    struct lfht_t *lfht_ptr;

    unsigned long long int start_id;
    long long int step;
    unsigned long long int num_ids;
    unsigned long long int itterations;

    long long int ins_fails;
    long long int del_fails;
    long long int search_fails;
    long long int search_by_val_fails;
    long long int swap_val_fails;

    long long int ins_successes;
    long long int del_successes;
    long long int search_successes;
    long long int search_by_val_successes;
    long long int swap_val_successes;

    long long int itter_inits;
    long long int itter_nexts;
    long long int itter_ends;

} lfht_mt_test_params_t;


/***********************************************************************************
 *
 * lfht_mt_test_fcn_1()
 *
 *     This function is intended to be executed by one or more threads 
 *     in a LFHT multi-thread test.
 *
 *     For each id (params_ptr->start_id + n * params_ptr->step) whern 
 *     0 <= n < params_ptr->num_ids,
 *
 *        0) Attempt to insert the id into the LFHT 
 *        1) Attempt to find the id in the LFHT
 *        2) Attempt to find the id by value in the LFHT
 *           This is a very expensive operation, so only 
 *           do it in one case in 32.
 *        3) Attempt to delete the id from the LFHT
 *        4) Attempt to find the id in the LFHT
 *        5) Attempt to delete the id from the LFHT
 *        6) Attempt to insert the id into the LFHT
 *        7) Attempt to find the id in the LFHT
 *        8) Attempt to insert the id into the LFHT
 *        9) Attempt to delete the id from the LFHT
 *
 *     in the order given.  However, randomly intersperse the list of 
 *     operations on any given id with the same lists of operations on 
 *     other ids.
 *
 *     Note that at present, params_ptr->num_ids may not exceed 10,000.
 *
 *     Failed insertions, deletions, searches, and searches by value are counted, 
 *     and the counts returned in params_ptr->ins_fails, params_ptr->del_fails, 
 *     params_ptr->search_fails, and params_ptr->search_by_val_fails.
 *
 *
 *                                                   JRM -- 6/18/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void * lfht_mt_test_fcn_1(void * args )
{
    struct lfht_mt_test_params_t * params_ptr;
    bool first_pass = true;
    long long int i;
    long long int ins_fails = 0;
    long long int del_fails = 0;
    long long int search_fails = 0;
    long long int search_by_val_fails = 0;
    long long int swap_val_fails = 0;
    long long int swap_val_successes = 0;
    long long int itter_inits = 0;
    long long int itter_nexts = 0;
    long long int itter_ends = 0;
    unsigned long long int val_swap_offset = 1000000;
    unsigned long long int id;
    unsigned long long int new_id;
    int count = 0;
    int log[10000];
    void * value;

    params_ptr = (struct lfht_mt_test_params_t *)args;

    assert(params_ptr);
    assert(params_ptr->lfht_ptr);
    assert(LFHT_VALID == params_ptr->lfht_ptr->tag);
    assert((params_ptr->step >= 1) || ( params_ptr->step <= -1));
    assert((params_ptr->num_ids > 0) && (params_ptr->num_ids <= 10000));

    for (i = 0; i < 10000; i++)
        log[i] = 0;

    while ( count < 10 * params_ptr->num_ids ) {

        i = rand() % params_ptr->num_ids;

        id = params_ptr->start_id + (i * params_ptr->step);

        assert(0 <= id);

        switch ( log[i] ) {

            case 0:
                if ( ! lfht_add(params_ptr->lfht_ptr, id, (void *)id) ) {

                    ins_fails++;
                }
                log[i]++;
                count++;
                break;

            case 1:
                
                if ( ! lfht_find(params_ptr->lfht_ptr, id, &value) ) {

                    search_fails++;

                } else {

                    assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));
                }
                log[i]++;
                count++;
                break;

            case 2:
                /* lfht_find_id_by_value() is very expensive, so only do it roughly 
                 * one time in 32.
                 */
                if ( 0 == (rand() & 0x1F) ) {

                    if ( ! lfht_find_id_by_value(params_ptr->lfht_ptr, &new_id, (void *)id) ) {

                        search_by_val_fails++;

                    } else {

                        assert(new_id == id);
                    }
                }
                log[i]++;
                count++;
                break;

            case 3: 
                if ( ! lfht_delete(params_ptr->lfht_ptr, id) ) {

                    del_fails++;
                }
                log[i]++;
                count++;
                break;

            case 4:
                if ( ! lfht_find(params_ptr->lfht_ptr, id, &value) ) {

                    search_fails++;

                } else {
 
                    assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));
                }
                log[i]++;
                count++;
                break;

            case 5: 
                if ( ! lfht_delete(params_ptr->lfht_ptr, id) ) {

                    del_fails++;
                }
                log[i]++;
                count++;
                break;

            case 6:
                if ( ! lfht_add(params_ptr->lfht_ptr, id, (void *)id) ) {

                    ins_fails++;
                }
                log[i]++;
                count++;
                break;

            case 7:
                if ( ! lfht_find(params_ptr->lfht_ptr, id, &value) ) {

                    search_fails++;

                } else {

                    assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));
                }
                log[i]++;
                count++;
                break;

            case 8:
                if ( ! lfht_add(params_ptr->lfht_ptr, id, (void *)id) ) {

                    ins_fails++;
                }
                log[i]++;
                count++;
                break;

            case 9: 
                if ( ! lfht_delete(params_ptr->lfht_ptr, id) ) {

                   del_fails++;
                }
                log[i]++;
                count++;
                break;

            default:
                /* do nothing */
                break;
        }

        /* count can be 50000 for several itterations through the while
         * loop.  Use the first_pass variable to ensure that the enclosed
         * block of code is only executed onec.
         */
        if ( ( count == 50000 ) && ( first_pass ) ) {

            first_pass = false;
            assert(0 == swap_val_successes);
            assert(0 == swap_val_fails);
            for (i = 0; i < 10000; i++) {

                id = params_ptr->start_id + (i * params_ptr->step);

                if ( lfht_swap_value(params_ptr->lfht_ptr, id, 
                                     (void *)(id + val_swap_offset), &value) ) {

                    swap_val_successes++;
                    assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));

                } else {

                    swap_val_fails++;
                }
            }
            assert(swap_val_successes + swap_val_fails == 10000LL);

            itter_inits++;
            if ( lfht_get_first(params_ptr->lfht_ptr, &id, &value) ) {

                if ( lfht_swap_value(params_ptr->lfht_ptr, id, (void *)(id), &value) ) {

                    swap_val_successes++;
                    assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));

                } else {

                    swap_val_fails++;
                }

                while (lfht_get_next(params_ptr->lfht_ptr, id, &id, &value)) {

                    if ( lfht_swap_value(params_ptr->lfht_ptr, id, (void *)(id), &value) ) {

                        swap_val_successes++;
                        assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));

                    } else {

                        swap_val_fails++;
                    }
                    itter_nexts++;
                }
            }
            itter_ends++;
        }
    }

    params_ptr->ins_fails           = ins_fails;
    params_ptr->del_fails           = del_fails;
    params_ptr->search_fails        = search_fails;
    params_ptr->search_by_val_fails = search_by_val_fails;
    params_ptr->swap_val_fails      = swap_val_fails;

    params_ptr->itter_inits         = itter_inits;
    params_ptr->itter_nexts         = itter_nexts;
    params_ptr->itter_ends          = itter_ends;

    return(NULL);

} /* lfht_mt_test_fcn_1() */


/***********************************************************************************
 *
 * lfht_mt_test_fcn_2()
 *
 *     This function is intended to be executed by one or more threads 
 *     in a LFHT multi-thread test.
 *
 *     Proceed as follows:
 *
 *     1) pick a random id in the supplied range.
 *
 *     2) Pick a random number in the range [0.99].
 *
 *        If it is in [0,3], attempt to insert the random id in the LFHT
 *
 *        If it is in [4,7], attempt to delete the random id from the LFHT
 *
 *        If it is 8, search for a random value in the LFHT
 *
 *        If it is 9, attempt to modify the value of a random id in the LFHT
 *
 *        Otherwise, search for the random id in the LFHT.
 *
 *        Record the results in *params_ptr.
 *
 *     3) Pick a random number in the range [0, params_ptr->itterations - 1].  On 
 *        this itteration, operate as above, but after that operation is complete,
 *        itterate through all entries in the LFHT, and verify that their values 
 *        are as expected.
 *
 *     Repeat until the specified number of operations have been attempted.
 *
 *                                                   JRM -- 6/27/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void * lfht_mt_test_fcn_2(void * args )
{
    struct lfht_mt_test_params_t * params_ptr;
    int operation;
    long long int i = 0;
    long long int itterateration_pass;
    long long int ins_fails = 0;
    long long int del_fails = 0;
    long long int search_fails = 0;
    long long int search_by_val_fails = 0;
    long long int swap_val_fails = 0;
    long long int ins_successes = 0;
    long long int del_successes = 0;
    long long int search_successes = 0;
    long long int search_by_val_successes = 0;
    long long int swap_val_successes = 0;
    long long int itter_inits = 0;
    long long int itter_nexts = 0;
    long long int itter_ends = 0;
    long long int id;
    long long int test_id;
    unsigned long long int val_swap_offset = 1000000;
    int count = 0;
    int log[10000];
    void * value = NULL;

    params_ptr = (struct lfht_mt_test_params_t *)args;

    assert(params_ptr);
    assert(params_ptr->lfht_ptr);
    assert(LFHT_VALID == params_ptr->lfht_ptr->tag);
    assert(params_ptr->num_ids > 0);
    assert(params_ptr->itterations > 0);

    itterateration_pass = (long long int)(rand() % params_ptr->itterations);
    assert(0 <= itterateration_pass);
    assert(itterateration_pass < params_ptr->itterations);

    while ( i < params_ptr->itterations ) {

        id = (long long int)((rand() % params_ptr->num_ids) + params_ptr->start_id);

        operation = rand() % 100;

        switch ( operation ) {

            case 0:
            case 1:
            case 2: 
            case 3: /* insert value */
                if ( lfht_add(params_ptr->lfht_ptr, id, (void *)id) ) {

                    ins_successes++;

                } else {

                    ins_fails++;
                }
                break;

            case 4:
            case 5:
            case 6:
            case 7: /* delete value */
                if ( lfht_delete(params_ptr->lfht_ptr, id) ) {

                    del_successes++;

                } else {

                    del_fails++;
                }
                break;

            case 8: /* search by value */
                if ( lfht_find_id_by_value(params_ptr->lfht_ptr, &test_id, (void *)id) ) {

                    search_by_val_successes++;
                    assert(test_id == id);

                } else {

                    search_by_val_fails++;
                }
                break;

            case 9: /* swap value */
                if ( lfht_swap_value(params_ptr->lfht_ptr, id, 
                                     (void *)(id + val_swap_offset), &value) ) {

                    swap_val_successes++;
                    assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));

                } else {

                    swap_val_fails++;
                }
                break;

            default:
                if ( lfht_find(params_ptr->lfht_ptr, id, &value) ) {

                    search_successes++;

                    assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));

                } else {

                    search_fails++;
                }
                break;
        }

        if ( i == itterateration_pass ) {

            itter_inits++;
            if ( lfht_get_first(params_ptr->lfht_ptr, &id, &value) ) {

                assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));

                while (lfht_get_next(params_ptr->lfht_ptr, id, &id, &value)) {

                    assert(((void *)id == value) || ((void *)(id + val_swap_offset) == value));
                    itter_nexts++;
                }
            }

            itter_ends++;
        }

        i++;
    } 

    params_ptr->ins_successes           = ins_successes;
    params_ptr->ins_fails               = ins_fails;

    params_ptr->del_successes           = del_successes;
    params_ptr->del_fails               = del_fails;

    params_ptr->search_successes        = search_successes;
    params_ptr->search_fails            = search_fails;

    params_ptr->search_by_val_successes = search_by_val_successes;
    params_ptr->search_by_val_fails     = search_by_val_fails;

    params_ptr->swap_val_successes      = swap_val_successes;
    params_ptr->swap_val_fails          = swap_val_fails;

    params_ptr->itter_inits             = itter_inits;
    params_ptr->itter_nexts             = itter_nexts;
    params_ptr->itter_ends              = itter_ends;

    return(NULL);

} /* lfht_mt_test_fcn_2() */


/***********************************************************************************
 *
 * lfht_lfsll_mt_test_fcn_1__serial_test()
 *
 *     Serial test of lfht_mt_test_fcn_1() with the lock free hash table 
 *     configured to function as a lock free singly linked list.
 *
 *                                                   JRM -- 6/19/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_lfsll_mt_test_fcn_1__serial_test()
{
    unsigned int seed;
    int count = 0;
    int log[10000];
    struct timeval t;
    struct lfht_t lfht;
    struct lfht_mt_test_params_t params = {
        /* lfht_ptr                = */ NULL,
        /* start_id                = */ 50000LL,
        /* step                    = */ -3LL,
        /* num_ids                 = */ 10000LL,
        /* iterations              = */ 0,   /* not used in this case */
        /* ins_fails               = */ 0LL,
        /* del_fails               = */ 0LL,
        /* search_fails            = */ 0LL,
        /* search_by_val_fails     = */ 0LL,
        /* swap_val_fails          = */ 0LL,
        /* ins_successes           = */ 0LL, /* not used in this case */
        /* del_successes           = */ 0LL, /* not used in this case */
        /* search_successes        = */ 0LL, /* not used in this case */
        /* search_by_val_successes = */ 0LL, /* not used in this case */
        /* swap_val_successes      = */ 0LL, /* not used in this case */
        /* itter_inits             = */ 0LL,
        /* itter_nexts             = */ 0LL,
        /* itter_ends              = */ 0LL
    };


    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT LFSLL serial test of lfht_mt_test_fcn_1 (seed = 0x%x) ...", seed);

    fflush(stdout);

    lfht_init(&lfht);

    /* set lfht.max_index_bits to zero -- which forces the lock free hash table
     * to funtion as a lock free singly linked list, as it forces all entries
     * into a single hash bucket.
     */
    lfht.max_index_bits = 0;

    params.lfht_ptr = &lfht;

    lfht_mt_test_fcn_1((void *)(&params));

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);

    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(params.ins_fails == atomic_load(&(lfht.insertion_failures)));
    assert(params.del_fails == atomic_load(&(lfht.deletion_failures)));
    assert(params.search_fails == atomic_load(&(lfht.failed_searches)));
    assert(params.search_by_val_fails == atomic_load(&(lfht.failed_val_searches)));
    assert(params.swap_val_fails == atomic_load(&(lfht.failed_val_swaps)));

    assert(params.itter_inits == atomic_load(&(lfht.itter_inits)));
    assert(params.itter_nexts == atomic_load(&(lfht.itter_nexts)));
    assert(params.itter_ends == atomic_load(&(lfht.itter_ends)));

    assert(0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(3 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(20000 == atomic_load(&(lfht.insertions)));
    assert(10000 == atomic_load(&(lfht.insertion_failures)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));

    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    assert(30000 == atomic_load(&(lfht.deletion_attempts)));
    assert(10000 == atomic_load(&(lfht.deletion_failures)));
    assert(20000 == atomic_load(&(lfht.deletion_starts)));
    assert(    0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(    0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));

    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert(atomic_load(&(lfht.ins_deletion_completions)) + 
           atomic_load(&(lfht.del_deletion_completions)) +
           atomic_load(&(lfht.lfsll_phys_len)) - 2 == 20000);

    assert(30000 == atomic_load(&(lfht.searches)));
    assert(20000 == atomic_load(&(lfht.successful_searches)));
    assert(10000 == atomic_load(&(lfht.failed_searches)));

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */
    assert(10000 == (atomic_load(&(lfht.successful_val_swaps)) / 2) + 
                    atomic_load(&(lfht.failed_val_swaps)));

    assert(1 == params.itter_inits);
    assert(params.itter_nexts + 1 == (atomic_load(&(lfht.successful_val_swaps)) / 2));
    assert(1 == params.itter_ends);

    assert(0 == params.search_by_val_fails);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_lfsll_mt_test_fcn_1__serial_test() */


/***********************************************************************************
 *
 * lfht_lfsll_mt_test_fcn_2__serial_test()
 *
 *     Serial test of lfht_mt_test_fcn_2() with the lock free hash table 
 *     configured to function as a lock free singly linked list.
 *
 *                                                   JRM -- 6/28/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_lfsll_mt_test_fcn_2__serial_test()
{
    unsigned int seed;
    int count = 0;
    int log[10000];
    struct timeval t;
    struct lfht_t lfht;
    struct lfht_mt_test_params_t params = {
        /* lfht_ptr                = */ NULL,
        /* start_id                = */ 0LL,
        /* step                    = */ 0LL, /* not used in this case */
        /* num_ids                 = */ 10000LL,
        /* iterations              = */ 1000000LL,
        /* ins_fails               = */ 0LL,
        /* del_fails               = */ 0LL,
        /* search_fails            = */ 0LL,
        /* search_by_val_fails     = */ 0LL,
        /* swap_val_fails          = */ 0LL,
        /* ins_successes           = */ 0LL, 
        /* del_successes           = */ 0LL,
        /* search_successes        = */ 0LL,
        /* search_by_val_successes = */ 0LL,
        /* swap_val_successes      = */ 0LL, 
        /* itter_inits             = */ 0LL,
        /* itter_nexts             = */ 0LL,
        /* itter_ends              = */ 0LL
    };

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT LFSLL serial test of lfht_mt_test_fcn_2 (seed = 0x%x) ...", seed);

    fflush(stdout);

    lfht_init(&lfht);

    /* set lfht.max_index_bits to zero -- which forces the lock free hash table
     * to funtion as a lock free singly linked list, as it forces all entries
     * into a single hash bucket.
     */
    lfht.max_index_bits = 0;

    params.lfht_ptr = &lfht;

    lfht_mt_test_fcn_2((void *)(&params));

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);
#endif /* JRM */

#if 0 /* JRM */
    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(params.ins_fails           == atomic_load(&(lfht.insertion_failures)));
    assert(params.del_fails           == atomic_load(&(lfht.deletion_failures)));
    assert(params.search_fails        == atomic_load(&(lfht.failed_searches)));
    assert(params.search_by_val_fails == atomic_load(&(lfht.failed_val_searches)));
    assert(params.swap_val_fails      == atomic_load(&(lfht.failed_val_swaps)));

    assert(params.ins_successes           == atomic_load(&(lfht.insertions)));
    assert(params.del_successes           == atomic_load(&(lfht.deletion_starts)));
    assert(params.search_successes        == atomic_load(&(lfht.successful_searches)));
    assert(params.search_by_val_successes == atomic_load(&(lfht.successful_val_searches)));
    assert(params.swap_val_successes      == atomic_load(&(lfht.successful_val_swaps)));

    assert(params.itter_inits == atomic_load(&(lfht.itter_inits)));
    assert(params.itter_nexts == atomic_load(&(lfht.itter_nexts)));
    assert(params.itter_ends  == atomic_load(&(lfht.itter_ends)));


    /* lfht.log_len & lfht.phys_len will vary */

    assert((atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))) ==
           (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));

    /* lfht.insertions & lfht.insertion_failures will vary */

    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));

    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    /* lfht.deletion_attempts, lfht.deletion_failures, & lfht.deletion_starts will vary */

    assert(    0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(    0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));

    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert(atomic_load(&(lfht.ins_deletion_completions)) + 
           atomic_load(&(lfht.del_deletion_completions)) +
           atomic_load(&(lfht.lfsll_phys_len)) - 2 == atomic_load(&(lfht.insertions)));

    /* lfht.searches, lfht.successful_searches, & lfht.failed_searches will vary */

    assert( atomic_load(&(lfht.searches)) == 
            (atomic_load(&(lfht.successful_searches)) + atomic_load(&(lfht.failed_searches))) );

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */
    assert((params.search_by_val_fails + params.search_by_val_successes) ==
           atomic_load(&(lfht.value_searches)));

    assert((params.swap_val_fails + params.swap_val_successes) ==
           atomic_load(&(lfht.value_swaps)));

    assert(1 == params.itter_inits);
    /* number of itter_nexts varies */
    assert(1 == params.itter_ends);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_lfsll_mt_test_fcn_2__serial_test() */


/***********************************************************************************
 *
 * lfht_mt_test_fcn_1__serial_test()
 *
 *     Serial test of lfht_mt_test_fcn_1().
 *
 *                                                   JRM -- 6/19/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_mt_test_fcn_1__serial_test()
{
    unsigned int seed;
    int count = 0;
    int log[10000];
    struct timeval t;
    struct lfht_t lfht;
    struct lfht_mt_test_params_t params = {
        /* lfht_ptr                = */ NULL,
        /* start_id                = */ 50000LL,
        /* step                    = */ -3LL,
        /* num_ids                 = */ 10000LL,
        /* iterations              = */ 0,   /* not used in this case */
        /* ins_fails               = */ 0LL,
        /* del_fails               = */ 0LL,
        /* search_fails            = */ 0LL,
        /* search_by_val_fails     = */ 0LL,
        /* swap_val_fails          = */ 0LL,
        /* ins_successes           = */ 0LL, /* not used in this case */
        /* del_successes           = */ 0LL, /* not used in this case */
        /* search_successes        = */ 0LL, /* not used in this case */
        /* search_by_val_successes = */ 0LL, /* not used in this case */
        /* swap_val_successes      = */ 0LL, /* not used in this case */
        /* itter_inits             = */ 0LL,
        /* itter_nexts             = */ 0LL,
        /* itter_ends              = */ 0LL
    };


    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT serial test of lfht_mt_test_fcn_1 (seed = 0x%x) ...", seed);

    fflush(stdout);

    lfht_init(&lfht);

    params.lfht_ptr = &lfht;

    lfht_mt_test_fcn_1((void *)(&params));

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);

    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(params.ins_fails == atomic_load(&(lfht.insertion_failures)));
    assert(params.del_fails == atomic_load(&(lfht.deletion_failures)));
    assert(params.search_fails == atomic_load(&(lfht.failed_searches)));
    assert(params.search_by_val_fails == atomic_load(&(lfht.failed_val_searches)));
    assert(params.swap_val_fails == atomic_load(&(lfht.failed_val_swaps)));

    assert(params.itter_inits == atomic_load(&(lfht.itter_inits)));
    assert(params.itter_nexts == atomic_load(&(lfht.itter_nexts)));
    assert(params.itter_ends == atomic_load(&(lfht.itter_ends)));

    assert(   0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(2049 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(20000 == atomic_load(&(lfht.insertions)));
    assert(10000 == atomic_load(&(lfht.insertion_failures)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));

    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    assert(30000 == atomic_load(&(lfht.deletion_attempts)));
    assert(10000 == atomic_load(&(lfht.deletion_failures)));
    assert(20000 == atomic_load(&(lfht.deletion_starts)));
    assert(    0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(    0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));

    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert(atomic_load(&(lfht.ins_deletion_completions)) + 
           atomic_load(&(lfht.del_deletion_completions)) +
           atomic_load(&(lfht.lfsll_phys_len)) - 
           atomic_load(&(lfht.buckets_initialized)) - 1 == 20000);

    assert(30000 == atomic_load(&(lfht.searches)));
    assert(20000 == atomic_load(&(lfht.successful_searches)));
    assert(10000 == atomic_load(&(lfht.failed_searches)));

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and i
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */
    assert(10000 == (atomic_load(&(lfht.successful_val_swaps)) / 2) + 
                    atomic_load(&(lfht.failed_val_swaps)));

    assert(1 == params.itter_inits);
    assert(params.itter_nexts + 1 == (atomic_load(&(lfht.successful_val_swaps)) / 2));
    assert(1 == params.itter_ends);

    assert(0 == params.search_by_val_fails);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_mt_test_fcn_1__serial_test() */


/***********************************************************************************
 *
 * lfht_mt_test_fcn_2__serial_test()
 *
 *     Serial test of lfht_mt_test_fcn_2().
 *
 *                                                   JRM -- 6/28/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_mt_test_fcn_2__serial_test()
{
    unsigned int seed;
    int count = 0;
    int log[10000];
    struct timeval t;
    struct lfht_t lfht;
    struct lfht_mt_test_params_t params = {
        /* lfht_ptr                = */ NULL,
        /* start_id                = */ 0LL,
        /* step                    = */ 0LL, /* not used in this case */
        /* num_ids                 = */ 10000LL,
        /* iterations              = */ 1000000LL,
        /* ins_fails               = */ 0LL,
        /* del_fails               = */ 0LL,
        /* search_fails            = */ 0LL,
        /* search_by_val_fails     = */ 0LL,
        /* swap_val_fails          = */ 0LL,
        /* ins_successes           = */ 0LL, 
        /* del_successes           = */ 0LL,
        /* search_successes        = */ 0LL,
        /* search_by_val_successes = */ 0LL,
        /* swap_val_successes      = */ 0LL,
        /* itter_inits             = */ 0LL,
        /* itter_nexts             = */ 0LL,
        /* itter_ends              = */ 0LL
    };

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT serial test of lfht_mt_test_fcn_2 (seed = 0x%x) ...", seed);

    fflush(stdout);

    lfht_init(&lfht);

    params.lfht_ptr = &lfht;

    lfht_mt_test_fcn_2((void *)(&params));

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);
#endif /* JRM */

#if 0 /* JRM */
    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert(params.ins_fails           == atomic_load(&(lfht.insertion_failures)));
    assert(params.del_fails           == atomic_load(&(lfht.deletion_failures)));
    assert(params.search_fails        == atomic_load(&(lfht.failed_searches)));
    assert(params.search_by_val_fails == atomic_load(&(lfht.failed_val_searches)));
    assert(params.swap_val_fails      == atomic_load(&(lfht.failed_val_swaps)));

    assert(params.ins_successes           == atomic_load(&(lfht.insertions)));
    assert(params.del_successes           == atomic_load(&(lfht.deletion_starts)));
    assert(params.search_successes        == atomic_load(&(lfht.successful_searches)));
    assert(params.search_by_val_successes == atomic_load(&(lfht.successful_val_searches)));
    assert(params.swap_val_successes      == atomic_load(&(lfht.successful_val_swaps)));

    assert(params.itter_inits == atomic_load(&(lfht.itter_inits)));
    assert(params.itter_nexts == atomic_load(&(lfht.itter_nexts)));
    assert(params.itter_ends  == atomic_load(&(lfht.itter_ends)));


    /* lfht.log_len & lfht.phys_len will vary */

    assert((atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))) ==
           (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));

    /* lfht.insertions & lfht.insertion_failures will vary */

    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_ins_col)));
    assert(    0 == atomic_load(&(lfht.ins_restarts_due_to_del_col)));

    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    /* lfht.deletion_attempts, lfht.deletion_failures, & lfht.deletion_starts will vary */

    assert(    0 == atomic_load(&(lfht.deletion_start_cols)));
    assert(    0 == atomic_load(&(lfht.del_restarts_due_to_del_col)));

    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert(atomic_load(&(lfht.ins_deletion_completions)) + 
           atomic_load(&(lfht.del_deletion_completions)) +
           atomic_load(&(lfht.lfsll_phys_len)) - 
           atomic_load(&(lfht.buckets_initialized)) - 1 == atomic_load(&(lfht.insertions)));

    /* lfht.searches, lfht.successful_searches, & lfht.failed_searches will vary */

    assert( atomic_load(&(lfht.searches)) == 
            (atomic_load(&(lfht.successful_searches)) + atomic_load(&(lfht.failed_searches))) );

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */
    assert((params.search_by_val_fails + params.search_by_val_successes) ==
           atomic_load(&(lfht.value_searches)));

    assert((params.swap_val_fails + params.swap_val_successes) ==
           atomic_load(&(lfht.value_swaps)));

    assert(1 == params.itter_inits);
    /* number of itter_nexts varies */
    assert(1 == params.itter_ends);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_mt_test_fcn_2__serial_test() */


/***********************************************************************************
 *
 * lfht_lfsll_mt_test_1()
 *
 *     Setup a lock free hash table, but configure it to function as a lock free
 *     singly linked list.
 *
 *     Spawn nthreads threads, each of which performs operations on the LFHT
 *     in parallel.  In this case, allow only one thread to touch any one value.
 *     As a result, there should be no insert, deleted, or search collisions
 *     proper.  However collisions allocating and discarding entries will occur,
 *     as will collisions when completing deletions. 
 *
 *                                                   JRM -- 6/19/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_lfsll_mt_test_1(int nthreads)
{
    int i;
    long long int ins_fails = 0LL;
    long long int del_fails = 0LL;
    long long int search_fails = 0LL;
    long long int search_by_val_fails = 0LL;
    long long int swap_val_fails = 0LL;
    long long int itter_inits = 0LL;
    long long int itter_nexts = 0LL;
    long long int itter_ends = 0LL;
    unsigned int seed;
    struct timeval t;
    struct lfht_t lfht;
    pthread_t threads[MAX_NUM_THREADS];
    struct lfht_mt_test_params_t params[MAX_NUM_THREADS];

    assert(nthreads <= MAX_NUM_THREADS);

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT LFSLL multi-thread test 1 (nthreads = %d, seed = 0x%x) ...", 
            nthreads, seed);

    fflush(stdout);

    lfht_init(&lfht);

    /* set lfht.max_index_bits to zero -- which forces the lock free hash table
     * to funtion as a lock free singly linked list, as it forces all entries
     * into a single hash bucket.
     */   
    lfht.max_index_bits = 0; 

    for (i = 0; i < nthreads; i++) {

        params[i].lfht_ptr                = &lfht;
        params[i].start_id                = (long long int)i;
        params[i].step                    = (long long int)nthreads;
        params[i].num_ids                 = 10000LL;
        params[i].itterations             = 0LL; /* not used in this test */
        params[i].ins_fails               = 0LL;
        params[i].del_fails               = 0LL;
        params[i].search_fails            = 0LL;
        params[i].search_by_val_fails     = 0LL;
        params[i].swap_val_fails          = 0LL;
        params[i].ins_successes           = 0LL; /* not used in this test */
        params[i].del_successes           = 0LL; /* not used in this test */
        params[i].search_successes        = 0LL; /* not used in this test */
        params[i].search_by_val_successes = 0LL; /* not used in this test */
        params[i].swap_val_successes      = 0LL; /* not used in this test */
        params[i].itter_inits             = 0LL;
        params[i].itter_nexts             = 0LL;
        params[i].itter_ends              = 0LL;
    }

    /* create the threads and have them execute &lfht_mt_test_fcn_1. */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_create(&(threads[i]), NULL, &lfht_mt_test_fcn_1, (void *)(&(params[i]))));
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_join(threads[i], NULL));

        ins_fails           += params[i].ins_fails;
        del_fails           += params[i].del_fails;
        search_fails        += params[i].search_fails;
        search_by_val_fails += params[i].search_by_val_fails;
        swap_val_fails      += params[i].swap_val_fails;

        itter_inits         += params[i].itter_inits;
        itter_nexts         += params[i].itter_nexts;
        itter_ends          += params[i].itter_ends;
    }

#if 0 
    fprintf(stdout, "\nins / del / search fails = %lld / %lld / %lld\n", 
            ins_fails, del_fails, search_fails);
    fprintf(stdout, "nodes allocated / freed / net = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.num_nodes_allocated)),
            atomic_load(&(lfht.num_nodes_freed)),
            (atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))));
    fprintf(stdout, "phys_len / fl_len / sum = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.lfsll_phys_len)), atomic_load(&(lfht.fl_len)),
            (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));
#endif

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);
#endif /* JRM */

#if 0 /* JRM */
    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert((atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))) ==
           (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));

    assert(ins_fails == atomic_load(&(lfht.insertion_failures)));
    assert(del_fails == atomic_load(&(lfht.deletion_failures)));
    assert(search_fails == atomic_load(&(lfht.failed_searches)));
    assert(search_by_val_fails == atomic_load(&(lfht.failed_val_searches)));
    assert(swap_val_fails == atomic_load(&(lfht.failed_val_swaps)));

    assert(itter_inits == atomic_load(&(lfht.itter_inits)));
    assert(itter_nexts == atomic_load(&(lfht.itter_nexts)));
    assert(itter_ends == atomic_load(&(lfht.itter_ends)));

    assert(0 == atomic_load(&(lfht.lfsll_log_len)));
    assert(3 == atomic_load(&(lfht.lfsll_phys_len)));

    assert(2 * nthreads * 10000 == atomic_load(&(lfht.insertions)));
    assert(    nthreads * 10000 == atomic_load(&(lfht.insertion_failures)));

    /* lfht.lfht.ins_restarts_due_to_ins_col and lfht.lfht.ins_restarts_due_to_del_col will vary */
    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    assert(3 * nthreads * 10000 == atomic_load(&(lfht.deletion_attempts)));
    assert(    nthreads * 10000 == atomic_load(&(lfht.deletion_failures)));
    assert(2 * nthreads * 10000 == atomic_load(&(lfht.deletion_starts)));

    /* lfht.deletion_start_cols and lfht.del_restarts_due_to_del_col) will vary */
    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert( ( atomic_load(&(lfht.ins_deletion_completions)) + 
              atomic_load(&(lfht.del_deletion_completions)) +
              atomic_load(&(lfht.lfsll_log_len)) )  == 
            ( (2 * nthreads * 10000) - (atomic_load(&(lfht.lfsll_phys_len)) - 2) ) );

    assert(3 * nthreads * 10000 == atomic_load(&(lfht.searches)));
    assert(2 * nthreads * 10000 == atomic_load(&(lfht.successful_searches)));
    assert(    nthreads * 10000 == atomic_load(&(lfht.failed_searches)));

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */

    /* value swaps (total, successful, and failed) will vary */

    assert(nthreads == itter_inits);
    /* itter_nexts will vary */
    assert(nthreads == itter_ends);

    assert(0 == search_by_val_fails);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_lfsll_mt_test_1() */


/***********************************************************************************
 *
 * lfht_lfsll_mt_test_2()
 *
 *     Setup a lock free hash table, but configure it to function as a lock free 
 *     singly linked list.
 *
 *     Spawn nthreads threads, each of which performs operations on the LFHT
 *     in parallel.  In this case, all threads perform the same set of operations
 *     on the same set of value -- thus all manner of collisions, insert, search, 
 *     and delete failures are expected.  Further, since operations are performed
 *     in a largely random order, correctness is hard to check.
 *
 *     Instead, we simply verify that the statistics ballance -- i.e. allocs and 
 *     frees ballance, successful inserts and deletes ballance, etc.
 *
 *                                                   JRM -- 6/27/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_lfsll_mt_test_2(int nthreads)
{
    int i;
    long long int ins_fails = 0LL;
    long long int del_fails = 0LL;
    long long int search_fails = 0LL;
    long long int search_by_val_fails = 0LL;
    long long int swap_val_fails = 0LL;
    long long int ins_successes = 0LL;
    long long int del_successes = 0LL;
    long long int search_successes = 0LL;
    long long int search_by_val_successes = 0LL;
    long long int swap_val_successes = 0LL;
    long long int itter_inits = 0LL;
    long long int itter_nexts = 0LL;
    long long int itter_ends = 0LL;
    unsigned int seed;
    struct timeval t;
    struct lfht_t lfht;
    pthread_t threads[MAX_NUM_THREADS];
    struct lfht_mt_test_params_t params[MAX_NUM_THREADS];

    assert(nthreads <= MAX_NUM_THREADS);

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT LFSLL multi-thread test 2 (nthreads = %d, seed = 0x%x) ...", 
            nthreads, seed);

    fflush(stdout);

    lfht_init(&lfht);

    /* set lfht.max_index_bits to zero -- which forces the lock free hash table
     * to funtion as a lock free singly linked list, as it forces all entries
     * into a single hash bucket.
     */
    lfht.max_index_bits = 0;

    for (i = 0; i < nthreads; i++) {

        params[i].lfht_ptr                = &lfht;

        if ( (i % 2) == 0 ) {

            params[i].start_id            = (long long int)0;
            params[i].step                = (long long int)1;

        } else {

            params[i].start_id            = (long long int)9999;
            params[i].step                = (long long int)-1;
        }
        params[i].num_ids                 = 10000LL;
        params[i].itterations             = 0LL; /* not used in this test */
        params[i].ins_fails               = 0LL;
        params[i].del_fails               = 0LL;
        params[i].search_fails            = 0LL;
        params[i].search_by_val_fails     = 0LL;
        params[i].swap_val_fails          = 0LL;
        params[i].ins_successes           = 0LL; /* not used in this test */
        params[i].del_successes           = 0LL; /* not used in this test */
        params[i].search_successes        = 0LL; /* not used in this test */
        params[i].search_by_val_successes = 0LL; /* not used in this test */
        params[i].swap_val_successes      = 0LL; /* not used in this test */
        params[i].itter_inits             = 0LL;
        params[i].itter_nexts             = 0LL;
        params[i].itter_ends              = 0LL;
    }

    /* create the threads and have them execute &lfht_mt_test_fcn_1. */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_create(&(threads[i]), NULL, &lfht_mt_test_fcn_1, (void *)(&(params[i]))));
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_join(threads[i], NULL));

        ins_fails               += params[i].ins_fails;
        del_fails               += params[i].del_fails;
        search_fails            += params[i].search_fails;
        search_by_val_fails     += params[i].search_by_val_fails;
        swap_val_fails          += params[i].swap_val_fails;

        ins_successes           += params[i].ins_successes;
        del_successes           += params[i].del_successes;
        search_successes        += params[i].search_successes;
        search_by_val_successes += params[i].search_by_val_successes;
        swap_val_successes      += params[i].swap_val_successes;

        itter_inits             += params[i].itter_inits;
        itter_nexts             += params[i].itter_nexts;
        itter_ends              += params[i].itter_ends;
    }

#if 0 
    fprintf(stdout, "\nins / del / search fails = %lld / %lld / %lld\n", 
            ins_fails, del_fails, search_fails);
    fprintf(stdout, "\nins / del / search successes = %lld / %lld / %lld\n", 
            ins_successes, del_successes, search_successes);
    fprintf(stdout, "nodes allocated / freed / net = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.num_nodes_allocated)),
            atomic_load(&(lfht.num_nodes_freed)),
            (atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))));
    fprintf(stdout, "lfsll_phys_len / fl_len / sum = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.lfsll_phys_len)), atomic_load(&(lfht.fl_len)),
            (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));
#endif

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);
#endif /* JRM */

#if 0 /* JRM */
    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert((atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))) ==
           (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));

    assert(ins_fails == atomic_load(&(lfht.insertion_failures)));
    assert(del_fails == atomic_load(&(lfht.deletion_failures)));
    assert(search_fails == atomic_load(&(lfht.failed_searches)));
    assert(search_by_val_fails == atomic_load(&(lfht.failed_val_searches)));
    assert(swap_val_fails == atomic_load(&(lfht.failed_val_swaps)));

    assert(itter_inits == atomic_load(&(lfht.itter_inits)));
    assert(itter_nexts == atomic_load(&(lfht.itter_nexts)));
    assert(itter_ends == atomic_load(&(lfht.itter_ends)));

    /* lfht.lfsll_log_len and lfht.lfsll_phys_len will vary */

    assert(3 * nthreads * 10000 == 
           (atomic_load(&(lfht.insertions)) + atomic_load(&(lfht.insertion_failures))));

    /* lfht.lfht.ins_restarts_due_to_ins_col and lfht.lfht.ins_restarts_due_to_del_col will vary */
    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    assert(3 * nthreads * 10000 == atomic_load(&(lfht.deletion_attempts)));
    assert((nthreads * 10000) + (ins_fails - (nthreads * 10000)) == 
            atomic_load(&(lfht.deletion_failures)) + atomic_load(&(lfht.deletion_start_cols)) - 
            atomic_load(&((lfht.lfsll_log_len))));
    assert(((2 * nthreads * 10000) - (ins_fails - (nthreads * 10000))) ==
           (atomic_load(&(lfht.deletion_starts)) + atomic_load(&(lfht.lfsll_log_len))));

    /* lfht.deletion_start_cols and lfht.del_restarts_due_to_del_col) will vary */
    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert( ( atomic_load(&(lfht.ins_deletion_completions)) + 
              atomic_load(&(lfht.del_deletion_completions)) +
              atomic_load(&(lfht.lfsll_phys_len)) - 2 )  == 
            ( (2 * nthreads * 10000) - (ins_fails - (nthreads * 10000)) ) );

    assert(3 * nthreads * 10000 == atomic_load(&(lfht.searches)));
    assert(3 * nthreads * 10000 == 
          ( atomic_load(&(lfht.successful_searches)) + atomic_load(&(lfht.failed_searches)) ) );

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */

    /* value swaps (total, successful, and failed) will vary */

    assert(nthreads == itter_inits);
    /* itter_nexts will vary */
    assert(nthreads == itter_ends);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_lfsll_mt_test_2() */


/***********************************************************************************
 *
 * lfht_lfsll_mt_test_3()
 *
 *     Setup a lock free hash table, but configure it to function as a lock free
 *     singly linked list.
 *
 *     Spawn nthreads threads, each of which performs random operations on the LFHT
 *     in parallel.  
 *
 *     In this case, all threads perform random operations on random values in 
 *     the same range -- thus all manner of collisions, insert, search, 
 *     and delete failures are expected.  Further, since operations are performed
 *     in random order, correctness is hard to check.
 *
 *     Instead, we simply verify that the statistics ballance -- i.e. allocs and 
 *     frees ballance, successful inserts and deletes ballance, etc.
 *
 *                                                   JRM -- 6/28/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_lfsll_mt_test_3(int nthreads)
{
    int i;
    long long int ins_fails = 0LL;
    long long int del_fails = 0LL;
    long long int search_fails = 0LL;
    long long int search_by_val_fails = 0LL;
    long long int swap_val_fails = 0LL;
    long long int ins_successes = 0LL;
    long long int del_successes = 0LL;
    long long int search_successes = 0LL;
    long long int search_by_val_successes = 0LL;
    long long int swap_val_successes = 0LL;
    long long int itter_inits = 0LL;
    long long int itter_nexts = 0LL;
    long long int itter_ends = 0LL;
    unsigned int seed;
    struct timeval t;
    struct lfht_t lfht;
    pthread_t threads[MAX_NUM_THREADS];
    struct lfht_mt_test_params_t params[MAX_NUM_THREADS];

    assert(nthreads <= MAX_NUM_THREADS);

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT LFSLL multi-thread test 3 (nthreads = %d, seed = 0x%x) ...", 
            nthreads, seed);

    fflush(stdout);

    lfht_init(&lfht);

    /* set lfht.max_index_bits to zero -- which forces the lock free hash table
     * to funtion as a lock free singly linked list, as it forces all entries
     * into a single hash bucket.
     */   
    lfht.max_index_bits = 0; 

    for (i = 0; i < nthreads; i++) {

        params[i].lfht_ptr                = &lfht;
        params[i].start_id                = (long long int)0;
        params[i].step                    = (long long int)0; /* not used in this case */
        params[i].num_ids                 = 10000LL;
        params[i].itterations             = 100000LL;
        params[i].ins_fails               = 0LL;
        params[i].del_fails               = 0LL;
        params[i].search_fails            = 0LL;
        params[i].search_by_val_fails     = 0LL;
        params[i].swap_val_fails          = 0LL;
        params[i].ins_successes           = 0LL; 
        params[i].del_successes           = 0LL;
        params[i].search_successes        = 0LL;
        params[i].search_by_val_successes = 0LL;
        params[i].swap_val_successes      = 0LL;
        params[i].itter_inits             = 0LL;
        params[i].itter_nexts             = 0LL;
        params[i].itter_ends              = 0LL;
    }

    /* create the threads and have them execute &lfht_mt_test_fcn_1. */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_create(&(threads[i]), NULL, &lfht_mt_test_fcn_2, (void *)(&(params[i]))));
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_join(threads[i], NULL));

        ins_fails               += params[i].ins_fails;
        del_fails               += params[i].del_fails;
        search_fails            += params[i].search_fails;
        search_by_val_fails     += params[i].search_by_val_fails;
        swap_val_fails          += params[i].swap_val_fails;

        ins_successes           += params[i].ins_successes;
        del_successes           += params[i].del_successes;
        search_successes        += params[i].search_successes;
        search_by_val_successes += params[i].search_by_val_successes;
        swap_val_successes      += params[i].swap_val_successes;

        itter_inits             += params[i].itter_inits;
        itter_nexts             += params[i].itter_nexts;
        itter_ends              += params[i].itter_ends;
    }

#if 0 
    fprintf(stdout, "\nins / del / search fails = %lld / %lld / %lld\n", 
            ins_fails, del_fails, search_fails);
    fprintf(stdout, "\nins / del / search successes = %lld / %lld / %lld\n", 
            ins_successes, del_successes, search_successes);
    fprintf(stdout, "nodes allocated / freed / net = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.num_nodes_allocated)),
            atomic_load(&(lfht.num_nodes_freed)),
            (atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))));
    fprintf(stdout, "lfsll_phys_len / fl_len / sum = %lld / %lld / %lld, lfsll_log_len = %lld.\n",
            atomic_load(&(lfht.lfsll_phys_len)), atomic_load(&(lfht.fl_len)),
            (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))),
            atomic_load(&(lfht.lfsll_log_len)));
#endif

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);
#endif /* JRM */

#if 0 /* JRM */
    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert((atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))) ==
           (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));

    assert(ins_fails            == atomic_load(&(lfht.insertion_failures)));
    assert(del_fails            == atomic_load(&(lfht.deletion_failures)));
    assert(search_fails         == atomic_load(&(lfht.failed_searches)));
    assert(search_by_val_fails  == atomic_load(&(lfht.failed_val_searches)));
    assert(swap_val_fails       == atomic_load(&(lfht.failed_val_swaps)));

    assert(ins_successes            == atomic_load(&(lfht.insertions)));
    assert(del_successes            == (atomic_load(&(lfht.deletion_starts)) + 
                                        atomic_load(&(lfht.deletion_start_cols))));
    assert(search_successes         == atomic_load(&(lfht.successful_searches)));
    assert(search_by_val_successes  == atomic_load(&(lfht.successful_val_searches)));
    assert(swap_val_successes       == atomic_load(&(lfht.successful_val_swaps)));

    assert(itter_inits == atomic_load(&(lfht.itter_inits)));
    assert(itter_nexts == atomic_load(&(lfht.itter_nexts)));
    assert(itter_ends  == atomic_load(&(lfht.itter_ends)));

    /* lfht.lfsll_log_len and lfht.lfsll_phys_len will vary */

    /* lfht.insertions & lfht.insertion_failures will vary */

    /* lfht.lfht.ins_restarts_due_to_ins_col and lfht.lfht.ins_restarts_due_to_del_col will vary */

    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    /* lfht.deletion_attempts will vary */

    /* lfht.deletion_failures will vary */

    assert( (ins_successes - atomic_load(&(lfht.lfsll_log_len))) == 
            (atomic_load(&(lfht.ins_deletion_completions)) +
             atomic_load(&(lfht.del_deletion_completions)) +
             (atomic_load(&(lfht.lfsll_phys_len)) - atomic_load(&(lfht.lfsll_log_len)) - 2)) ); 

    /* lfht.deletion_start_cols and lfht.del_restarts_due_to_del_col) will vary */
    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    /* lfht.searches, lfht.successful_searches and lfht.failed_searches will vary. */

    assert(atomic_load(&(lfht.searches)) == (search_successes + search_fails));

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */

    assert((search_by_val_fails + search_by_val_successes) == atomic_load(&(lfht.value_searches)));

    assert((swap_val_fails + swap_val_successes) == atomic_load(&(lfht.value_swaps)));

    assert(nthreads == itter_inits);
    /* number of itter_nexts varies */
    assert(nthreads == itter_ends);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_lfsll_mt_test_3() */


/***********************************************************************************
 *
 * lfht_mt_test_1()
 *
 *     Setup a lock free hash table.
 *
 *     Spawn nthreads threads, each of which performs operations on the LFHT
 *     in parallel.  In this case, allow only one thread to touch any one value.
 *     As a result, there should be no insert, deleted, or search collisions
 *     proper.  However collisions allocating and discarding entries will occur,
 *     as will collisions when completing deletions. 
 *
 *                                                   JRM -- 6/19/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_mt_test_1(int run, int nthreads)
{
    int i;
    long long int ins_fails = 0LL;
    long long int del_fails = 0LL;
    long long int search_fails = 0LL;
    long long int search_by_val_fails = 0LL;
    long long int swap_val_fails = 0LL;
    long long int itter_inits = 0LL;
    long long int itter_nexts = 0LL;
    long long int itter_ends = 0LL;
    unsigned int seed;
    struct timeval t;
    struct lfht_t lfht;
    pthread_t threads[MAX_NUM_THREADS];
    struct lfht_mt_test_params_t params[MAX_NUM_THREADS];

    assert(nthreads <= MAX_NUM_THREADS);

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT multi-thread test 1 (nthreads = %d, run = %d, seed = 0x%x) ...", 
            nthreads, run, seed);

    fflush(stdout);

    lfht_init(&lfht);

    for (i = 0; i < nthreads; i++) {

        params[i].lfht_ptr                = &lfht;
        params[i].start_id                = (long long int)i;
        params[i].step                    = (long long int)nthreads;
        params[i].num_ids                 = 10000LL;
        params[i].itterations             = 0LL; /* not used in this test */
        params[i].ins_fails               = 0LL;
        params[i].del_fails               = 0LL;
        params[i].search_fails            = 0LL;
        params[i].search_by_val_fails     = 0LL;
        params[i].swap_val_fails          = 0LL;
        params[i].ins_successes           = 0LL; /* not used in this test */
        params[i].del_successes           = 0LL; /* not used in this test */
        params[i].search_successes        = 0LL; /* not used in this test */
        params[i].search_by_val_successes = 0LL; /* not used in this test */
        params[i].swap_val_successes      = 0LL; /* not used in this test */
        params[i].itter_inits             = 0LL;
        params[i].itter_nexts             = 0LL;
        params[i].itter_ends              = 0LL;
    }

    /* create the threads and have them execute &lfht_mt_test_fcn_1. */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_create(&(threads[i]), NULL, &lfht_mt_test_fcn_1, (void *)(&(params[i]))));
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_join(threads[i], NULL));

        ins_fails           += params[i].ins_fails;
        del_fails           += params[i].del_fails;
        search_fails        += params[i].search_fails;
        search_by_val_fails += params[i].search_by_val_fails;
        swap_val_fails      += params[i].swap_val_fails;

        itter_inits         += params[i].itter_inits;
        itter_nexts         += params[i].itter_nexts;
        itter_ends          += params[i].itter_ends;
    }

#if 0 
    fprintf(stdout, "\nins / del / search fails = %lld / %lld / %lld\n", 
            ins_fails, del_fails, search_fails);
    fprintf(stdout, "nodes allocated / freed / net = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.num_nodes_allocated)),
            atomic_load(&(lfht.num_nodes_freed)),
            (atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))));
    fprintf(stdout, "phys_len / fl_len / sum = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.lfsll_phys_len)), atomic_load(&(lfht.fl_len)),
            (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));
#endif

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);
#endif /* JRM */

#if 0 /* JRM */
    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert((atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))) ==
           (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));

    assert(ins_fails           == atomic_load(&(lfht.insertion_failures)));
    assert(del_fails           == atomic_load(&(lfht.deletion_failures)));
    assert(search_fails        == atomic_load(&(lfht.failed_searches)));
    assert(search_by_val_fails == atomic_load(&(lfht.failed_val_searches)));
    assert(swap_val_fails      == atomic_load(&(lfht.failed_val_swaps)));

    assert(itter_inits         == atomic_load(&(lfht.itter_inits)));
    assert(itter_nexts         == atomic_load(&(lfht.itter_nexts)));
    assert(itter_ends          == atomic_load(&(lfht.itter_ends)));

    assert(   0 == atomic_load(&(lfht.lfsll_log_len)));

    /* lfsll_phys_len varies, and is checked below */

    assert(2 * nthreads * 10000 == atomic_load(&(lfht.insertions)));
    assert(    nthreads * 10000 == atomic_load(&(lfht.insertion_failures)));

    /* lfht.lfht.ins_restarts_due_to_ins_col and lfht.lfht.ins_restarts_due_to_del_col will vary */
    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    assert(3 * nthreads * 10000 == atomic_load(&(lfht.deletion_attempts)));
    assert(    nthreads * 10000 == atomic_load(&(lfht.deletion_failures)));
    assert(2 * nthreads * 10000 == atomic_load(&(lfht.deletion_starts)));

    /* lfht.deletion_start_cols and lfht.del_restarts_due_to_del_col) will vary */
    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert( ( atomic_load(&(lfht.ins_deletion_completions)) + 
              atomic_load(&(lfht.del_deletion_completions)) +
              atomic_load(&(lfht.lfsll_log_len)) )  == 
            ( (2 * nthreads * 10000) - 
              (atomic_load(&(lfht.lfsll_phys_len)) - atomic_load(&(lfht.buckets_initialized)) - 1) ) );

    assert(3 * nthreads * 10000 == atomic_load(&(lfht.searches)));
    assert(2 * nthreads * 10000 == atomic_load(&(lfht.successful_searches)));
    assert(    nthreads * 10000 == atomic_load(&(lfht.failed_searches)));

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */

    /* value swaps (total, successful, and failed) will vary */

    assert(nthreads == itter_inits);
    /* itter_nexts will vary */
    assert(nthreads == itter_ends);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_mt_test_1() */


/***********************************************************************************
 *
 * lfht_mt_test_2()
 *
 *     Setup a lock free hash table.
 *
 *     Spawn nthreads threads, each of which performs operations on the LFHT
 *     in parallel.  In this case, all threads perform the same set of operations
 *     on the same set of value -- thus all manner of collisions, insert, search, 
 *     and delete failures are expected.  Further, since operations are performed
 *     in a largely random order, correctness is hard to check.
 *
 *     Instead, we simply verify that the statistics ballance -- i.e. allocs and 
 *     frees ballance, successful inserts and deletes ballance, etc.
 *
 *                                                   JRM -- 6/27/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_mt_test_2(int run, int nthreads)
{
    int i;
    long long int ins_fails = 0LL;
    long long int del_fails = 0LL;
    long long int search_fails = 0LL;
    long long int search_by_val_fails = 0LL;
    long long int swap_val_fails = 0LL;
    long long int ins_successes = 0LL;
    long long int del_successes = 0LL;
    long long int search_successes = 0LL;
    long long int search_by_val_successes = 0LL;
    long long int swap_val_successes = 0LL;
    long long int itter_inits = 0LL;
    long long int itter_nexts = 0LL;
    long long int itter_ends = 0LL;
    unsigned int seed;
    struct timeval t;
    struct lfht_t lfht;
    pthread_t threads[MAX_NUM_THREADS];
    struct lfht_mt_test_params_t params[MAX_NUM_THREADS];

    assert(nthreads <= MAX_NUM_THREADS);

    assert(0 == gettimeofday(&t, NULL));

    seed = (unsigned int)(t.tv_usec);

    srand(seed);

    fprintf(stdout, "LFHT multi-thread test 2 (nthreads = %d, run = %d, seed = 0x%x) ...", 
            nthreads, run, seed);

    fflush(stdout);

    lfht_init(&lfht);

    for (i = 0; i < nthreads; i++) {

        params[i].lfht_ptr                = &lfht;

        if ( (i % 2) == 0 ) {

            params[i].start_id            = (long long int)0;
            params[i].step                = (long long int)1;

        } else {

            params[i].start_id            = (long long int)9999;
            params[i].step                = (long long int)-1;
        }
        params[i].num_ids                 = 10000LL;
        params[i].itterations             = 0LL; /* not used in this test */
        params[i].ins_fails               = 0LL;
        params[i].del_fails               = 0LL;
        params[i].search_fails            = 0LL;
        params[i].search_by_val_fails     = 0LL;
        params[i].swap_val_fails          = 0LL;
        params[i].ins_successes           = 0LL; /* not used in this test */
        params[i].del_successes           = 0LL; /* not used in this test */
        params[i].search_successes        = 0LL; /* not used in this test */
        params[i].search_by_val_successes = 0LL; /* not used in this test */
        params[i].swap_val_successes      = 0LL; /* not used in this test */
        params[i].itter_inits             = 0LL;
        params[i].itter_nexts             = 0LL;
        params[i].itter_ends              = 0LL;
    }

    /* create the threads and have them execute &lfht_mt_test_fcn_1. */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_create(&(threads[i]), NULL, &lfht_mt_test_fcn_1, (void *)(&(params[i]))));
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_join(threads[i], NULL));

        ins_fails               += params[i].ins_fails;
        del_fails               += params[i].del_fails;
        search_fails            += params[i].search_fails;
        search_by_val_fails     += params[i].search_by_val_fails;
        swap_val_fails          += params[i].swap_val_fails;

        ins_successes           += params[i].ins_successes;
        del_successes           += params[i].del_successes;
        search_successes        += params[i].search_successes;
        search_by_val_successes += params[i].search_by_val_successes;
        swap_val_successes      += params[i].swap_val_successes;

        itter_inits             += params[i].itter_inits;
        itter_nexts             += params[i].itter_nexts;
        itter_ends              += params[i].itter_ends;
    }

#if 0 
    fprintf(stdout, "\nins / del / search fails = %lld / %lld / %lld\n", 
            ins_fails, del_fails, search_fails);
    fprintf(stdout, "\nins / del / search successes = %lld / %lld / %lld\n", 
            ins_successes, del_successes, search_successes);
    fprintf(stdout, "nodes allocated / freed / net = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.num_nodes_allocated)),
            atomic_load(&(lfht.num_nodes_freed)),
            (atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))));
    fprintf(stdout, "lfsll_phys_len / fl_len / sum = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.lfsll_phys_len)), atomic_load(&(lfht.fl_len)),
            (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));
#endif

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);
#endif /* JRM */

#if 0 /* JRM */
    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert((atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))) ==
           (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));

    assert(ins_fails           == atomic_load(&(lfht.insertion_failures)));
    assert(del_fails           == atomic_load(&(lfht.deletion_failures)));
    assert(search_fails        == atomic_load(&(lfht.failed_searches)));
    assert(search_by_val_fails == atomic_load(&(lfht.failed_val_searches)));
    assert(swap_val_fails      == atomic_load(&(lfht.failed_val_swaps)));

    assert(itter_inits         == atomic_load(&(lfht.itter_inits)));
    assert(itter_nexts         == atomic_load(&(lfht.itter_nexts)));
    assert(itter_ends          == atomic_load(&(lfht.itter_ends)));

    /* lfht.lfsll_log_len and lfht.lfsll_phys_len will vary */

    assert(3 * nthreads * 10000 == 
           (atomic_load(&(lfht.insertions)) + atomic_load(&(lfht.insertion_failures))));

    /* lfht.lfht.ins_restarts_due_to_ins_col and lfht.lfht.ins_restarts_due_to_del_col will vary */
    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    assert(3 * nthreads * 10000 == atomic_load(&(lfht.deletion_attempts)));

    assert((nthreads * 10000) + (ins_fails - (nthreads * 10000)) == 
            atomic_load(&(lfht.deletion_failures)) + atomic_load(&(lfht.deletion_start_cols)) - 
            atomic_load(&((lfht.lfsll_log_len))));

    assert(((2 * nthreads * 10000) - (ins_fails - (nthreads * 10000))) ==
           (atomic_load(&(lfht.deletion_starts)) + atomic_load(&(lfht.lfsll_log_len))));

    /* lfht.deletion_start_cols and lfht.del_restarts_due_to_del_col) will vary */
    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    assert( ( atomic_load(&(lfht.ins_deletion_completions)) + 
              atomic_load(&(lfht.del_deletion_completions)) +
              atomic_load(&(lfht.lfsll_phys_len)) - 
              atomic_load(&(lfht.buckets_initialized)) - 1) ==
            ( (2 * nthreads * 10000) - (ins_fails - (nthreads * 10000)) ) );

    assert(3 * nthreads * 10000 == atomic_load(&(lfht.searches)));

    assert(3 * nthreads * 10000 == 
          ( atomic_load(&(lfht.successful_searches)) + atomic_load(&(lfht.failed_searches)) ) );

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */

    /* value swaps (total, successful, and failed) will vary */

    assert(nthreads == itter_inits);
    /* itter_nexts will vary */
    assert(nthreads == itter_ends);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_mt_test_2() */


/***********************************************************************************
 *
 * lfht_mt_test_3()
 *
 *     Setup a lock free hash table.
 *
 *     Spawn nthreads threads, each of which performs random operations on the LFHT
 *     in parallel.  
 *
 *     In this case, all threads perform random operations on random values in 
 *     the same range -- thus all manner of collisions, insert, search, 
 *     and delete failures are expected.  Further, since operations are performed
 *     in random order, correctness is hard to check.
 *
 *     Instead, we simply verify that the statistics ballance -- i.e. allocs and 
 *     frees ballance, successful inserts and deletes ballance, etc.
 *
 *                                                   JRM -- 6/28/23
 *
 * Changes:
 *
 *     None.
 *
 ***********************************************************************************/

void lfht_mt_test_3(int run, int nthreads)
{
    int i;
    long long int ins_fails = 0LL;
    long long int del_fails = 0LL;
    long long int search_fails = 0LL;
    long long int search_by_val_fails = 0LL;
    long long int swap_val_fails = 0LL;
    long long int ins_successes = 0LL;
    long long int del_successes = 0LL;
    long long int search_successes = 0LL;
    long long int search_by_val_successes = 0LL;
    long long int swap_val_successes = 0LL;
    long long int itter_inits = 0LL;
    long long int itter_nexts = 0LL;
    long long int itter_ends = 0LL;
    unsigned int seed;
    struct timeval t;
    struct lfht_t lfht;
    pthread_t threads[MAX_NUM_THREADS];
    struct lfht_mt_test_params_t params[MAX_NUM_THREADS];

    assert(nthreads <= MAX_NUM_THREADS);

    assert(0 == gettimeofday(&t, NULL));

#if 1 
    seed = (unsigned int)(t.tv_usec);
#else
    seed = 0xa4d3e;
#endif

    srand(seed);

    fprintf(stdout, "LFHT multi-thread test 3 (nthreads = %d, run = %d, seed = 0x%x) ...", 
            nthreads, run, seed);

    fflush(stdout);

    lfht_init(&lfht);

    for (i = 0; i < nthreads; i++) {

        params[i].lfht_ptr                = &lfht;
        params[i].start_id                = (long long int)0;
        params[i].step                    = (long long int)0; /* not used in this case */
        params[i].num_ids                 = 10000LL;
        params[i].itterations             = 100000LL;
        params[i].ins_fails               = 0LL;
        params[i].del_fails               = 0LL;
        params[i].search_fails            = 0LL;
        params[i].search_by_val_fails     = 0LL;
        params[i].swap_val_fails          = 0LL;
        params[i].ins_successes           = 0LL; 
        params[i].del_successes           = 0LL;
        params[i].search_successes        = 0LL;
        params[i].search_by_val_successes = 0LL;
        params[i].swap_val_successes      = 0LL;
        params[i].itter_inits             = 0LL;
        params[i].itter_nexts             = 0LL;
        params[i].itter_ends              = 0LL;
    }

    /* create the threads and have them execute &lfht_mt_test_fcn_1. */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_create(&(threads[i]), NULL, &lfht_mt_test_fcn_2, (void *)(&(params[i]))));
    }

    /* Wait for all the threads to complete */
    for (i = 0; i < nthreads; i++) {

        assert(0 == pthread_join(threads[i], NULL));

        ins_fails               += params[i].ins_fails;
        del_fails               += params[i].del_fails;
        search_fails            += params[i].search_fails;
        search_by_val_fails     += params[i].search_by_val_fails;
        swap_val_fails          += params[i].swap_val_fails;

        ins_successes           += params[i].ins_successes;
        del_successes           += params[i].del_successes;
        search_successes        += params[i].search_successes;
        search_by_val_successes += params[i].search_by_val_successes;
        swap_val_successes      += params[i].swap_val_successes;

        itter_inits             += params[i].itter_inits;
        itter_nexts             += params[i].itter_nexts;
        itter_ends              += params[i].itter_ends;
    }

#if 0 
    fprintf(stdout, "\nins / del / search fails = %lld / %lld / %lld\n", 
            ins_fails, del_fails, search_fails);
    fprintf(stdout, "\nins / del / search successes = %lld / %lld / %lld\n", 
            ins_successes, del_successes, search_successes);
    fprintf(stdout, "nodes allocated / freed / net = %lld / %lld / %lld.\n",
            atomic_load(&(lfht.num_nodes_allocated)),
            atomic_load(&(lfht.num_nodes_freed)),
            (atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))));
    fprintf(stdout, "lfsll_phys_len / fl_len / sum = %lld / %lld / %lld, lfsll_log_len = %lld.\n",
            atomic_load(&(lfht.lfsll_phys_len)), atomic_load(&(lfht.fl_len)),
            (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))),
            atomic_load(&(lfht.lfsll_log_len)));
#endif

#if 0 /* JRM */
    lfht_dump_list(&lfht, stdout);
#endif /* JRM */

#if 0 /* JRM */
    lfht_dump_stats(&lfht, stdout);
#endif /* JRM */

    lfht_verify_list_lens(&lfht);

    lfht_dump_interesting_stats(&lfht);

    assert((atomic_load(&(lfht.num_nodes_allocated)) - atomic_load(&(lfht.num_nodes_freed))) ==
           (atomic_load(&(lfht.lfsll_phys_len)) + atomic_load(&(lfht.fl_len))));

    assert(ins_fails            == atomic_load(&(lfht.insertion_failures)));
    assert(del_fails            == atomic_load(&(lfht.deletion_failures)));
    assert(search_fails         == atomic_load(&(lfht.failed_searches)));
    assert(search_by_val_fails  == atomic_load(&(lfht.failed_val_searches)));
    assert(swap_val_fails       == atomic_load(&(lfht.failed_val_swaps)));

    assert(ins_successes            == atomic_load(&(lfht.insertions)));
    assert(del_successes            == (atomic_load(&(lfht.deletion_starts)) + 
                                        atomic_load(&(lfht.deletion_start_cols))));
    assert(search_successes         == atomic_load(&(lfht.successful_searches)));
    assert(search_by_val_successes  == atomic_load(&(lfht.successful_val_searches)));
    assert(swap_val_successes       == atomic_load(&(lfht.successful_val_swaps)));

    assert(itter_inits == atomic_load(&(lfht.itter_inits)));
    assert(itter_nexts == atomic_load(&(lfht.itter_nexts)));
    assert(itter_ends  == atomic_load(&(lfht.itter_ends)));

    /* lfht.lfsll_log_len and lfht.lfsll_phys_len will vary */

    /* lfht.insertions & lfht.insertion_failures will vary */

    /* lfht.lfht.ins_restarts_due_to_ins_col and lfht.lfht.ins_restarts_due_to_del_col will vary */

    /* lfht.ins_deletion_completions and lfht.nodes_visited_during_ins will vary */

    /* lfht.deletion_attempts will vary */

    /* lfht.deletion_failures will vary */

    assert( (ins_successes - atomic_load(&(lfht.lfsll_log_len))) == 
            (atomic_load(&(lfht.ins_deletion_completions)) +
             atomic_load(&(lfht.del_deletion_completions)) +
             (atomic_load(&(lfht.lfsll_phys_len)) - 
              atomic_load(&(lfht.lfsll_log_len)) - 
              atomic_load(&(lfht.buckets_initialized)) - 1)) ); 

    /* lfht.deletion_start_cols and lfht.del_restarts_due_to_del_col) will vary */
    /* lfht.del_deletion_completions and lfht.nodes_visited_during_dels will vary */

    /* lfht.searches, lfht.successful_searches and lfht.failed_searches will vary. */

    assert(atomic_load(&(lfht.searches)) == (search_successes + search_fails));

    /* lfht.marked_nodes_visited_in_succ_searches, lfht.unmarked_nodes_visited_in_succ_searches
     * lfht.marked_nodes_visited_in_failed_searches, and 
     * lfht.unmarked_nodes_visited_in_failed_searches will vary
     */

    assert((search_by_val_fails + search_by_val_successes) == atomic_load(&(lfht.value_searches)));

    assert((swap_val_fails + swap_val_successes) == atomic_load(&(lfht.value_swaps)));

    assert(nthreads == itter_inits);
    /* number of itter_nexts varies */
    assert(nthreads == itter_ends);

    lfht_clear(&lfht);

    fprintf(stdout, " Done. \n");

    return;

} /* lfht_mt_test_3() */

#define RUN_LFSLL_TESTS 1

int main()
{
    int i;
    int nthreads = 8;
#if LFHT__USE_SPTR
    struct lfht_flsptr_t test = {NULL, 0x0ULL};
    _Atomic struct lfht_flsptr_t atest;

    atomic_init(&(atest), test);

    fprintf(stdout, "atomic_is_lock_free(&atest) = %d\n", (int)atomic_is_lock_free(&atest));
#endif /* LFHT__USE_SPTR */

    lfht_hash_fcn_test();
    lfht_hash_to_index_test();

#if RUN_LFSLL_TESTS
    lfht_lfsll_serial_test_1();
    lfht_lfsll_serial_test_2();
    lfht_lfsll_serial_test_3();
#endif

    lfht_serial_test_1();
    lfht_serial_test_2();
    lfht_serial_test_3();

#if RUN_LFSLL_TESTS
    lfht_lfsll_mt_test_fcn_1__serial_test();
    lfht_lfsll_mt_test_fcn_2__serial_test();
#endif

    lfht_mt_test_fcn_1__serial_test();
    lfht_mt_test_fcn_2__serial_test();
#if 0
    lfht_lfsll_mt_test_1(nthreads);
    lfht_lfsll_mt_test_2(nthreads);
    lfht_lfsll_mt_test_3(nthreads);
    lfht_mt_test_1(1, nthreads);
    lfht_mt_test_2(1, nthreads);
    lfht_mt_test_3(1, nthreads);

#else
    for ( nthreads = 1; nthreads < 32; nthreads++ ) {

#if RUN_LFSLL_TESTS
        lfht_lfsll_mt_test_1(nthreads);
        lfht_lfsll_mt_test_2(nthreads);
        lfht_lfsll_mt_test_3(nthreads);
#endif
        for ( i = 0; i < 100; i++) {

            lfht_mt_test_1(i, nthreads);
            lfht_mt_test_2(i, nthreads);
            lfht_mt_test_3(i, nthreads);
        }
    }
#endif

    fprintf(stdout, "\nLFHT tests complete.\n");

    return(0);

} /* main() */
