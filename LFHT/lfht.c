#include <assert.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

/************************************************************************
 *
 * lfht_add
 *
 * Insert a node with the supplied id and value into the indicated lock 
 * free hash table.  Fail and return false if the hash table already 
 * contains already contains a node with the supplied id. Observe that
 * this implies that the hash generated from the id is unique within
 * the range of valid ids.  
 * 
 * Return true on success.
 *
 *                                           JRM -- 6/30/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

bool lfht_add(struct lfht_t * lfht_ptr, unsigned long long int id, void * value)
{
    bool success;
    int index_bits;
    unsigned long long curr_buckets_defined;
    unsigned long long new_buckets_defined;
    unsigned long long int hash;
    struct lfht_node_t * bucket_head_ptr;
    struct lfht_fl_node_t * fl_node_ptr = NULL;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
#ifdef H5I__MT
    assert((id & ID_MASK) <= LFHT__MAX_ID);
#else /* H5I__MT */
    assert(id <= LFHT__MAX_ID);
#endif /* H5I__MT */

    fl_node_ptr = lfht_enter(lfht_ptr);

#ifdef H5I__MT
    hash = lfht_id_to_hash((id & ID_MASK), false);
#else /* H5I__MT */
    hash = lfht_id_to_hash(id, false);
#endif /* H5I__MT */

    bucket_head_ptr = lfht_get_hash_bucket_sentinel(lfht_ptr, hash);

    success = lfht_add_internal(lfht_ptr, bucket_head_ptr, id, hash, false, value, NULL);

    /* test to see if the logical length of the LFSLL has increased to 
     * the point that we should double the (logical) size of the bucket
     * index.  If so, increment the index_bits and buckets_defined 
     * fields accordingly.
     */

    index_bits = atomic_load(&(lfht_ptr->index_bits));
    curr_buckets_defined = 0x01ULL << index_bits;

    if ( index_bits < lfht_ptr->max_index_bits ) {

        if ( (atomic_load(&(lfht_ptr->lfsll_log_len)) / curr_buckets_defined) >= 8 ) {

            /* attempt to increment lfht_ptr->index_bits and lfht_buckets_defined.  Must do 
             * this with a compare and exchange, as it is likely that other threads will be 
             * attempting to do the same thing at more or less the same time.
             *
             * Do nothing if the compare and exchange fails, as that only means that 
             * another thread beat us to it.
             *
             * However, do collect stats.
             */

            if ( atomic_compare_exchange_strong(&(lfht_ptr->index_bits), &index_bits, index_bits + 1) ) {

                /* set of lfht_ptr->index_bits succeeded -- must update lfht_ptr->buckets_defined
                 * as well.  As it is possible that this update could be interleaved with another 
                 * index_bits increment, this can get somewhat involved.  
                 *
                 * For the first pass, use the computed current and new value for buckets defined
                 * in the call to atomic_compare_exchange_strong(). If this succeeds, we are done.
                 *
                 * Otherwise, load the current values of lfht_ptr->index_bits and 
                 * lfht_ptr->buckets_defined, and compute what lfht_ptr->buckets_defined should 
                 * be given the current value of lfht_ptr->index_bits.  If the two values 
                 * match, or if the actual value is greater than the computed value, we are 
                 * done.
                 *
                 * If not, call atomic_compare_exchange_strong() again to correct the value 
                 * of lfht_ptr->buckets_defined, and repeat as necessary.
                 */

                bool first = true;
                bool done = false;

                new_buckets_defined = curr_buckets_defined << 1;

                do {

                    if ( atomic_compare_exchange_strong(&(lfht_ptr->buckets_defined), &curr_buckets_defined,
                                                        new_buckets_defined) ) {

                        done = true;

                    } else {

                        if ( first ) {

                            first = false;
                            atomic_fetch_add(&(lfht_ptr->buckets_defined_update_cols), 1);
                        }

                        index_bits = atomic_load(&(lfht_ptr->index_bits));

                        assert(index_bits <= lfht_ptr->max_index_bits);

                        new_buckets_defined = 0x01ULL << index_bits;

                        curr_buckets_defined = atomic_load(&(lfht_ptr->buckets_defined));

                        if ( curr_buckets_defined >= new_buckets_defined ) {

                            done = true;

                        } else {

                            atomic_fetch_add(&(lfht_ptr->buckets_defined_update_retries), 1);
                        }
                    }
                } while ( ! done );
            } else {

                atomic_fetch_add(&(lfht_ptr->index_bits_incr_cols), 1);
            }
        }
    }

    lfht_exit(lfht_ptr, fl_node_ptr);

    return(success);

} /* lfht_add() */


/************************************************************************
 *
 * lfsll_add_internal
 *
 * **** INTERNAL USE ONLY - DO NOT CALL FROM OUTSIDE THE LFHT ****
 *
 * Intenal function to insert a node with the supplied value into the 
 * lock free singly linked list of the indicated lock free hash table.
 *
 * New entries are inserted into the LFSLL in increasing value order, 
 * using the sentinel node supplied in bucket_head_ptr as the starting 
 * point of the search for the target insertion point.  
 *
 * The function will fail and return false if the LFSLL already contains 
 * a node with the supplied hash value.  
 *
 * On success, the function will return true.  If the new_node_ptr_ptr 
 * parameter is not NULL, it will also set *new_node_ptr_ptr to point 
 * to the newly inserted node prior to returning.  This later function
 * exists to support insertion of new bucket sentinel nodes.
 *
 * In passing, the function will complete the deletion of nodes already 
 * marked for deletion when encountered and update stats accordingly.  
 * Similarly, update stats for restarts caused by collisions, 
 * successful or failed insrtions, etc.
 *
 *                                           JRM -- 6/30/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

bool lfht_add_internal(struct lfht_t * lfht_ptr, 
                       struct lfht_node_t * bucket_head_ptr,
                       unsigned long long int id, 
                       unsigned long long int hash, bool sentinel, void * value, 
                       struct lfht_node_t ** new_node_ptr_ptr)
{
    bool done = false;
    bool success = false;
    int del_completions = 0;
    int del_completion_cols = 0;
    int insertion_cols = 0;
    int nodes_visited = 0;
    unsigned long long int lfsll_log_len;
    unsigned long long int max_lfsll_log_len;
    unsigned long long int lfsll_phys_len;
    unsigned long long int max_lfsll_phys_len;
    struct lfht_node_t * new_node_ptr = NULL;
    struct lfht_node_t * first_node_ptr;
    struct lfht_node_t * second_node_ptr;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(bucket_head_ptr);
    assert(LFHT_VALID_NODE == bucket_head_ptr->tag);
    assert(bucket_head_ptr->sentinel);
    assert(bucket_head_ptr->hash < hash);
    assert(sentinel || (0x01ULL == (hash & 0x01ULL)));

    /* Allocate the new node now.  The objective is to
     * minimize the window between when lfmt_find_mod_point()
     * returns, and we actually perform the insertion.
     *
     * This has a cost -- as the new value may already exist,
     * in which case we must discard the node and return 
     * failure.
     */
    new_node_ptr = lfht_create_node(lfht_ptr, id, hash, sentinel, value);
    assert(new_node_ptr);
    assert(LFHT_VALID_NODE == new_node_ptr->tag);

    /* now do the insertion.  We repeat until we are successful,
     * or we discover that the sll already contains a node with 
     * the specified hash.
     */
    do {
        first_node_ptr = NULL;
        second_node_ptr = NULL;

        /* in its current implementation, lfsll_find_mod_point() will
         * either succeed or trigger an assertion -- thus no need to
         * check return value at present.
         */
        lfht_find_mod_point(lfht_ptr,
                            bucket_head_ptr,
                            &first_node_ptr,
                            &second_node_ptr,
                            &del_completion_cols,
                            &del_completions,
                            &nodes_visited,
                            hash);

        assert(first_node_ptr);

        if ( hash == first_node_ptr->hash ) { 

            /* value already exists in the SLL.  Discard the new node,
             * and report failure.  Note that we must mark new_node_ptr->next
             * to keet lfsll_discard_node() happy.
             */
            atomic_store(&(new_node_ptr->next), (struct lfht_node_t *)0x01ULL);
            lfht_discard_node(lfht_ptr, new_node_ptr, 0);
            new_node_ptr = NULL;
            done = true;
            success = false;

        } else {

            assert(second_node_ptr);

            /* load the new node next ptr with second_ptr */
            atomic_store(&(new_node_ptr->next), second_node_ptr);

            /* attempt to insert *new_node_ptr into the hash table's SLL */
            if ( atomic_compare_exchange_strong(&(first_node_ptr->next), &second_node_ptr, new_node_ptr) ) {

                /* insertion succeeded */

                /* increment the logical and physical length of the lfsll */
                if ( ! sentinel ) {

                    atomic_fetch_add(&(lfht_ptr->lfsll_log_len), 1);
                }
                atomic_fetch_add(&(lfht_ptr->lfsll_phys_len), 1);

                done = true;
                success = true;

            } else {

                insertion_cols++;
            }
        }
    } while ( ! done );

    if ( ( success ) && ( new_node_ptr_ptr ) ) {

        *new_node_ptr_ptr = new_node_ptr;
    }

    /* update statistics */

    if ( success ) {

        if ( ! sentinel ) {

            atomic_fetch_add(&(lfht_ptr->insertions), 1);

        }
        /* collect stats on successful sentinel insertions? */ /* JRM */

    } else {

        if ( ! sentinel ) {

            atomic_fetch_add(&(lfht_ptr->insertion_failures), 1);

         }
         /* collect stats on failed sentinel insertions? */ /* JRM */
    }

    /* if appropriate, attempt to update lfht_ptr->max_lfsll_log_len and lfht_ptr->max_lfsll_phys_len.  
     * In the event of a collision, just ignore it and go on, as I don't see any reasonable way to 
     * recover.
     */
    if ( (lfsll_log_len = atomic_load(&(lfht_ptr->lfsll_log_len))) > 
         (max_lfsll_log_len = atomic_load(&(lfht_ptr->max_lfsll_log_len))) ) {

        atomic_compare_exchange_strong(&(lfht_ptr->max_lfsll_log_len), &max_lfsll_log_len, lfsll_log_len);
    }

    if ( (lfsll_phys_len = atomic_load(&(lfht_ptr->lfsll_phys_len))) > 
         (max_lfsll_phys_len = atomic_load(&(lfht_ptr->max_lfsll_phys_len))) ) {

        atomic_compare_exchange_strong(&(lfht_ptr->max_lfsll_phys_len), &max_lfsll_phys_len, lfsll_phys_len);
    }

    assert(insertion_cols >= 0);
    assert(del_completion_cols >= 0);
    assert(del_completions >= 0);
    assert(nodes_visited >= 0);
    
    atomic_fetch_add(&(lfht_ptr->ins_restarts_due_to_ins_col), insertion_cols);
    atomic_fetch_add(&(lfht_ptr->ins_restarts_due_to_del_col), del_completion_cols);
    atomic_fetch_add(&(lfht_ptr->ins_deletion_completions),    del_completions);
    atomic_fetch_add(&(lfht_ptr->nodes_visited_during_ins),    nodes_visited);

    return(success);

} /* lfht_add_internal() */


/************************************************************************
 *
 * lfht_clear
 *
 *     Clear the supplied instance of lfht_t in preparation for deletion.
 *
 *                                           JRM -- 5/30/23
 *
 ************************************************************************/

void lfht_clear(struct lfht_t * lfht_ptr)
{
    unsigned long long marked_nodes_discarded = 0;
    unsigned long long unmarked_nodes_discarded = 0;
    unsigned long long sentinel_nodes_discarded = 0;
    struct lfht_node_t * discard_ptr = NULL;
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_discard_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr = NULL;
#if LFHT__USE_SPTR
    struct lfht_flsptr_t init_flsptr = {NULL, 0x0ULL};
    struct lfht_flsptr_t fl_shead;
    struct lfht_flsptr_t snext;
#endif /* LFHT__USE_SPTR */

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);

    /* Delete the elements of the LFSLL -- note that this moves 
     * all elements from the LFHT to the free list.
     */

    node_ptr = atomic_load(&(lfht_ptr->lfsll_root));

    atomic_store(&(lfht_ptr->lfsll_root), NULL);

    while ( node_ptr ) {

        assert(LFHT_VALID_NODE == node_ptr->tag);

        discard_ptr = node_ptr;
        node_ptr = atomic_load(&(discard_ptr->next));

        /* first test to see if the node is a sentinel node -- if it is, it must not 
         * be marked.
         */
        if ( discard_ptr->sentinel ) {

            /* sentinel nodes can't be marked for deletion -- verify this */
            assert(0x0ULL == (((unsigned long long)(discard_ptr->next)) & 0x01ULL));

            /* mark discard_ptr->next to keep lfht_discard_node() happy */
            atomic_store(&(discard_ptr->next), 
                         (struct lfht_node_t *)(((unsigned long long)(node_ptr)) | 0x01ULL));

            sentinel_nodes_discarded++;

        } else {

            /* test to see if node_ptr is marked.  If it, remove the
             * mark so we can use it.
             */
            if ( ((unsigned long long)(node_ptr)) & 0x01ULL ) {

                /* node_ptr is marked -- remove the mark and increment marked nodes visited */
                node_ptr = (struct lfht_node_t *)(((unsigned long long)(node_ptr)) & (~0x01ULL));

                marked_nodes_discarded++;

            } else {

                /* mark discard_ptr->next to keep lfht_discard_node() happy */
                atomic_store(&(discard_ptr->next), 
                             (struct lfht_node_t *)(((unsigned long long)(node_ptr)) | 0x01ULL));

                unmarked_nodes_discarded++;
            }
        }

        lfht_discard_node(lfht_ptr, discard_ptr, 0);
    }

    assert(atomic_load(&(lfht_ptr->buckets_initialized)) + 1 == sentinel_nodes_discarded);

    assert(atomic_load(&(lfht_ptr->lfsll_phys_len)) == 
           sentinel_nodes_discarded + marked_nodes_discarded + unmarked_nodes_discarded);

    assert(atomic_load(&(lfht_ptr->lfsll_log_len)) == unmarked_nodes_discarded);

    /* Now delete and free all items in the free list.  Do 
     * this directly, as lfht_discard_node() will try to 
     * put them back on the free list.
     */
#if LFHT__USE_SPTR
    fl_shead = atomic_load(&(lfht_ptr->fl_shead));
    fl_node_ptr = fl_shead.ptr;

    atomic_store(&(lfht_ptr->fl_shead), init_flsptr);
    atomic_store(&(lfht_ptr->fl_stail), init_flsptr);
#else /* LFHT__USE_SPTR */
    fl_node_ptr = atomic_load(&(lfht_ptr->fl_head));

    atomic_store(&(lfht_ptr->fl_head), NULL);
    atomic_store(&(lfht_ptr->fl_tail), NULL);
#endif /* LFHT__USE_SPTR */

    atomic_store(&(lfht_ptr->next_sn), 0ULL);

    while ( fl_node_ptr ) {

        assert(LFHT_FL_NODE_ON_FL == fl_node_ptr->tag);

        fl_discard_ptr = fl_node_ptr;
#if LFHT__USE_SPTR
        snext = atomic_load(&(fl_discard_ptr->snext));
        fl_node_ptr = snext.ptr;

        discard_ptr->tag = LFHT_FL_NODE_INVALID;

        snext.ptr = NULL;
        snext.sn  = 0ULL;
        atomic_store(&(fl_discard_ptr->snext), snext);
#else /* LFHT__USE_SPTR */
        fl_node_ptr = atomic_load(&(fl_discard_ptr->next));

        discard_ptr->tag = LFHT_FL_NODE_INVALID;
        atomic_store(&(fl_discard_ptr->next), NULL);
#endif /* LFHT__USE_SPTR */

        free((void *)fl_discard_ptr);
    }

    return;

} /* lfht_clear() */


/************************************************************************
 *
 * lfht_clear_stats()
 *
 *     Set all the stats fields in the supplied instance of lfht_t
 *     to zero..
 *
 *                           JRM -- 5/30/23
 *
 ************************************************************************/

void lfht_clear_stats(struct lfht_t * lfht_ptr)
{

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);

    atomic_store(&(lfht_ptr->max_lfsll_log_len), 0LL);
    atomic_store(&(lfht_ptr->max_lfsll_phys_len), 0LL);

    atomic_store(&(lfht_ptr->max_fl_len), 0LL);
    atomic_store(&(lfht_ptr->num_nodes_allocated), 0LL);
    atomic_store(&(lfht_ptr->num_nodes_freed), 0LL);
    atomic_store(&(lfht_ptr->num_node_free_candidate_selection_restarts), 0LL);
    atomic_store(&(lfht_ptr->num_nodes_added_to_fl), 0LL);
    atomic_store(&(lfht_ptr->num_nodes_drawn_from_fl), 0LL);
    atomic_store(&(lfht_ptr->num_fl_head_update_cols), 0LL);
    atomic_store(&(lfht_ptr->num_fl_tail_update_cols), 0LL);
    atomic_store(&(lfht_ptr->num_fl_append_cols), 0LL);
    atomic_store(&(lfht_ptr->num_fl_req_denied_due_to_empty), 0LL);
    atomic_store(&(lfht_ptr->num_fl_req_denied_due_to_ref_count), 0LL);
    atomic_store(&(lfht_ptr->num_fl_node_ref_cnt_incs), 0LL);
    atomic_store(&(lfht_ptr->num_fl_node_ref_cnt_inc_retrys), 0LL);
    atomic_store(&(lfht_ptr->num_fl_node_ref_cnt_decs), 0LL);
    atomic_store(&(lfht_ptr->num_fl_frees_skiped_due_to_empty), 0LL);
    atomic_store(&(lfht_ptr->num_fl_frees_skiped_due_to_ref_count), 0LL);

    atomic_store(&(lfht_ptr->index_bits_incr_cols), 0LL);
    atomic_store(&(lfht_ptr->buckets_defined_update_cols), 0LL);
    atomic_store(&(lfht_ptr->buckets_defined_update_retries), 0LL);
    atomic_store(&(lfht_ptr->bucket_init_cols), 0LL);
    atomic_store(&(lfht_ptr->bucket_init_col_sleeps), 0LL);
    atomic_store(&(lfht_ptr->recursive_bucket_inits), 0LL);
    atomic_store(&(lfht_ptr->sentinels_traversed), 0LL);

    atomic_store(&(lfht_ptr->insertions), 0LL);
    atomic_store(&(lfht_ptr->insertion_failures), 0LL);
    atomic_store(&(lfht_ptr->ins_restarts_due_to_ins_col), 0LL);
    atomic_store(&(lfht_ptr->ins_restarts_due_to_del_col), 0LL);
    atomic_store(&(lfht_ptr->ins_deletion_completions), 0LL);
    atomic_store(&(lfht_ptr->nodes_visited_during_ins), 0LL);

    atomic_store(&(lfht_ptr->deletion_attempts), 0LL);
    atomic_store(&(lfht_ptr->deletion_starts), 0LL);
    atomic_store(&(lfht_ptr->deletion_start_cols), 0LL);
    atomic_store(&(lfht_ptr->deletion_failures), 0LL);
    atomic_store(&(lfht_ptr->del_restarts_due_to_del_col), 0LL);
    atomic_store(&(lfht_ptr->del_retries), 0LL);
    atomic_store(&(lfht_ptr->del_deletion_completions), 0LL);
    atomic_store(&(lfht_ptr->nodes_visited_during_dels), 0LL);

    atomic_store(&(lfht_ptr->searches), 0LL);
    atomic_store(&(lfht_ptr->successful_searches), 0LL);
    atomic_store(&(lfht_ptr->failed_searches), 0LL);
    atomic_store(&(lfht_ptr->marked_nodes_visited_in_succ_searches), 0LL);
    atomic_store(&(lfht_ptr->unmarked_nodes_visited_in_succ_searches), 0LL);
    atomic_store(&(lfht_ptr->marked_nodes_visited_in_failed_searches), 0LL);
    atomic_store(&(lfht_ptr->unmarked_nodes_visited_in_failed_searches), 0LL);

    atomic_store(&(lfht_ptr->value_swaps), 0LL);
    atomic_store(&(lfht_ptr->successful_val_swaps), 0LL);
    atomic_store(&(lfht_ptr->failed_val_swaps), 0LL);
    atomic_store(&(lfht_ptr->marked_nodes_visited_in_succ_val_swaps), 0LL);
    atomic_store(&(lfht_ptr->unmarked_nodes_visited_in_succ_val_swaps), 0LL);
    atomic_store(&(lfht_ptr->marked_nodes_visited_in_failed_val_swaps), 0LL);
    atomic_store(&(lfht_ptr->unmarked_nodes_visited_in_failed_val_swaps), 0LL);

    atomic_store(&(lfht_ptr->value_searches), 0LL);
    atomic_store(&(lfht_ptr->successful_val_searches), 0LL);
    atomic_store(&(lfht_ptr->failed_val_searches), 0LL);
    atomic_store(&(lfht_ptr->marked_nodes_visited_in_val_searches), 0LL);
    atomic_store(&(lfht_ptr->unmarked_nodes_visited_in_val_searches), 0LL);
    atomic_store(&(lfht_ptr->sentinels_traversed_in_val_searches), 0LL);

    atomic_store(&(lfht_ptr->itter_inits), 0LL);
    atomic_store(&(lfht_ptr->itter_nexts), 0LL);
    atomic_store(&(lfht_ptr->itter_ends), 0LL);
    atomic_store(&(lfht_ptr->marked_nodes_visited_in_itters), 0LL);
    atomic_store(&(lfht_ptr->unmarked_nodes_visited_in_itters), 0LL);
    atomic_store(&(lfht_ptr->sentinels_traversed_in_itters), 0LL);

    return;

} /* lfht_clear_stats() */


/************************************************************************
 *
 * lfht_create_hash_bucket() 
 *
 *     Create a hash bucket for the supplied hash and number of hash 
 *     bucket table index bits.  
 *
 *     To do this, find the hash bucket for the same hash but with 
 *     index_bits minus 1, and use that hash bucket to find the insertion 
 *     point for the new bucket in the LFSLL.  
 *
 *     Note that it is possible that the index bucket for the supplied
 *     hash but with index_bits minus 1 may not exist -- in which case
 *     a recursive call is made.  Further, it may be that hash buckets
 *     for the given hash and both index_bits and index_bits minus 1
 *     are the same -- in which case there will be nothing to do when 
 *     the recursive call returns.
 *
 *                                          JRM -- 6/30/23
 *
 ************************************************************************/

void lfht_create_hash_bucket(struct lfht_t * lfht_ptr, unsigned long long int hash, int index_bits)
{
    unsigned long long int target_index;
    unsigned long long int target_hash;
    unsigned long long int parent_index;
    struct lfht_node_t * bucket_head_ptr;
    struct lfht_node_t * sentinel_ptr = NULL;
    struct lfht_node_t * null_ptr = NULL;

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(index_bits > 0);
    
    target_index = lfht_hash_to_idx(hash, index_bits);
    parent_index = lfht_hash_to_idx(hash, index_bits - 1);

    if ( NULL == atomic_load(&(lfht_ptr->bucket_idx[target_index])) ) {

        if ( NULL == atomic_load(&(lfht_ptr->bucket_idx[parent_index])) ) {

            /* parent bucket doesn't exist either -- make a recursive call */

            lfht_create_hash_bucket(lfht_ptr, hash, index_bits - 1);

            atomic_fetch_add(&(lfht_ptr->recursive_bucket_inits), 1);
        }

        assert(NULL != (bucket_head_ptr = atomic_load(&(lfht_ptr->bucket_idx[parent_index]))));

        /* it is possible that parent_index == target_index -- hence the following check */

        if ( NULL == atomic_load(&(lfht_ptr->bucket_idx[target_index])) ) {

            target_hash = lfht_id_to_hash(target_index, true);

            assert(target_index == (lfht_id_to_hash(target_hash >> 1, true) >> 1));

            if ( lfht_add_internal(lfht_ptr, bucket_head_ptr, 0ULL, 
                                   target_hash, true, NULL, &sentinel_ptr) ) {

                /* creation of the sentinel node for the hash bucket succeeded.
                 * now store a pointer to the new node in the bucket index.
                 */
                assert(sentinel_ptr);
                assert(LFHT_VALID_NODE == sentinel_ptr->tag);
                assert(0x0ULL == sentinel_ptr->id);
                assert(target_hash == sentinel_ptr->hash);
                assert(sentinel_ptr->sentinel);
                assert(NULL == atomic_load(&(sentinel_ptr->value)));

                /* set lfht_ptr->bucket_idx[target_index].  Do this via atomic_compare_exchange_strong(). */
                /* assert that this succeeds, as it should be impossible for it to fail */
                assert(atomic_compare_exchange_strong(&(lfht_ptr->bucket_idx[target_index]), 
                                                      &null_ptr, sentinel_ptr) );

                atomic_fetch_add(&(lfht_ptr->buckets_initialized), 1);

            } else {

                /* the attempt to insert the new sentinel node failed -- which means that 
                 * that the node already exists.  Thus if it hasn't been set already, 
                 * lfht_ptr->bucket_idx[target_index] will be set to point to the new 
                 * sentinel shortly.
                 */

                atomic_fetch_add(&(lfht_ptr->bucket_init_cols), 1);
            
                while ( NULL == atomic_load(&(lfht_ptr->bucket_idx[target_index])) ) {

                    /* need to do better than this.  Want to call pthread_yield(),
                     * but that call doesn't seem to be supported anymore.
                     */
                    sleep(1);
               
                    atomic_fetch_add(&(lfht_ptr->bucket_init_col_sleeps), 1);
                }
            }
        }
    } else {

        /* Another thread beat us to defining the new bucket.  
         *
         * As there is nothing to back out of, I don't think this qualifies as a 
         * a collision -- hence no stats for this case.
         */
    }

    return;

} /* lfht_create_hash_bucket() */


/************************************************************************
 *
 * lfht_create_node
 *
 *     Test to see if an instance of lfht_fl_node_t is available on the 
 *     free list.  If there is remove it from the free list, re-initialize 
 *     it, and return a pointer to the include instance of lfht_node_t
 *     the the caller.
 *
 *     Otherwise, allocate and initialize an instance of struct 
 *     lfht_fl_node_t and return a pointer to the included instance of 
 *     lfht_node_t to the caller.  
 *
 *     Return a pointer to the new instance on success, and NULL on 
 *     failure.
 *
 *                                          JRM -- 6/30/23
 *
 ************************************************************************/

struct lfht_node_t * lfht_create_node(struct lfht_t * lfht_ptr, unsigned long long int id, 
                                      unsigned long long int hash, bool sentinel, void * value)
{
    bool fl_search_done = false;
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr = NULL;
#if LFHT__USE_SPTR
    struct lfht_flsptr_t sfirst;
    struct lfht_flsptr_t new_sfirst;
    struct lfht_flsptr_t test_sfirst;
    struct lfht_flsptr_t slast;
    struct lfht_flsptr_t new_slast;
    struct lfht_flsptr_t snext;
    struct lfht_flsptr_t new_snext;
#else /* LFHT__USE_SPTR */
    bool new_node = false;
    struct lfht_fl_node_t * first = NULL;
    struct lfht_fl_node_t * last = NULL;
    struct lfht_fl_node_t * next = NULL;
#endif /* LFHT__USE_SPTR */

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);

    if ( hash > LFHT__MAX_HASH ) {

        fprintf(stderr, "hash = 0x%llx, LFHT__MAX_HASH = 0x%llx\n", hash, LFHT__MAX_HASH);
    }
    assert(hash <= LFHT__MAX_HASH);

#if LFHT__USE_SPTR
    sfirst = atomic_load(&(lfht_ptr->fl_shead));
    if ( NULL == sfirst.ptr ) {

        /* the free list hasn't been initialized yet, so skip
         * the search of the free list.
         */
       fl_search_done = true;
    }

    while ( ! fl_search_done ) {

        sfirst = atomic_load(&(lfht_ptr->fl_shead));
        slast = atomic_load(&(lfht_ptr->fl_stail));

        assert(sfirst.ptr);
        assert(slast.ptr);

        snext = atomic_load(&(sfirst.ptr->snext));

        test_sfirst = atomic_load(&(lfht_ptr->fl_shead));
        if ( ( test_sfirst.ptr == sfirst.ptr ) && ( test_sfirst.sn == sfirst.sn ) ) {

            if ( sfirst.ptr == slast.ptr ) {

                if ( NULL == snext.ptr ) {

                    /* the free list is empty */
                    atomic_fetch_add(&(lfht_ptr->num_fl_req_denied_due_to_empty), 1);
                    fl_search_done = true;
                    break;

                } 

                /* attempt to set lfht_ptr->fl_tail to next.  It doesn't 
                 * matter whether we succeed or fail, as if we fail, it 
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                new_slast.ptr = snext.ptr;
                new_slast.sn  = slast.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_stail), &slast, new_slast) ) {

                    atomic_fetch_add(&(lfht_ptr->num_fl_tail_update_cols), 1);
                }
            } else {

                /* set up new_sfirst now in case we need it later.  */
                assert(snext.ptr);
                new_sfirst.ptr = snext.ptr;
                new_sfirst.sn  = sfirst.sn + 1;

                if ( atomic_load(&(sfirst.ptr->ref_count)) > 0 ) {

                    /* The ref count on the entry at the head of the free list 
                     * has a positive ref count, which means that there may be 
                     * a pointer to it somewhere.  Rather than take the risk, 
                     * let it sit on the free list until the ref count drops 
                     * to zero.
                     */
                    atomic_fetch_add(&(lfht_ptr->num_fl_req_denied_due_to_ref_count), 1);
                    fl_search_done = true;

                } else if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_shead), &sfirst, new_sfirst) ) {

                    /* the attempt to remove the first item from the free list
                     * failed.  Update stats and try again.
                     */
                    atomic_fetch_add(&(lfht_ptr->num_fl_head_update_cols), 1);

                } else {

                    /* first has been removed from the free list.  Set fl_node_ptr to first,
                     * update stats, and exit the loop by setting fl_search_done to true.
                     */
                    fl_node_ptr = sfirst.ptr;

                    atomic_store(&(fl_node_ptr->tag), LFHT_FL_NODE_IN_USE);
                    assert( 0x0ULL == atomic_load(&(fl_node_ptr->ref_count)));

                    new_snext.ptr = NULL;
                    new_snext.sn  = snext.sn + 1;

                    assert(atomic_compare_exchange_strong(&(fl_node_ptr->snext), &snext, new_snext));

#else /* LFHT__USE_SPTR */
    if ( NULL == atomic_load(&(lfht_ptr->fl_head)) ) {

        /* the free list hasn't been initialized yet, so skip
         * the search of the free list.
         */
       fl_search_done = true;
    }

    while ( ! fl_search_done ) {

        first = atomic_load(&(lfht_ptr->fl_head));
        last = atomic_load(&(lfht_ptr->fl_tail));

        assert(first);
        assert(last);

        next = atomic_load(&(first->next));

        if ( first == atomic_load(&(lfht_ptr->fl_head)) ) {

            if ( first == last ) {

                if ( NULL == next ) {

                    /* the free list is empty */
                    atomic_fetch_add(&(lfht_ptr->num_fl_req_denied_due_to_empty), 1);
                    fl_search_done = true;
                    break;

                } 

                /* attempt to set lfht_ptr->fl_tail to next.  It doesn't 
                 * matter whether we succeed or fail, as if we fail, it 
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_tail), &first, next) ) {

                    atomic_fetch_add(&(lfht_ptr->num_fl_tail_update_cols), 1);
                }
            } else {

                if ( atomic_load(&(first->ref_count)) > 0 ) {

                    /* The ref count on the entry at the head of the free list 
                     * has a positive ref count, which means that there may be 
                     * a pointer to it somewhere.  Rather than take the risk, 
                     * let it sit on the free list until the ref count drops 
                     * to zero.
                     */
                    atomic_fetch_add(&(lfht_ptr->num_fl_req_denied_due_to_ref_count), 1);
                    fl_search_done = true;

                } else if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_head), &first, next) ) {

                    /* the attempt to remove the first item from the free list
                     * failed.  Update stats and try again.
                     */
                    atomic_fetch_add(&(lfht_ptr->num_fl_head_update_cols), 1);

                } else {

                    /* first has been removed from the free list.  Set fl_node_ptr to first,
                     * update stats, and exit the loop by setting fl_search_done to true.
                     */
                    fl_node_ptr = first;

                    atomic_store(&(fl_node_ptr->tag), LFHT_FL_NODE_IN_USE);
                    assert( 0x0ULL == atomic_load(&(fl_node_ptr->ref_count)));
                    assert(atomic_load(&(fl_node_ptr->next))); 

                    /* don't set fl_node_ptr->next to NULL to avoid setting up an ABA bug */

#endif /* LFHT__USE_SPTR */

                    node_ptr = (struct lfht_node_t *)fl_node_ptr;

                    assert(node_ptr);

                    node_ptr->tag = LFHT_VALID_NODE;
                    atomic_store(&(node_ptr->next), NULL);
                    node_ptr->id = id;
                    node_ptr->hash = hash;
                    node_ptr->sentinel = sentinel;
                    atomic_store(&(node_ptr->value), value);

                    atomic_fetch_sub(&(lfht_ptr->fl_len), 1);
                    atomic_fetch_add(&(lfht_ptr->num_nodes_drawn_from_fl), 1);

                    fl_search_done = true;
                }
            } 
        }
    } /* while ( ! fl_search_done ) */

    if ( NULL == fl_node_ptr ) {

        fl_node_ptr = (struct lfht_fl_node_t *)malloc(sizeof(struct lfht_fl_node_t));

        assert(fl_node_ptr);

        atomic_fetch_add(&(lfht_ptr->num_nodes_allocated), 1);

        atomic_init(&(fl_node_ptr->tag), LFHT_FL_NODE_IN_USE);
        atomic_init(&(fl_node_ptr->ref_count), 0);
        atomic_init(&(fl_node_ptr->sn), 0ULL);
#if LFHT__USE_SPTR
        snext.ptr = NULL;
        snext.sn  = 0ULL;
        atomic_init(&(fl_node_ptr->snext), snext);
#else /* LFHT__USE_SPTR */
        atomic_init(&(fl_node_ptr->next), NULL);
#endif /* LFHT__USE_SPTR */

        node_ptr = (struct lfht_node_t *)fl_node_ptr;

        assert(node_ptr);

        node_ptr->tag = LFHT_VALID_NODE;
        atomic_init(&(node_ptr->next), NULL);
        node_ptr->id = id;
        node_ptr->hash = hash;
        node_ptr->sentinel = sentinel;
        atomic_init(&(node_ptr->value), value);
    }
    
    assert(fl_node_ptr);

    return(node_ptr);

} /* lfht_create_node() */


/************************************************************************
 *
 * lfht_delete
 *
 * Attenpt to find the target node in the lfht.
 *
 * If it is not found, return false.
 *
 * If it is found, attempt to mark it for deletion.
 *
 * In passing, complete the deletion of any nodes encountered that are
 * already marked for deletion, and update stats accordingly.  Similarly, 
 * update stats for restarts caused by collisions, successful or failed 
 * deletions, etc.
 *
 *                                           JRM -- 6/13/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

bool lfht_delete(struct lfht_t * lfht_ptr, unsigned long long int id)
{
    bool done = false;
    bool success = false;
    int del_completions = 0;
    int del_completion_cols = 0;
    int del_init_cols = 0;
    int del_retries = 0;
    int nodes_visited = 0;
    unsigned long long int hash;
    struct lfht_node_t * bucket_head_ptr;;
    struct lfht_node_t * first_node_ptr;
    struct lfht_node_t * second_node_ptr;
    struct lfht_node_t * marked_second_node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
#ifdef H5I__MT
    assert((id & ID_MASK) <= LFHT__MAX_ID);
#else /* H5I__MT */
    assert(id <= LFHT__MAX_ID);
#endif /* H5I__MT */

    fl_node_ptr = lfht_enter(lfht_ptr);

#ifdef H5I__MT
    hash = lfht_id_to_hash((id & ID_MASK), false);
#else /* H5I__MT */
    hash = lfht_id_to_hash(id, false);
#endif /* H5I__MT */

    bucket_head_ptr = lfht_get_hash_bucket_sentinel(lfht_ptr, hash);

    do { 
        first_node_ptr = NULL;
        second_node_ptr = NULL;
        /* attempt to find the target */

        /* in its current implementation, lfht_find_mod_point() will
         * either succeed or trigger an assertion -- thus no need to
         * check return value at present.
         */
        lfht_find_mod_point(lfht_ptr,
                            bucket_head_ptr,
                            &first_node_ptr,
                            &second_node_ptr,
                            &del_completion_cols,
                            &del_completions,
                            &nodes_visited,
                            hash);

        assert(first_node_ptr);

        if ( hash == first_node_ptr->hash ) { 

            assert(!first_node_ptr->sentinel);
            assert(id == first_node_ptr->id);

            /* hash exists in the SLL.  Attempt to mark the 
             * node for deletion.  If we fail, that means that either:
             *
             * 1) another thread has beat us to marking *first_node_ptr as deleted.
             *
             * 2) another thread has either inserted a new node just after *first_node_ptr
             *    or physically deleted *second_node_ptr.
             *
             * No worries if the former, but in latter case, we must try again.
             */
            marked_second_node_ptr = (struct lfht_node_t *)(((unsigned long long)(second_node_ptr)) | 0x01ULL);

            if ( atomic_compare_exchange_strong(&(first_node_ptr->next), &second_node_ptr, 
                                                marked_second_node_ptr) ) {

                /* decrement the logical lfsll length.  We will decrement the physical list 
                 * length when the node is physically deleted from the list.
                 */
                atomic_fetch_sub(&(lfht_ptr->lfsll_log_len), 1);

                success = true;
                done = true;

            } else if ( 0 != (((unsigned long long)(second_node_ptr)) & 0x01ULL) ) {

                /* recall that atomic_compare_exchamge_strong replaces the expected value
                 * with the actual value on failure.  If the low order bit is set, we are 
                 * in case 1) above -- another thread beat us to marking *first_node_ptr
                 * as deleted.
                 */

                success = true;
                done = true;
                del_init_cols++;

            } else {

                /* a node has been added or deleted just after *first_node_ptr.  Must
                 * retry the deletion.
                 */
                del_retries++;
            }
        } else {

            /* target not in lfht */

            success = false;
            done = true;
        }
    }
    while ( ! done );

    /* update statistics */

    assert(del_init_cols >= 0);
    assert(del_completion_cols >= 0);
    assert(del_completions >= 0);
    assert(nodes_visited >= 0);
    assert(del_retries >= 0);
    
    atomic_fetch_add(&(lfht_ptr->deletion_attempts), 1);

    if ( success ) {

        if ( del_init_cols == 0 ) {

            atomic_fetch_add(&(lfht_ptr->deletion_starts), 1);

        } else {

            atomic_fetch_add(&(lfht_ptr->deletion_start_cols), 1);
       }
    } else {

        atomic_fetch_add(&(lfht_ptr->deletion_failures), 1);
    }

    atomic_fetch_add(&(lfht_ptr->del_retries),                 (long long)del_retries);
    atomic_fetch_add(&(lfht_ptr->del_restarts_due_to_del_col), (long long)del_completion_cols);
    atomic_fetch_add(&(lfht_ptr->del_deletion_completions),    (long long)del_completions);
    atomic_fetch_add(&(lfht_ptr->nodes_visited_during_dels),   (long long)nodes_visited);

    lfht_exit(lfht_ptr, fl_node_ptr);

    return(success);

} /* lfht_delete() */


/************************************************************************
 *
 * lfht_discard_node
 *
 *     Append the supplied instance of lfht_node_t 
 *     (really lfht_fl_node_t) to the free list and increment 
 *     lfht_ptr->fl_len.
 *
 *     If the free list length exceeds lfht_ptr->max_desired_fl_len,
 *     attempt the remove the node at the head of the free list from 
 *     the free list, and discard it and decrement lfht_ptr->fl_len
 *     if successful
 *
 *                                          JRM -- 6/30/23
 *
 ************************************************************************/

void lfht_discard_node(struct lfht_t * lfht_ptr, struct lfht_node_t * node_ptr, unsigned int expected_ref_count)
{
    bool done = false;
    unsigned int in_use_tag = LFHT_FL_NODE_IN_USE;
    long long int fl_len;
    long long int max_fl_len;
    struct lfht_node_t * next;
    struct lfht_fl_node_t * fl_node_ptr;
#if LFHT__USE_SPTR
    struct lfht_flsptr_t snext = {NULL, 0ULL};
    struct lfht_flsptr_t fl_stail;
    struct lfht_flsptr_t fl_snext;
    struct lfht_flsptr_t new_fl_snext;
    struct lfht_flsptr_t test_fl_stail;
    struct lfht_flsptr_t new_fl_stail;
#else /* LFHT__USE_SPTR */
    struct lfht_fl_node_t * fl_tail;
    struct lfht_fl_node_t * fl_next;
#endif /* LFHT__USE_SPTR */

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(node_ptr);
    assert(node_ptr->tag == LFHT_VALID_NODE);

    next = atomic_load(&(node_ptr->next));

    assert(0x01ULL == (((unsigned long long)(next)) & 0x01ULL));

    fl_node_ptr = (struct lfht_fl_node_t *)node_ptr;

    assert(LFHT_FL_NODE_IN_USE == atomic_load(&(fl_node_ptr->tag)));
    assert(expected_ref_count == atomic_load(&(fl_node_ptr->ref_count)));

#if LFHT__USE_SPTR
    snext = atomic_load(&(fl_node_ptr->snext));
    assert(NULL == snext.ptr);
#else /* LFHT__USE_SPTR */
    /* fl_node_ptr->next may or may not be NULL, depending on whether it was 
     * allocated directly from the heap, or from the free list.  In either 
     * case, we must set it to NULL before appending it to the free list.
     */
    atomic_store(&(fl_node_ptr->next), NULL);
#endif /* LFHT__USE_SPTR */

    assert( atomic_compare_exchange_strong(&(fl_node_ptr->tag), &in_use_tag, LFHT_FL_NODE_ON_FL) );

    atomic_store(&(fl_node_ptr->sn), atomic_fetch_add(&(lfht_ptr->next_sn), 1));

    while ( ! done ) {

#if LFHT__USE_SPTR
        fl_stail = atomic_load(&(lfht_ptr->fl_stail));

        assert(fl_stail.ptr);

        /* it is possible that *fl_tail.ptr has passed through the free list 
         * and been re-allocated between the time we loaded it, and now.
         * If so, fl_stail_ptr->tag will no longer be LFHT_FL_NODE_ON_FL.
         * This isn't a problem, as if so, the following if statement will fail.
         */
        // assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_stail.ptr->tag)));

        fl_snext = atomic_load(&(fl_stail.ptr->snext));

        test_fl_stail = atomic_load(&(lfht_ptr->fl_stail));

        if ( ( test_fl_stail.ptr == fl_stail.ptr ) && ( test_fl_stail.sn == fl_stail.sn ) ) {

            if ( NULL == fl_snext.ptr ) {

                /* attempt to append fl_node_ptr by setting fl_tail->next to fl_node_ptr.  
                 * If this succeeds, update stats and attempt to set lfht_ptr->fl_tail
                 * to fl_node_ptr as well.  This may or may not succeed, but in either 
                 * case we are done.
                 */
                new_fl_snext.ptr = fl_node_ptr;
                new_fl_snext.sn  = fl_snext.sn + 1;
                if ( atomic_compare_exchange_strong(&(fl_stail.ptr->snext), &fl_snext, new_fl_snext) ) {

                    atomic_fetch_add(&(lfht_ptr->fl_len), 1);
                    atomic_fetch_add(&(lfht_ptr->num_nodes_added_to_fl), 1);

                    new_fl_stail.ptr = fl_node_ptr;
                    new_fl_stail.sn  = fl_stail.sn + 1;
                    if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_stail), &fl_stail, new_fl_stail) ) {
#else /* LFHT__USE_SPTR */
        fl_tail = atomic_load(&(lfht_ptr->fl_tail));

        assert(fl_tail);
        assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_tail->tag)));

        fl_next = atomic_load(&(fl_tail->next));

        if ( fl_tail == atomic_load(&(lfht_ptr->fl_tail)) ) {

            if ( NULL == fl_next ) {

                /* attempt to append fl_node_ptr by setting fl_tail->next to fl_node_ptr.  
                 * If this succeeds, update stats and attempt to set lfht_ptr->fl_tail
                 * to fl_node_ptr as well.  This may or may not succeed, but in either 
                 * case we are done.
                 */
                if ( atomic_compare_exchange_strong(&(fl_tail->next), &fl_next, fl_node_ptr) ) {

                    atomic_fetch_add(&(lfht_ptr->fl_len), 1);
                    atomic_fetch_add(&(lfht_ptr->num_nodes_added_to_fl), 1);

                    if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_tail), &fl_tail, fl_node_ptr) ) {
#endif /* LFHT__USE_SPTR */

                        atomic_fetch_add(&(lfht_ptr->num_fl_tail_update_cols), 1);
                    }

                    /* if appropriate, attempt to update lfht_ptr->max_fl_len.  In the
                     * event of a collision, just ignore it and go on, as I don't see any 
                     * reasonable way to recover.
                     */
                    if ( (fl_len = atomic_load(&(lfht_ptr->fl_len))) > 
                         (max_fl_len = atomic_load(&(lfht_ptr->max_fl_len))) ) {

                        atomic_compare_exchange_strong(&(lfht_ptr->max_fl_len), &max_fl_len, fl_len);
                    }

                    done = true;

                } else {

                    /* append failed -- update stats and try again */
                    atomic_fetch_add(&(lfht_ptr->num_fl_append_cols), 1);

                }
            } else {

                // assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_next->tag)));

#if LFHT__USE_SPTR
                /* attempt to set lfht_ptr->fl_stail to fl_next.  It doesn't 
                 * matter whether we succeed or fail, as if we fail, it 
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                new_fl_stail.ptr = fl_snext.ptr;
                new_fl_stail.sn  = fl_stail.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_stail), &fl_stail, new_fl_stail) ) {
#else /* LFHT__USE_SPTR */
                /* attempt to set lfht_ptr->fl_tail to fl_next.  It doesn't 
                 * matter whether we succeed or fail, as if we fail, it 
                 * just means that some other thread beat us to it.
                 *
                 * that said, it doesn't hurt to collect stats
                 */
                if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_tail), &fl_tail, fl_next) ) {
#endif /* LFHT__USE_SPTR */

                    atomic_fetch_add(&(lfht_ptr->num_fl_tail_update_cols), 1);
                }
            }
        }
    }
#if 0 /* turn off node frees for now */
    /* Test to see if the free list is longer than the max_desired_fl_len. 
     * If so, attempt to remove an entry from the head of the free list
     * and discard it.  No worries if this fails, as the max_desired_fl_len
     * is a soft limit.
     */
    if ( atomic_load(&(lfht_ptr->fl_len)) > lfht_ptr->max_desired_fl_len ) {

        bool fl_search_done = false;
        struct lfht_fl_node_t * first;
        struct lfht_fl_node_t * last;
        struct lfht_fl_node_t * next;
        struct lfht_fl_node_t * discard_fl_node_ptr = NULL;
        struct lfht_node_t * discard_node_ptr = NULL;
#if LFHT__USE_SPTR
        struct lfht_flsptr_t sfirst;
        struct lfht_flsptr_t new_sfirst;
        struct lfht_flsptr_t test_sfirst;
        struct lfht_flsptr_t slast;
        struct lfht_flsptr_t new_slast;
        struct lfht_flsptr_t snext;
#endif /* LFHT__USE_SPTR */

        while ( ! fl_search_done ) {

#if LFHT__USE_SPTR
            sfirst = atomic_load(&(lfht_ptr->fl_shead));
            slast = atomic_load(&(lfht_ptr->fl_stail));

            assert(sfirst.ptr);
            assert(slast.ptr);

            snext = atomic_load(&(sfirst.ptr->snext));

            test_sfirst = atomic_load(&(lfht_ptr->fl_shead));
            if ( ( test_sfirst.ptr == sfirst.ptr ) && ( test_sfirst.sn == sfirst.sn ) ) {

                if ( sfirst.ptr == slast.ptr ) {

                    if ( NULL == snext.ptr ) {

                        /* the free list is empty */
                        atomic_fetch_add(&(lfht_ptr->num_fl_frees_skiped_due_to_empty), 1);
                        fl_search_done = true;
                        break;

                    } 

                    /* attempt to set lfht_ptr->fl_stail to next.  It doesn't 
                     * matter whether we succeed or fail, as if we fail, it 
                     * just means that some other thread beat us to it.
                     *
                     * that said, it doesn't hurt to collect stats
                     */
                    assert(snext.ptr);
                    new_slast.ptr = snext.ptr;
                    new_slast.sn  = slast.sn + 1;
                    if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_stail), &slast, new_slast) ) {

                        atomic_fetch_add(&(lfht_ptr->num_fl_tail_update_cols), 1);
                    }
                } else {

                    /* setup new_sfirst now in case we need it.  */
                    assert(snext.ptr);
                    new_sfirst.ptr = snext.ptr;
                    new_sfirst.sn  = sfirst.sn + 1;
                    if ( atomic_load(&(sfirst.ptr->ref_count)) > 0 ) {

                        /* The ref count on the entry at the head of the free list 
                         * has a positive ref count, which means that there may be 
                         * a pointer to it somewhere.  Rather than take the risk, 
                         * let it sit on the free list until the ref count drops 
                         * to zero.
                         */
                        atomic_fetch_add(&(lfht_ptr->num_fl_frees_skiped_due_to_ref_count), 1);
                        fl_search_done = true;

                    } else if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_shead), &sfirst, new_sfirst) ) {

                        /* the attempt to remove the first item from the free list
                         * failed.  Update stats and try again.
                         */
                        atomic_fetch_add(&(lfht_ptr->num_fl_head_update_cols), 1);
                        atomic_fetch_add(&(lfht_ptr->num_node_free_candidate_selection_restarts), 1);

                    } else {

                        /* first has been removed from the free list.  Set discard_fl_node_ptr to first,
                         * update stats, and exit the loop by setting fl_search_done to true.
                         */
                        discard_fl_node_ptr = sfirst.ptr;
#else /* LFHT__USE_SPTR */
            first = atomic_load(&(lfht_ptr->fl_head));
            last = atomic_load(&(lfht_ptr->fl_tail));

            assert(first);
            assert(last);

            next = atomic_load(&(first->next));

            if ( first == atomic_load(&(lfht_ptr->fl_head)) ) {

                if ( first == last ) {

                    if ( NULL == next ) {

                        /* the free list is empty */
                        atomic_fetch_add(&(lfht_ptr->num_fl_frees_skiped_due_to_empty), 1);
                        fl_search_done = true;
                        break;

                    } 

                    /* attempt to set lfht_ptr->fl_tail to next.  It doesn't 
                     * matter whether we succeed or fail, as if we fail, it 
                     * just means that some other thread beat us to it.
                     *
                     * that said, it doesn't hurt to collect stats
                     */
                    if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_tail), &first, next) ) {

                        atomic_fetch_add(&(lfht_ptr->num_fl_tail_update_cols), 1);
                    }
                } else {

                    if ( atomic_load(&(first->ref_count)) > 0 ) {

                        /* The ref count on the entry at the head of the free list 
                         * has a positive ref count, which means that there may be 
                         * a pointer to it somewhere.  Rather than take the risk, 
                         * let it sit on the free list until the ref count drops 
                         * to zero.
                         */
                        atomic_fetch_add(&(lfht_ptr->num_fl_frees_skiped_due_to_ref_count), 1);
                        fl_search_done = true;

                    } else if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_head), &first, next) ) {

                        /* the attempt to remove the first item from the free list
                         * failed.  Update stats and try again.
                         */
                        atomic_fetch_add(&(lfht_ptr->num_fl_head_update_cols), 1);
                        atomic_fetch_add(&(lfht_ptr->num_node_free_candidate_selection_restarts), 1);

                    } else {

                        /* first has been removed from the free list.  Set discard_fl_node_ptr to first,
                         * update stats, and exit the loop by setting fl_search_done to true.
                         */
                        discard_fl_node_ptr = first;
#endif /* LFHT__USE_SPTR */

                        atomic_fetch_sub(&(lfht_ptr->fl_len), 1);
                        atomic_fetch_add(&(lfht_ptr->num_nodes_freed), 1);
                        fl_search_done = true;
                    }
                } 
            } else {

                /* lfht_ptr->fl_head got changed out from under us -- this is expected
                 * from time to time, but collect stats to see how common it is.
                 */
                atomic_fetch_add(&(lfht_ptr->num_node_free_candidate_selection_restarts), 1);
            }
        } /* while ( ! fl_search_done ) */

        if ( discard_fl_node_ptr ) {

            assert(LFHT_FL_NODE_ON_FL == atomic_load(&(discard_fl_node_ptr->tag)));
            assert(0 == atomic_load(&(discard_fl_node_ptr->ref_count)));
#if LFHT__USE_SPTR
            snext.ptr = NULL;
            snext.sn  = 0ULL;
            atomic_store(&(discard_fl_node_ptr->snext), snext);
#else /* LFHT__USE_SPTR */
            atomic_store(&(discard_fl_node_ptr->next), NULL);
#endif /* LFHT__USE_SPTR */

            discard_node_ptr = (struct lfht_node_t *)discard_fl_node_ptr;

            assert(LFHT_VALID_NODE == discard_node_ptr->tag);

            discard_node_ptr->tag = LFHT_INVALID_NODE;
            atomic_store(&(discard_fl_node_ptr->tag), LFHT_FL_NODE_INVALID);

            free(discard_node_ptr);
        }
    } /* if ( atomic_load(&(lfht_ptr->fl_len)) > lfht_ptr->max_desired_fl_len ) */
#endif /* JRM */
    return;

} /* lfht_discard_node() */


/************************************************************************
 *
 * lfht_dump_list
 *
 *     Print the contents of the lfht_t to the supplied file.  For now
 *     this means displaying the contents of the LFSLL in the lock free
 *     hash table.
 *
 *
 *                                          JRM -- 6/14/23
 *
 ************************************************************************/

void lfht_dump_list(struct lfht_t * lfht_ptr, FILE * file_ptr)
{
    long long int node_num = 0;
    struct lfht_node_t * node_ptr;

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(file_ptr);

    fprintf(file_ptr, "\n\n***** CONTENTS OF LFSLL IN THE LFHT *****\n");

    fprintf(file_ptr, "\nLFSLL Logical / Physical Length = %lld/%lld, Free List Len = %lld.\n\n", 
            atomic_load(&(lfht_ptr->lfsll_log_len)), atomic_load(&(lfht_ptr->lfsll_phys_len)),
            atomic_load(&(lfht_ptr->fl_len)));

    node_ptr = atomic_load(&(lfht_ptr->lfsll_root));

    while ( node_ptr ) {

        fprintf(file_ptr, 
                "Node num = %lld, marked = %lld, sentinel = %d, id = 0x%lld, hash = 0x%llx, value = 0x%llx\n", 
                node_num++, (((unsigned long long)(atomic_load(&(node_ptr->next)))) & 0x01ULL),
                (int)(node_ptr->sentinel), node_ptr->id, node_ptr->hash, 
                (unsigned long long)atomic_load(&(node_ptr->value)));

        node_ptr = atomic_load(&(node_ptr->next));

        /* Clear the low order bit of node ptr whether it is set or not. */
        node_ptr = (struct lfht_node_t *)(((unsigned long long)(node_ptr)) & (~0x01ULL));
    }

    fprintf(file_ptr, "\n***** END LFHT CONTENTS *****\n\n");

    return;

} /* lfht_dump_list() */


/************************************************************************
 *
 * lfht_dump_stats
 *
 *     Print the contents of the statistics fields of the supplied
 *     intance of lfht_t to the supplied file.
 *
 *
 *                                          JRM -- 6/14/23
 *
 ************************************************************************/

void lfht_dump_stats(struct lfht_t * lfht_ptr, FILE * file_ptr)
{

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(file_ptr);

    fprintf(file_ptr, "\n\n***** LFSLL STATS *****\n");

    fprintf(file_ptr, "\nCurrent logical / physical LFSLL length = %lld / %lld \n", 
            atomic_load(&(lfht_ptr->lfsll_log_len)), atomic_load(&(lfht_ptr->lfsll_phys_len)));
    fprintf(file_ptr, "Max logical / physical LFSLL length = %lld / %lld\n", 
            atomic_load(&(lfht_ptr->max_lfsll_log_len)), atomic_load(&(lfht_ptr->max_lfsll_phys_len)));

    fprintf(file_ptr, "\nFree List:\n");
    fprintf(file_ptr, 
            "Max / current FL Length = %lld /%lld, Nodes added / deleted from free list = %lld / %lld\n",
            atomic_load(&(lfht_ptr->max_fl_len)),
            atomic_load(&(lfht_ptr->fl_len)),
            atomic_load(&(lfht_ptr->num_nodes_added_to_fl)), 
            atomic_load(&(lfht_ptr->num_nodes_drawn_from_fl)));
    fprintf(file_ptr, "FL head / tail / append cols = %lld / %lld / %lld.\n",
            atomic_load(&(lfht_ptr->num_fl_head_update_cols)),
            atomic_load(&(lfht_ptr->num_fl_tail_update_cols)),
            atomic_load(&(lfht_ptr->num_fl_append_cols)));
    fprintf(file_ptr, "FL reqs failed due to empty / ref count = %lld / %lld.\n",
            atomic_load(&(lfht_ptr->num_fl_req_denied_due_to_empty)),
            atomic_load(&(lfht_ptr->num_fl_req_denied_due_to_ref_count)));
    fprintf(file_ptr, "FL node ref count inc / decs = %lld / %lld, ref count inc retrys = %lld.\n",
            atomic_load(&(lfht_ptr->num_fl_node_ref_cnt_incs)),
            atomic_load(&(lfht_ptr->num_fl_node_ref_cnt_decs)),
            atomic_load(&(lfht_ptr->num_fl_node_ref_cnt_inc_retrys)));
    fprintf(file_ptr, 
            "Nodes allocated / freed = %lld / %lld, candidate selection for free retries = %lld\n",
            atomic_load(&(lfht_ptr->num_nodes_allocated)),
            atomic_load(&(lfht_ptr->num_nodes_freed)),
            atomic_load(&(lfht_ptr->num_node_free_candidate_selection_restarts)));
    fprintf(file_ptr, "Frees skiped due to empty / ref_count = %lld / %lld.\n",
            atomic_load(&(lfht_ptr->num_fl_frees_skiped_due_to_empty)),
            atomic_load(&(lfht_ptr->num_fl_frees_skiped_due_to_ref_count)));

    fprintf(file_ptr, "\nHash Buckets:\n");
    fprintf(file_ptr, 
            "Hash buckets defined / initialized = %lld / %lld, index_bits = %d, max index_bits = %d\n",
            atomic_load(&(lfht_ptr->buckets_defined)),
            atomic_load(&(lfht_ptr->buckets_initialized)),
            atomic_load(&(lfht_ptr->index_bits)),
            lfht_ptr->max_index_bits);
    fprintf(file_ptr, "Index bits incr cols = %lld, buckets defined update cols / retries = %lld / %lld.\n",
            atomic_load(&(lfht_ptr->index_bits_incr_cols)),
            atomic_load(&(lfht_ptr->buckets_defined_update_cols)),
            atomic_load(&(lfht_ptr->buckets_defined_update_retries)));
    fprintf(file_ptr, "Hash bucket init cols / col sleeps = %lld / %lld\n", 
            atomic_load(&(lfht_ptr->bucket_init_cols)),
            atomic_load(&(lfht_ptr->bucket_init_col_sleeps)));
    fprintf(file_ptr, "recursive bucket inits = %lld, sentinels traversed = %lld.\n",
            atomic_load(&(lfht_ptr->recursive_bucket_inits)),
            atomic_load(&(lfht_ptr->sentinels_traversed)));

    fprintf(file_ptr, "\nInsertions:\n");
    fprintf(file_ptr, "successful / failed = %lld/%lld, ins / del cols = %lld/%lld\n",
            atomic_load(&(lfht_ptr->insertions)), atomic_load(&(lfht_ptr->insertion_failures)),
            atomic_load(&(lfht_ptr->ins_restarts_due_to_ins_col)),
            atomic_load(&(lfht_ptr->ins_restarts_due_to_del_col)));
    fprintf(file_ptr, "del completions = %lld, nodes visited = %lld\n",
            atomic_load(&(lfht_ptr->ins_deletion_completions)), 
            atomic_load(&(lfht_ptr->nodes_visited_during_ins)));

    fprintf(file_ptr, "\nDeletions:\n");
    fprintf(file_ptr, "attempted / failed = %lld/%lld, starts / start cols = %lld/%lld, retries = %lld\n",
            atomic_load(&(lfht_ptr->deletion_attempts)), atomic_load(&(lfht_ptr->deletion_failures)),
            atomic_load(&(lfht_ptr->deletion_starts)), atomic_load(&(lfht_ptr->deletion_start_cols)),
            atomic_load(&(lfht_ptr->del_retries)));
    fprintf(file_ptr, "del completions = %lld, del col restarts = %lld, nodes visited = %lld\n",
            atomic_load(&(lfht_ptr->del_deletion_completions)), 
            atomic_load(&(lfht_ptr->del_restarts_due_to_del_col)), 
            atomic_load(&(lfht_ptr->nodes_visited_during_dels)));

    fprintf(file_ptr, "\nSearches:\n");
    fprintf(file_ptr, "attempted / successful / failed = %lld/%lld/%lld\n",
            atomic_load(&(lfht_ptr->searches)), atomic_load(&(lfht_ptr->successful_searches)),
            atomic_load(&(lfht_ptr->failed_searches)));
    fprintf(file_ptr, 
            "marked/unmoard nodes visited in: successful search %lld/%lld, failed search %lld/%lld\n",
            atomic_load(&(lfht_ptr->marked_nodes_visited_in_succ_searches)), 
            atomic_load(&(lfht_ptr->unmarked_nodes_visited_in_succ_searches)), 
            atomic_load(&(lfht_ptr->marked_nodes_visited_in_failed_searches)), 
            atomic_load(&(lfht_ptr->unmarked_nodes_visited_in_failed_searches)));

    if ( atomic_load(&(lfht_ptr->value_swaps)) > 0LL ) {

        fprintf(file_ptr, "\nValue Swaps:\n");
        fprintf(file_ptr, "attempted / successful / failed = %lld/%lld/%lld\n",
                atomic_load(&(lfht_ptr->value_swaps)), atomic_load(&(lfht_ptr->successful_val_swaps)),
                atomic_load(&(lfht_ptr->failed_val_swaps)));
        fprintf(file_ptr, 
            "marked/unmoard nodes visited in: successful value swaps %lld/%lld, failed value swaps %lld/%lld\n",
                atomic_load(&(lfht_ptr->marked_nodes_visited_in_succ_val_swaps)), 
                atomic_load(&(lfht_ptr->unmarked_nodes_visited_in_succ_val_swaps)), 
                atomic_load(&(lfht_ptr->marked_nodes_visited_in_failed_val_swaps)), 
                atomic_load(&(lfht_ptr->unmarked_nodes_visited_in_failed_val_swaps)));

    } else {

        fprintf(file_ptr, "\nNo Value Swaps.\n");
    }

    if ( atomic_load(&(lfht_ptr->value_searches)) > 0LL ) {

        fprintf(file_ptr, "\nSearches by Value:\n");
        fprintf(file_ptr, "attempted / successful / failed = %lld/%lld/%lld\n",
                atomic_load(&(lfht_ptr->value_searches)), atomic_load(&(lfht_ptr->successful_val_searches)),
                atomic_load(&(lfht_ptr->failed_val_searches)));
        fprintf(file_ptr, 
                "marked/unmoard nodes visited in value searches %lld/%lld, sentinels traversed %lld\n",
                atomic_load(&(lfht_ptr->marked_nodes_visited_in_val_searches)), 
                atomic_load(&(lfht_ptr->unmarked_nodes_visited_in_val_searches)), 
                atomic_load(&(lfht_ptr->sentinels_traversed_in_val_searches)));

    } else {

        fprintf(file_ptr, "\nNo Searches by Value.\n");
    }

    if ( atomic_load(&(lfht_ptr->itter_inits)) > 0LL ) {

        fprintf(file_ptr, "\nItterations:\n");
        fprintf(file_ptr, "initiated / nexts / completed = %lld/%lld/%lld\n",
                atomic_load(&(lfht_ptr->itter_inits)), atomic_load(&(lfht_ptr->itter_nexts)),
                atomic_load(&(lfht_ptr->itter_ends)));
        fprintf(file_ptr, 
                "marked/unmoard nodes visited in itterations %lld/%lld, sentinels traversed %lld\n",
                atomic_load(&(lfht_ptr->marked_nodes_visited_in_itters)), 
                atomic_load(&(lfht_ptr->unmarked_nodes_visited_in_itters)), 
                atomic_load(&(lfht_ptr->sentinels_traversed_in_itters)));

    } else {

        fprintf(file_ptr, "\nNo Itterations Initiated.\n");
    }

    fprintf(file_ptr, "\n***** END LFSLL STATS *****\n\n");

    return;

} /* lfht_dump_stats() */

#if 0 /* original version */
/************************************************************************
 *
 * lfht_enter()
 *
 * Function to be called on entry to any API call that touches the LFHT
 * data structures.
 *
 * At present, this function exists to increment the ref_count on the 
 * last node on the free list, and return a pointer to it.  This pointer
 * is then used by lfht_exit() to decrement the same ref_count.
 *
 * Geven the frequency with which this function will be called, it may
 * be useful to turn it into a macro.
 *
 *                                           JRM -- 6/30/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

struct lfht_fl_node_t * lfht_enter(struct lfht_t * lfht_ptr)
{
    bool done = false;
    struct lfht_fl_node_t * fl_tail;
    struct lfht_fl_node_t * fl_next;
#if LFHT__USE_SPTR
    struct lfht_flsptr_t fl_stail;
    struct lfht_flsptr_t test_fl_stail;
    struct lfht_flsptr_t fl_snext;
#endif /* LFHT__USE_SPTR */

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);

    while ( ! done ) {
#if LFHT__USE_SPTR
        fl_stail = atomic_load(&(lfht_ptr->fl_stail));

        assert(fl_stail.ptr);

        fl_next = atomic_load(&(fl_stail.ptr->next));

        test_fl_stail = atomic_load(&(lfht_ptr->fl_stail));

        if ( ( test_fl_stail.ptr == fl_stail.ptr ) && ( test_fl_stail.sn == fl_stail.sn ) ) {

            if ( NULL == fl_next ) {

                atomic_fetch_add(&(fl_stail.ptr->ref_count), 1);

                /* it is possible that lfht_ptr->fl_tail has changed in the 
                 * time since we last checked.  If so, it is remotely 
                 * possible that *fl_tail is no longer on the free list.
                 *
                 * If not, update stats and return fl_tail.
                 *
                 * If so, decrement fl_tail->ref_count, update stats, and 
                 * try again.
                 */
                test_fl_stail = atomic_load(&(lfht_ptr->fl_stail));

                if ( ( test_fl_stail.ptr == fl_stail.ptr ) && ( test_fl_stail.sn == fl_stail.sn ) ) {

                    assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_stail.ptr->tag)));

                    atomic_fetch_add(&(lfht_ptr->num_fl_node_ref_cnt_incs), 1);

                    done = true;

                } else {

                    atomic_fetch_sub(&(fl_stail.ptr->ref_count), 1);
                    atomic_fetch_add(&(lfht_ptr->num_fl_node_ref_cnt_inc_retrys), 1);
                }
            } else {

                /* lfht_ptr->fl_stail doesn't point to the end of the free list.  
                 * 
                 * Attempt to set lfht_ptr->fl_stail to point to fl_next to move towards 
                 * correcting this.
                 *
                 * This will fail if lfht_ptr->fl_stail != fl_stail -- which 
                 * is important, as if this is true, it is possible that fl_next
                 * is no longer on the free list.
                 *
                 * No immediate attempt to recover if we fail, but we do collect stats.
                 */
                fl_snext.ptr = fl_next;
                fl_snext.sn  = fl_stail.sn + 1;
                if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_stail), &fl_stail, fl_snext) ) {

                    atomic_fetch_add(&(lfht_ptr->num_fl_tail_update_cols), 1);
                }
            }
        }
    } /* while ( ! done ) */

    assert(fl_stail.ptr);
    assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_stail.ptr->tag)));
    assert(atomic_load(&(fl_stail.ptr->ref_count)) > 0);

    return(fl_stail.ptr);
#else /* LFHT__USE_SPTR */
        fl_tail = atomic_load(&(lfht_ptr->fl_tail));

        assert(fl_tail);

        fl_next = atomic_load(&(fl_tail->next));

        if ( fl_tail == atomic_load(&(lfht_ptr->fl_tail)) ) {
            if ( NULL == fl_next ) {

                atomic_fetch_add(&(fl_tail->ref_count), 1);

                /* it is possible that lfht_ptr->fl_tail has changed in the 
                 * time since we last checked.  If so, it is remotely 
                 * possible that *fl_tail is no longer on the free list.
                 *
                 * If not, update stats and return fl_tail.
                 *
                 * If so, decrement fl_tail->ref_count, update stats, and 
                 * try again.
                 */
                if ( fl_tail == atomic_load(&(lfht_ptr->fl_tail)) ) {

                    assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_tail->tag)));

                    atomic_fetch_add(&(lfht_ptr->num_fl_node_ref_cnt_incs), 1);

                    done = true;

                } else {

                    atomic_fetch_sub(&(fl_tail->ref_count), 1);
                    atomic_fetch_add(&(lfht_ptr->num_fl_node_ref_cnt_inc_retrys), 1);
                }
            } else {

                /* lfht_ptr->fl_tail doesn't point to the end of the free list.  
                 * 
                 * Attempt to set lfht_ptr->fl_tail to point to fl_next to move towards 
                 * correcting this.
                 *
                 * This will fail if lfht_ptr->fl_tail != fl_tail -- which 
                 * is important, as if this is true, it is possible that fl_next
                 * is no longer on the free list.
                 *
                 * No immediate attempt to recover if we fail, but we do collect stats.
                 */
                if ( ! atomic_compare_exchange_strong(&(lfht_ptr->fl_tail), &fl_tail, fl_next) ) {

                    atomic_fetch_add(&(lfht_ptr->num_fl_tail_update_cols), 1);
                }
            }
        }
    } /* while ( ! done ) */

    assert(fl_tail);
    assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_tail->tag)));
    assert(atomic_load(&(fl_tail->ref_count)) > 0);

    return(fl_tail);
#endif /* LFHT__USE_SPTR */

} /* lfht_enter() */

#else /* new version */

/************************************************************************
 *
 * lfht_enter()
 *
 * Function to be called on entry to any API call that touches the LFHT
 * data structures.
 *
 * At present, this function exists to insert an entry with refcount 1 
 * at the end of the free list, or (if such a node already exists) to 
 * increment its ref count.
 *
 * In either case, the pointer to the relevant node is returned to the
 * caller,  where it is then used by lfht_exit() to decrement the same 
 * ref_count.
 *
 * Geven the frequency with which this function will be called, it may
 * be useful to turn it into a macro.
 *
 *                                           JRM -- 6/30/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

struct lfht_fl_node_t * lfht_enter(struct lfht_t * lfht_ptr)
{
    bool done = false;
#if 0 
    unsigned int curr_ref_count;
#endif 
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr = NULL;
#if LFHT__USE_SPTR
#if 0
    struct lfht_flsptr_t fl_stail;
#endif
#else /* LFHT__USE_SPTR */
    struct lfht_fl_node_t * fl_tail;
#endif /* LFHT__USE_SPTR */

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
#if 0
    /* First, check to see if the node at the end of the 
     * the free list has a positive ref count.  If it does,
     * increment it with a atomic_compare_exchange_strong(),
     * and return a pointer to the node.
     */
#if LFHT__USE_SPTR
    fl_stail = atomic_load(&(lfht_ptr->fl_stail));

    assert(fl_stail.ptr);

    if ( 0 < (curr_ref_count = atomic_load(&(fl_stail.ptr->ref_count))) ) {

        if ( atomic_compare_exchange_strong(&(fl_stail.ptr->ref_count), &curr_ref_count, curr_ref_count + 1) ) {

            fl_node_ptr = fl_stail.ptr;
            assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_node_ptr->tag)));
            done = true;
        }
    }
#else /* LFHT__USE_SPTR */
    fl_tail = atomic_load(&(lfht_ptr->fl_tail));

    assert(fl_tail);

    if ( 0 < (curr_ref_count = atomic_load(&(fl_tail->ref_count))) ) {

        if ( atomic_compare_exchange_strong(&(fl_tailr->ref_count), &curr_ref_count, curr_ref_count + 1) ) {

            fl_node_ptr = fl_stail;
            assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_node_ptr->tag)));
            done = true;
        }
    }
#endif /* LFHT__USE_SPTR */
#endif
    if ( ! done ) {

        node_ptr = lfht_create_node(lfht_ptr, 0ULL, 1ULL, false, NULL);

        assert(node_ptr);
        assert(LFHT_VALID_NODE == node_ptr->tag);

        atomic_store(&(node_ptr->next), (struct lfht_node_t *)0x01ULL);

        fl_node_ptr = (struct lfht_fl_node_t *)node_ptr;

        assert(LFHT_FL_NODE_IN_USE == fl_node_ptr->tag);
        assert(0ULL == atomic_load(&(fl_node_ptr->ref_count)));

        atomic_store(&(fl_node_ptr->ref_count), 1ULL);

        lfht_discard_node(lfht_ptr, node_ptr, 1);

        done = true;
    }

    assert(fl_node_ptr);
    assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_node_ptr->tag)));
    assert(atomic_load(&(fl_node_ptr->ref_count)) > 0);

    return(fl_node_ptr);

} /* lfht_enter() */

#endif /* JRM */


/************************************************************************
 *
 * lfht_exit()
 *
 * Function to be called on exit from any API call that touches the LFHT
 * data structure.
 *
 * At present, this function exists to decrement the ref_count on the 
 * free list node whose ref count was incremented by the lfht_enter()
 * call.
 *
 *                                           JRM -- 6/24/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

void lfht_exit(struct lfht_t * lfht_ptr, struct lfht_fl_node_t * fl_node_ptr)
{
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(fl_node_ptr);
    assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_node_ptr->tag)));
    assert(atomic_load(&(fl_node_ptr->ref_count)) > 0);

    atomic_fetch_sub(&(fl_node_ptr->ref_count), 1);
    atomic_fetch_add(&(lfht_ptr->num_fl_node_ref_cnt_decs), 1LL);

    return;

} /* lfht_exit() */

#if 0 /* old version */
/************************************************************************
 *
 * lfht_find
 *
 * Search the supplied lfht looking for a node with the supplied id..
 *
 * If it is found, and the node is not marked for deletion, set *value_ptr
 * equal to the value field of the node and return true.  
 *
 * Otherwise, return false.
 *
 *                                           JRM -- 7/1/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

bool lfht_find(struct lfht_t * lfht_ptr,
               unsigned long long int id,
               void ** value_ptr)

{
    bool success = false;
    int marked_nodes_visited = 0;
    int unmarked_nodes_visited = 0;
    unsigned long long int hash;
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(0x0ULL <= id);
    assert(id <= LFHT__MAX_ID);
    assert(value_ptr);

    fl_node_ptr = lfht_enter(lfht_ptr);

    hash = lfht_id_to_hash(id, false);

    /* now attempt to find the target */

    node_ptr = lfht_get_hash_bucket_sentinel(lfht_ptr, hash);

    assert(LFHT_VALID_NODE == node_ptr->tag);
    assert(0x0ULL == (((unsigned long long)(node_ptr)) & 0x01ULL));
    assert(node_ptr->sentinel);
    assert(node_ptr->hash < hash);

    while ( node_ptr->hash < hash ) {

        node_ptr = atomic_load(&(node_ptr->next));

        /* test to see if node_ptr is marked.  If it, remove the
         * mark so we can use it.
         */
        if ( ((unsigned long long)(node_ptr)) & 0x01ULL ) {

            /* node is marked -- remove the mark and increment marked nodes visited */
            node_ptr = (struct lfht_node_t *)(((unsigned long long)(node_ptr)) & (~0x01ULL));

            marked_nodes_visited++;

        } else {

            unmarked_nodes_visited++;
        }

        assert( LFHT_VALID_NODE == node_ptr->tag );

        if ( ( node_ptr->sentinel ) && ( node_ptr->hash < hash ) ) {

            atomic_fetch_add(&(lfht_ptr->sentinels_traversed), 1);
        }
    }

    if ( ( node_ptr->hash != hash ) ||
         ( ((unsigned long long)atomic_load(&(node_ptr->next))) & 0x01ULL)) {

        success = false;

    } else {

        assert(! node_ptr->sentinel);
        assert(id == node_ptr->id);
        success = true;
        *value_ptr = atomic_load(&(node_ptr->value));
    }

    /* update statistics */

    assert(marked_nodes_visited >= 0);
    assert(unmarked_nodes_visited >= 0);
    
    atomic_fetch_add(&(lfht_ptr->searches), 1);

    if ( success ) {

        atomic_fetch_add(&(lfht_ptr->successful_searches), 1);
        atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_succ_searches), marked_nodes_visited);
        atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_succ_searches), unmarked_nodes_visited);

    } else {

        atomic_fetch_add(&(lfht_ptr->failed_searches), 1);
        atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_failed_searches), marked_nodes_visited);
        atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_failed_searches), unmarked_nodes_visited);
    }

    lfht_exit(lfht_ptr, fl_node_ptr);

    return(success);

} /* lfht_find() */

#else /* new version */

/************************************************************************
 *
 * lfht_find
 *
 * Search the supplied lfht looking for a node with the supplied id..
 *
 * If it is found, and the node is not marked for deletion, set *value_ptr
 * equal to the value field of the node and return true.  
 *
 * Otherwise, return false.
 *
 *                                           JRM -- 7/1/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

bool lfht_find(struct lfht_t * lfht_ptr,
               unsigned long long int id,
               void ** value_ptr)

{
    bool success = false;
    long long int marked_nodes_visited = 0;
    long long int unmarked_nodes_visited = 0;
    long long int sentinels_traversed = 0;
    unsigned long long int hash;
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
#ifdef H5I__MT
    assert((id & ID_MASK) <= LFHT__MAX_ID);
#else /* H5I__MT */
    assert(id <= LFHT__MAX_ID);
#endif /* H5I__MT */
    assert(value_ptr);

    fl_node_ptr = lfht_enter(lfht_ptr);

#ifdef H5I__MT
    hash = lfht_id_to_hash((id & ID_MASK), false);
#else /* H5I__MT */
    hash = lfht_id_to_hash(id, false);
#endif /* H5I__MT */

    /* now attempt to find the target */

    node_ptr = lfht_find_internal(lfht_ptr, hash, &marked_nodes_visited, 
                                  &unmarked_nodes_visited, &sentinels_traversed);

    if ( ( NULL == node_ptr ) || ( node_ptr->hash != hash ) ||
         ( ((unsigned long long)atomic_load(&(node_ptr->next))) & 0x01ULL)) {

        success = false;

    } else {

        assert(! node_ptr->sentinel);
        assert(hash == node_ptr->hash);
        success = true;
        *value_ptr = atomic_load(&(node_ptr->value));
    }

    /* update statistics */
    
    atomic_fetch_add(&(lfht_ptr->searches), 1);

    if ( success ) {

        atomic_fetch_add(&(lfht_ptr->successful_searches), 1);
        atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_succ_searches), marked_nodes_visited);
        atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_succ_searches), unmarked_nodes_visited);

    } else {

        atomic_fetch_add(&(lfht_ptr->failed_searches), 1);
        atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_failed_searches), marked_nodes_visited);
        atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_failed_searches), unmarked_nodes_visited);
    }

    if ( sentinels_traversed > 0 ) {

        atomic_fetch_add(&(lfht_ptr->sentinels_traversed), sentinels_traversed);
    }

    lfht_exit(lfht_ptr, fl_node_ptr);

    return(success);

} /* lfht_find() */

#endif /* new version */

/************************************************************************
 *
 * lfht_find_id_by_value
 *
 * Search the supplied lfht looking for a node with the supplied value.
 *
 * If it is found, and the node is not marked for deletion, set *id_ptr
 * equal to the associated id and return true.
 *
 * Otherwise, return false.
 *
 * Note that at present this function just does a simple scan of the 
 * LFSLL used by the LFHT to store its entries -- as such it is very 
 * in-efficient.  As I believe that this operation is rare, this 
 * should be acceptable.  However, if this changes, it may be 
 * necessary to re-visit this.
 *
 *                                           JRM -- 7/14/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

bool lfht_find_id_by_value(struct lfht_t * lfht_ptr,
                           unsigned long long int *id_ptr,
                           void * value)

{
    bool success = false;
    bool marked;
    unsigned long long int marked_nodes_visited = 0;
    unsigned long long int unmarked_nodes_visited = 0;
    unsigned long long int sentinels_traversed = 0;
    unsigned long long int id;
    struct lfht_node_t * next_ptr = NULL;
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(id_ptr);

    fl_node_ptr = lfht_enter(lfht_ptr);


    /* now attempt to find the target */

    node_ptr = atomic_load(&(lfht_ptr->lfsll_root));

    assert(LFHT_VALID_NODE == node_ptr->tag);
    assert(0x0ULL == (((unsigned long long)(node_ptr)) & 0x01ULL));
    assert(node_ptr->sentinel);

    while ( ( node_ptr ) && ( ! success ) ) {

        assert( LFHT_VALID_NODE == node_ptr->tag );

        next_ptr = atomic_load(&(node_ptr->next));

        /* test to see if next_ptr is marked.  If it, remove the
         * mark so we can use it.
         */
        if ( ((unsigned long long)(next_ptr)) & 0x01ULL ) {

            assert(!(node_ptr->sentinel));

            /* node is marked -- remove the mark and increment marked nodes visited */
            next_ptr = (struct lfht_node_t *)(((unsigned long long)(next_ptr)) & (~0x01ULL));

            marked = true;

            marked_nodes_visited++;

        } else {

            marked = false;

            if ( ! node_ptr->sentinel ) {

                unmarked_nodes_visited++;
            }
        }

        assert( LFHT_VALID_NODE == node_ptr->tag );

        if ( node_ptr->sentinel ) {

            sentinels_traversed++;

        } else if ( ( ! marked ) && ( atomic_load(&(node_ptr->value)) == value ) ) {

            id = node_ptr->id;
            success = true;
        }

        node_ptr = next_ptr;
    }

    if ( success ) {

        *id_ptr = id;
    }
    /* it is tempting to assert that lfht_ptr->lfsll_log_len == 0 if success is false.  
     *
     * However, there are two problems with this.  
     *
     * First, lfsll_log_len is updated only after the fact, and thus will be briefly 
     * incorrect after each insertion and deletion.  
     * 
     * Second, the search for the first element will fail if entries are inserted at 
     * the front of the lfsll after the scan for the first element has passed.  
     *
     * This is OK, as the result will be correct for some ordering of the insertions and 
     * the search for the first element.  If the user wishes to avoid this race, it is 
     * his responsibility to ensure that the hash table is quiecent during iterations.
     */

    /* update statistics */

    atomic_fetch_add(&(lfht_ptr->value_searches), 1);

    if ( success ) {

        atomic_fetch_add(&(lfht_ptr->successful_val_searches), 1);

    } else {

        atomic_fetch_add(&(lfht_ptr->failed_val_searches), 1);
    }

    atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_val_searches), marked_nodes_visited);
    atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_val_searches), unmarked_nodes_visited);
    atomic_fetch_add(&(lfht_ptr->sentinels_traversed), sentinels_traversed);
    atomic_fetch_add(&(lfht_ptr->sentinels_traversed_in_val_searches), sentinels_traversed);

    lfht_exit(lfht_ptr, fl_node_ptr);

    return(success);

} /* lfht_find_id_by_value() */


/************************************************************************
 *
 * lfht_find_internal
 *
 * Search the supplied lfht looking for a node with the supplied hash.
 *
 * If it is found, and the node is not marked for deletion, return a 
 * pointer to the node.
 *
 * Otherwise, return NULL.
 *
 *                                           JRM -- 7/13/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

struct lfht_node_t * lfht_find_internal(struct lfht_t * lfht_ptr, 
                                        unsigned long long int hash,
                                        long long int * marked_nodes_visited_ptr,
                                        long long int * unmarked_nodes_visited_ptr, 
                                        long long int * sentinels_traversed_ptr)

{
    long long int marked_nodes_visited = 0;
    long long int unmarked_nodes_visited = 0;
    long long int sentinels_traversed = 0;
    struct lfht_node_t * node_ptr = NULL;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(hash <= LFHT__MAX_HASH);
    assert(marked_nodes_visited_ptr);
    assert(unmarked_nodes_visited_ptr);
    assert(sentinels_traversed_ptr);

    /* attempt to find the target */

    node_ptr = lfht_get_hash_bucket_sentinel(lfht_ptr, hash);

    assert(LFHT_VALID_NODE == node_ptr->tag);
    assert(0x0ULL == (((unsigned long long)(node_ptr)) & 0x01ULL));
    assert(node_ptr->sentinel);
    assert(node_ptr->hash < hash);

    while ( node_ptr->hash < hash ) {

        node_ptr = atomic_load(&(node_ptr->next));

        /* test to see if node_ptr is marked.  If it, remove the
         * mark so we can use it.
         */
        if ( ((unsigned long long)(node_ptr)) & 0x01ULL ) {

            /* node is marked -- remove the mark and increment marked nodes visited */
            node_ptr = (struct lfht_node_t *)(((unsigned long long)(node_ptr)) & (~0x01ULL));

            marked_nodes_visited++;

        } else {

            unmarked_nodes_visited++;
        }

        assert( LFHT_VALID_NODE == node_ptr->tag );

        if ( ( node_ptr->sentinel ) && ( node_ptr->hash < hash ) ) {

            sentinels_traversed++;
        }
    }

    if ( ( node_ptr->hash != hash ) ||
         ( ((unsigned long long)atomic_load(&(node_ptr->next))) & 0x01ULL)) {

        node_ptr = NULL;

    } else {

        assert(! node_ptr->sentinel);
        assert(hash == node_ptr->hash);
    }

    *marked_nodes_visited_ptr   = marked_nodes_visited;
    *unmarked_nodes_visited_ptr = unmarked_nodes_visited;
    *sentinels_traversed_ptr    = sentinels_traversed;

    return(node_ptr);

} /* lfht_find_internal() */


/************************************************************************
 *
 * lfht_find_mod_point
 *
 * Starting at the sentinel node pointed to by bucket_head, scan the 
 * LFSLL in the hash table to find a pair of adjacent nodes such that 
 * the hash of the first node has value less than or equal to the 
 * supplied hash, and the hash of the second node has value greater 
 * than the supplied hash.
 *
 * Observe that since the list is sorted in increasing hash order, the hash
 * of the first node is the largest hash in the list that is less than 
 * or equal to the supplied hash.  Similarly, the hash of the second 
 * node is the smallest hash in the SLL that is greater than the 
 * supplied hash.
 *
 * On success, return pointers to the first and second nodes in 
 * *first_ptr_ptr and *second_ptr_ptr respectively.
 *
 * During the scan of the of the lfht, attempt to complete the deletion
 * of any node encountered that is marked for deletion.  If this 
 * attempt fails (due to a collision with another thread beating us
 * to the physical deletion), restart the scan from the beginning 
 * of the hash bucket.  Maintain a count of the number of collisions, and 
 * return this value in *cols_ptr.
 *
 * Recall that the sentry nodes at the beginning of each hash bucket
 * and end of the list can't be removed, which simplifies this restart.
 *
 * Similarly, maintain a count of the number of deletions completed,
 * and return this value in *dels_ptr.
 *
 *                                           JRM -- 6/30/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

void lfht_find_mod_point(struct lfht_t * lfht_ptr,
                         struct lfht_node_t * bucket_head_ptr,
                         struct lfht_node_t ** first_ptr_ptr,
                         struct lfht_node_t ** second_ptr_ptr,
                         int * cols_ptr,
                         int * dels_ptr,
                         int * nodes_visited_ptr,
                         unsigned long long int hash)
{
    bool done = false;
    bool retry = false;
    int cols = 0;
    int dels = 0;
    int nodes_visited = 0;
    struct lfht_node_t * first_ptr = NULL;
    struct lfht_node_t * second_ptr = NULL;
    struct lfht_node_t * third_ptr = NULL;

    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(bucket_head_ptr);
    assert(first_ptr_ptr);
    assert(NULL == *first_ptr_ptr);
    assert(second_ptr_ptr);
    assert(NULL == *second_ptr_ptr);
    assert(cols_ptr);
    assert(dels_ptr);
    assert(nodes_visited_ptr);
    assert(hash <= LFHT__MAX_HASH);

    /* first, find the sentinel node marking the beginning of the 
     * hash bucket that hash maps into.  Note that this sentinel 
     * node may not exist -- if so, lfht_get_hash_bucket_sentinel()
     * will create and insert it.
     */

    do { 
        assert(!done);

        retry = false;

        first_ptr = bucket_head_ptr;

        assert(LFHT_VALID_NODE == first_ptr->tag);
        assert(0x0ULL == (((unsigned long long)(first_ptr)) & 0x01ULL));
        assert(first_ptr->sentinel);
        assert(first_ptr->hash < hash);

        second_ptr = atomic_load(&(first_ptr->next));

        assert(second_ptr);
        assert(0x0ULL == (((unsigned long long)(second_ptr)) & 0x01ULL));
        assert(LFHT_VALID_NODE == second_ptr->tag);
   
        do {
            third_ptr = atomic_load(&(second_ptr->next));

            /* if the low order bit on third_ptr is set, *second_ptr has 
             * been marked for deletion.  Attempt to unlink and discard 
             * *second_ptr if so, and repeat until *second_ptr no longer 
             * marked for deletion.  If any deletion completion fails, we 
             * must re-start the search for the mod point
             */
            while ( ((unsigned long long)(third_ptr)) & 0x01ULL ) 
            {
                assert(first_ptr);
                assert(LFHT_VALID_NODE == first_ptr->tag);

                assert(second_ptr);
                assert(LFHT_VALID_NODE == second_ptr->tag);
                assert(!(second_ptr->sentinel));

                /* third_ptr has its low order bit set to indicate that 
                 * *second_ptr is marked for deletion,  Before we use 
                 * third_ptr, we must reset the low order bit.
                 */
                third_ptr = (struct lfht_node_t *)(((unsigned long long)(third_ptr)) & ~0x01ULL);

                assert(third_ptr);
                assert(LFHT_VALID_NODE == third_ptr->tag);

                if ( ! atomic_compare_exchange_strong(&(first_ptr->next), &second_ptr, third_ptr) ) {

                    /* compare and exchange failed -- some other thread
                     * beat us to the unlink.  Increment cols, set retry
                     * to TRUE and then restart the search at the head 
                     * of the SLL.
                     */
                    cols++;
                    retry = true;
                    break;

                } else {

                    /* unlink of *second_ptr succeeded.  Decrement the logical list length,
                     * increment dels, increment nodes_visited, discard *second_ptr, set 
                     * second_ptr to third_ptr, and then load third_ptr
                     */
                    atomic_fetch_sub(&(lfht_ptr->lfsll_phys_len), 1);
                    dels++;
                    nodes_visited++;
                    lfht_discard_node(lfht_ptr, second_ptr, 0);
                    second_ptr = third_ptr;
                    third_ptr = atomic_load(&(second_ptr->next));

                    assert(first_ptr);
                    assert(LFHT_VALID_NODE == first_ptr->tag);

                    assert(second_ptr);
                    assert(LFHT_VALID_NODE == second_ptr->tag);

                }
            } /* end while *second_ptr is marked for deletion */

            if ( ! retry ) {

                assert(first_ptr);
                assert(LFHT_VALID_NODE == first_ptr->tag);

                assert(second_ptr);
                assert(LFHT_VALID_NODE == second_ptr->tag);

                assert(first_ptr->hash <= hash);
 
                if ( second_ptr->hash > hash ) {

                    done = true;

                } else {

                    if ( second_ptr->sentinel ) {

                        atomic_fetch_add(&(lfht_ptr->sentinels_traversed), 1);
                    }

                    first_ptr = second_ptr;
                    second_ptr = third_ptr;
                    nodes_visited++;
                }
            }

        } while ( ( ! done ) && ( ! retry ) );

        assert( ! ( done && retry ) );

    } while ( retry );

    assert(done);
    assert(!retry);

    assert(first_ptr->hash <= hash);
    assert(hash < second_ptr->hash);

    *first_ptr_ptr = first_ptr;
    *second_ptr_ptr = second_ptr;
    *cols_ptr += cols;
    *dels_ptr += dels;
    *nodes_visited_ptr += nodes_visited;

    return;

} /* lfht_find_mod_point() */


/***********************************************************************************
 *
 * lfht_get_first()
 *
 * One of two API calls to support itteration through all entries in the 
 * lock free hash table.  Note that the itteration will almost certainly not be in 
 * id order, and that entries added during the itteration may or may not be included
 * in the itteration.
 *
 * If the supplied instance of a lock free hash table is empty, return false.
 *
 * If it contains at least one entry, return the id, and value of the first 
 * entry in *id_ptr, and *value_ptr respectively, and then return true.
 *
 *                                                  JRM -- 7/16/23
 *
 * Changes:
 *
 *  - None.
 *
 ***********************************************************************************/

bool lfht_get_first(struct lfht_t * lfht_ptr, unsigned long long int * id_ptr, void ** value_ptr)
{
    bool success = false;
    bool marked;
    unsigned long long int marked_nodes_visited = 0;
    unsigned long long int unmarked_nodes_visited = 0;
    unsigned long long int sentinels_traversed = 0;
    unsigned long long int id;
    void * value;
    struct lfht_node_t * next_ptr = NULL;
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(id_ptr);
    assert(value_ptr);

    fl_node_ptr = lfht_enter(lfht_ptr);


    /* search for the first entry in the hash table, if it exists */

    node_ptr = atomic_load(&(lfht_ptr->lfsll_root));

    assert(LFHT_VALID_NODE == node_ptr->tag);
    assert(0x0ULL == (((unsigned long long)(node_ptr)) & 0x01ULL));
    assert(node_ptr->sentinel);

    while ( ( node_ptr ) && ( ! success ) ) {

        assert( LFHT_VALID_NODE == node_ptr->tag );

        next_ptr = atomic_load(&(node_ptr->next));

        /* test to see if next_ptr is marked.  If it, remove the
         * mark so we can use it.
         */
        if ( ((unsigned long long)(next_ptr)) & 0x01ULL ) {

            assert(!(node_ptr->sentinel));

            /* node is marked -- remove the mark and increment marked nodes visited */
            next_ptr = (struct lfht_node_t *)(((unsigned long long)(next_ptr)) & (~0x01ULL));

            marked = true;

            marked_nodes_visited++;

        } else {

            marked = false;

            if ( ! node_ptr->sentinel ) {

                unmarked_nodes_visited++;
            }
        }

        assert( LFHT_VALID_NODE == node_ptr->tag );

        if ( node_ptr->sentinel ) {

            sentinels_traversed++;

        } else if ( ! marked ) {

            id = node_ptr->id;
            value = atomic_load(&(node_ptr->value));
            success = true;
        }

        node_ptr = next_ptr;
    }

    if ( success ) {

        *id_ptr    = id;
        *value_ptr = value;
    }

    /* update statistics */
    
    atomic_fetch_add(&(lfht_ptr->itter_inits), 1);

    if ( ! success ) {

        atomic_fetch_add(&(lfht_ptr->itter_ends), 1);
    }

    atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_itters), marked_nodes_visited);
    atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_itters), unmarked_nodes_visited);
    atomic_fetch_add(&(lfht_ptr->sentinels_traversed), sentinels_traversed);
    atomic_fetch_add(&(lfht_ptr->sentinels_traversed_in_itters), sentinels_traversed);

    lfht_exit(lfht_ptr, fl_node_ptr);

    return(success);

} /* lfht_get_first() */


/***********************************************************************************
 *
 * lfht_get_hash_bucket_sentinel()
 *
 *    Given a hash, find the sentinel node in the LFSLL marking the bucket into 
 *    which the hash will fall, and return a pointer to it.
 *
 *    Usually, this is a trivial look up.  However, it is possible that the 
 *    containing hash bucket hasn't been initialized yet.  In this case, it 
 *    will be necessary to create the bucket before returning the pointer 
 *    to the required sentinel.  Further, observe that this operation may 
 *    be recursing if a sequence of containing buckets haven't been initialized.
 *
 *                                                JRM -- 6/30/23
 *
 * Changes:
 *
 *  - None.
 *
 ***********************************************************************************/

struct lfht_node_t * lfht_get_hash_bucket_sentinel(struct lfht_t * lfht_ptr, unsigned long long int hash)
{
    int index_bits;
    struct lfht_node_t * sentinel_ptr = NULL;
    unsigned long long int hash_index;

    index_bits = atomic_load(&(lfht_ptr->index_bits));

    hash_index = lfht_hash_to_idx(hash, index_bits);

    if ( NULL == atomic_load(&(lfht_ptr->bucket_idx[hash_index])) ) {

        /* bucket doesn't exist -- create it */
        lfht_create_hash_bucket(lfht_ptr, hash, atomic_load(&(lfht_ptr->index_bits)));
    }

    sentinel_ptr = atomic_load(&(lfht_ptr->bucket_idx[hash_index]));
    assert(sentinel_ptr);
    assert(0x0ULL == (((unsigned long long)(sentinel_ptr)) & 0x01ULL));
    assert(LFHT_VALID_NODE == sentinel_ptr->tag);
    assert(sentinel_ptr->sentinel);
#if 1 /* JRM */
    if ( sentinel_ptr->hash > hash ) {

        fprintf(stderr, "\nhash_index = %lld, sentinel_ptr->hash = 0x%llx, hash = 0x%llx.\n",
                hash_index, sentinel_ptr->hash, hash);
    }
#endif /* JRM */
    assert(sentinel_ptr->hash < hash);

    return(sentinel_ptr);

} /* lfht_get_hash_bucket_sentinel() */


/***********************************************************************************
 *
 * lfht_get_next()
 *
 * One of two API calls to support itteration through all entries in the 
 * lock free hash table.  Note that the itteration will almost certainly not be in 
 * id order, and that entries added during the itteration may or may not be included
 * in the itteration.
 *
 * Compute the hash of the supplied old_id, and attempt to find the entry in the 
 * hash table with the smallest hash that is greater than that of the old_id.  If 
 * it exists, this is the next entry in the itteration.
 *
 * If no next entry exists, return false.
 *
 * Otherwise, return the id and value of the next entry in *id_ptr and *value_ptr,
 * and return true.
 *
 *                                                  JRM -- 7/16/23
 *
 * Changes:
 *
 *  - None.
 *
 ***********************************************************************************/

bool lfht_get_next(struct lfht_t * lfht_ptr, unsigned long long int old_id, 
                   unsigned long long int * id_ptr, void ** value_ptr)
{
    bool success = false;
    bool marked;
    unsigned long long int marked_nodes_visited = 0;
    unsigned long long int unmarked_nodes_visited = 0;
    unsigned long long int sentinels_traversed = 0;
    unsigned long long int id;
    unsigned long long int old_hash;
    void * value;
    struct lfht_node_t * next_ptr = NULL;
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(id_ptr);
    assert(value_ptr);

    fl_node_ptr = lfht_enter(lfht_ptr);

    /* compute the hash of old_id.  The node with this hash should still be 
     * in the hash table, but we have no way of enforcing this.  Thus, make
     * no assumptions.
     */
    old_hash = lfht_id_to_hash(old_id, false);

    /* now search for the node in the hash table with the smallest hash 
     * that is greater than old_hash.  This algorithm is very similar to 
     * that used in lfht_find_internal().
     */
    node_ptr = lfht_get_hash_bucket_sentinel(lfht_ptr, old_hash);

    assert(LFHT_VALID_NODE == node_ptr->tag);
    assert(0x0ULL == (((unsigned long long)(node_ptr)) & 0x01ULL));
    assert(node_ptr->sentinel);
    assert(node_ptr->hash < old_hash);

    while ( ( node_ptr ) && ( ! success ) ) {

        assert( LFHT_VALID_NODE == node_ptr->tag );

        next_ptr = atomic_load(&(node_ptr->next));

        /* test to see if next_ptr is marked.  If it, remove the
         * mark so we can use it.
         */
        if ( ((unsigned long long)(next_ptr)) & 0x01ULL ) {

            assert(!(node_ptr->sentinel));

            /* node is marked -- remove the mark from next_ptr, and increment marked nodes visited */
            next_ptr = (struct lfht_node_t *)(((unsigned long long)(next_ptr)) & (~0x01ULL));

            marked = true;

            marked_nodes_visited++;

        } else {

            marked = false;

            if ( ! node_ptr->sentinel ) {

                unmarked_nodes_visited++;
            }
        }

        assert( LFHT_VALID_NODE == node_ptr->tag );

        if ( node_ptr->sentinel ) {

            sentinels_traversed++;

        } else if ( ( ! marked ) && ( node_ptr->hash > old_hash ) ) {

            id = node_ptr->id;
            value = atomic_load(&(node_ptr->value));
            success = true;
        }

        node_ptr = next_ptr;
    }

    if ( success ) {

        *id_ptr    = id;
        *value_ptr = value;
    }

    /* update statistics */

    if ( success ) {

        atomic_fetch_add(&(lfht_ptr->itter_nexts), 1);

    } else {

        atomic_fetch_add(&(lfht_ptr->itter_ends), 1);
    }

    atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_itters), marked_nodes_visited);
    atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_itters), unmarked_nodes_visited);
    atomic_fetch_add(&(lfht_ptr->sentinels_traversed), sentinels_traversed);
    atomic_fetch_add(&(lfht_ptr->sentinels_traversed_in_itters), sentinels_traversed);

    lfht_exit(lfht_ptr, fl_node_ptr);

    return(success);

} /* lfht_get_next() */


/***********************************************************************************
 *
 * lfht_hash_to_bucket_idx()
 *
 *    Given a hash, compute the index of the containing bucket given the current 
 *    value of index_bits.
 *
 *    Do this by first left shifting the supplied hash by one bit.
 *
 *    The copy the lfht_ptr->index_bits most significant bits of the hash into 
 *    the least significant bits of the index in reverse order, and return the 
 *    result.
 *
 *
 *                                                JRM -- 6/29/23
 *
 * Changes:
 *
 *  - None.
 *
 ***********************************************************************************/

unsigned long long lfht_hash_to_idx(unsigned long long hash, int index_bits)
{
    int i;
    unsigned long long index = 0x0ULL;
    unsigned long long hash_bit;
    unsigned long long idx_bit;

    assert(0 <= index_bits);
    assert(LFHT__MAX_INDEX_BITS >= index_bits); 

    hash >>= 1;

    hash_bit = 0x01ULL << (LFHT__NUM_HASH_BITS - 1);
    idx_bit = 0x01ULL;

    for ( i = 0; i < index_bits; i++ ) {

        if ( 0 != (hash_bit & hash) ) {

            index |= idx_bit;
        }
        hash_bit >>= 1;
        idx_bit <<= 1; 
    }

    return ( index );

} /* lfht_hash_to_idx() */

/***********************************************************************************
 *
 * lfht_id_to_hash()
 *
 *    Given an id, compute the reverse order hash and return it.
 *
 *    Do this by examining the LFHT__NUM_HASH_BITS bit in the id, and if it is 
 *    set, set the first bit in the hash.  Then examine the LFHT__NUM_HASH_BITS - 1th
 *    bit in the id, and if it is set, set the second bit in the hash.  Repeat until
 *    the lower LFHT__NUM_HASH_BITS bits of the id have been examined, and the 
 *    corresponding bits in the hash have been set where appropriate.  Observe that 
 *    hash now contains the lower LFHT__NUM_HASH_BITS bits from the id in reverse
 *    order.
 *
 *    We must now modify hash so that if it sentinel hash, no id will hash to it,
 *    and it will always be the smallest value in its bucket.  Do this by left
 *    shifting hash by 1, and then bit-or-ing it with 0x01 if the sentinal_hash
 *    parameter is false.
 *
 *    Finally, return hash to the caller.
 *
 *                                                JRM -- 6/29/23
 *
 * Changes:
 *
 *  - None.
 *
 ***********************************************************************************/

unsigned long long lfht_id_to_hash(unsigned long long id, bool sentinel_hash)
{
    int i;
    unsigned long long id_bit;
    unsigned long long hash_bit;
    unsigned long long hash = 0;

    id_bit = 0x01ULL << (LFHT__NUM_HASH_BITS - 1);
    hash_bit = 0x01ULL;

    for ( i = 0; i < LFHT__NUM_HASH_BITS; i++ ) {

        if ( 0 != (id_bit & id) ) {

            hash |= hash_bit;
        }
        id_bit >>= 1;
        hash_bit <<= 1; 
    }

    hash <<= 1;

    if ( ! sentinel_hash ) {

        hash |= 0x01ULL;
    }

    return ( hash );

} /* lfht_id_to_hash() */


/************************************************************************
 *
 * lfht_init
 *
 *     Initialize an instance of lfht_t.
 *
 *                           JRM -- 6/30/23
 *
 ************************************************************************/

void lfht_init(struct lfht_t * lfht_ptr)
{
    int i;
    unsigned long long int mask = 0x0ULL;
#if LFHT__USE_SPTR
    struct lfht_flsptr_t init_lfht_flsptr = {NULL, 0x0ULL};
    struct lfht_flsptr_t fl_shead;
    struct lfht_flsptr_t fl_stail;
    struct lfht_flsptr_t snext;
#endif /* LFHT__USE_SPTR */

#ifdef H5I__MT
    assert(LFHT__NUM_HASH_BITS == ID_BITS + 1);
#endif /* H5I__MT */


    struct lfht_node_t * head_sentinel_ptr = NULL;
    struct lfht_node_t * tail_sentinel_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr = NULL;

    assert(lfht_ptr);

    lfht_ptr->tag = LFHT_VALID;


    /* lock free singly linked list */
    atomic_init(&(lfht_ptr->lfsll_root), NULL);
    atomic_init(&(lfht_ptr->lfsll_log_len), 0ULL);
    atomic_init(&(lfht_ptr->lfsll_phys_len), 2ULL);


    /* free list */
#if LFHT__USE_SPTR
    atomic_init(&(lfht_ptr->fl_shead), init_lfht_flsptr);
    atomic_init(&(lfht_ptr->fl_stail), init_lfht_flsptr);
#else /* LFHT__USE_SPTR */
    atomic_init(&(lfht_ptr->fl_head), NULL);
    atomic_init(&(lfht_ptr->fl_tail), NULL);
#endif /* LFHT__USE_SPTR */
    atomic_init(&(lfht_ptr->fl_len), 1LL);
    lfht_ptr->max_desired_fl_len = LFHT__MAX_DESIRED_FL_LEN;
    atomic_init(&(lfht_ptr->next_sn), 0ULL);


    /* hash bucket index */
    atomic_init(&(lfht_ptr->index_bits), 0);
    lfht_ptr->max_index_bits = LFHT__MAX_INDEX_BITS;
    for ( i = 0; i <= LFHT__NUM_HASH_BITS; i++ ) {

        lfht_ptr->index_masks[i] = mask;
        mask <<= 1;
        mask |= 0x01ULL;
    }
    atomic_init(&(lfht_ptr->buckets_defined), 1);
    atomic_init(&(lfht_ptr->buckets_initialized), 0);
    for ( i = 0; i < LFHT__BASE_IDX_LEN; i++ ) {

        atomic_init(&((lfht_ptr->bucket_idx)[i]), NULL);
    }


    /* statistics */
    atomic_init(&(lfht_ptr->max_lfsll_log_len), 0LL);
    atomic_init(&(lfht_ptr->max_lfsll_phys_len), 0LL);

    atomic_init(&(lfht_ptr->max_fl_len), 1LL);
    atomic_init(&(lfht_ptr->num_nodes_allocated), 0LL);
    atomic_init(&(lfht_ptr->num_nodes_freed), 0LL);
    atomic_init(&(lfht_ptr->num_node_free_candidate_selection_restarts), 0LL);
    atomic_init(&(lfht_ptr->num_nodes_added_to_fl), 0LL);
    atomic_init(&(lfht_ptr->num_nodes_drawn_from_fl), 0LL);
    atomic_init(&(lfht_ptr->num_fl_head_update_cols), 0LL);
    atomic_init(&(lfht_ptr->num_fl_tail_update_cols), 0LL);
    atomic_init(&(lfht_ptr->num_fl_append_cols), 0LL);
    atomic_init(&(lfht_ptr->num_fl_req_denied_due_to_empty), 0LL);
    atomic_init(&(lfht_ptr->num_fl_req_denied_due_to_ref_count), 0LL);
    atomic_init(&(lfht_ptr->num_fl_node_ref_cnt_incs), 0LL);
    atomic_init(&(lfht_ptr->num_fl_node_ref_cnt_inc_retrys), 0LL);
    atomic_init(&(lfht_ptr->num_fl_node_ref_cnt_decs), 0LL);
    atomic_init(&(lfht_ptr->num_fl_frees_skiped_due_to_empty), 0LL);
    atomic_init(&(lfht_ptr->num_fl_frees_skiped_due_to_ref_count), 0LL);

    atomic_init(&(lfht_ptr->index_bits_incr_cols), 0LL);
    atomic_init(&(lfht_ptr->buckets_defined_update_cols), 0LL);
    atomic_init(&(lfht_ptr->buckets_defined_update_retries), 0LL);
    atomic_init(&(lfht_ptr->bucket_init_cols), 0LL);
    atomic_init(&(lfht_ptr->bucket_init_col_sleeps), 0LL);
    atomic_init(&(lfht_ptr->recursive_bucket_inits), 0LL);
    atomic_init(&(lfht_ptr->sentinels_traversed), 0LL);

    atomic_init(&(lfht_ptr->insertions), 0LL);
    atomic_init(&(lfht_ptr->insertion_failures), 0LL);
    atomic_init(&(lfht_ptr->ins_restarts_due_to_ins_col), 0LL);
    atomic_init(&(lfht_ptr->ins_restarts_due_to_del_col), 0LL);
    atomic_init(&(lfht_ptr->ins_deletion_completions), 0LL);
    atomic_init(&(lfht_ptr->nodes_visited_during_ins), 0LL);

    atomic_init(&(lfht_ptr->deletion_attempts), 0LL);
    atomic_init(&(lfht_ptr->deletion_starts), 0LL);
    atomic_init(&(lfht_ptr->deletion_start_cols), 0LL);
    atomic_init(&(lfht_ptr->deletion_failures), 0LL);
    atomic_init(&(lfht_ptr->del_restarts_due_to_del_col), 0LL);
    atomic_init(&(lfht_ptr->del_retries), 0LL);
    atomic_init(&(lfht_ptr->del_deletion_completions), 0LL);
    atomic_init(&(lfht_ptr->nodes_visited_during_dels), 0LL);

    atomic_init(&(lfht_ptr->searches), 0LL);
    atomic_init(&(lfht_ptr->successful_searches), 0LL);
    atomic_init(&(lfht_ptr->failed_searches), 0LL);
    atomic_init(&(lfht_ptr->marked_nodes_visited_in_succ_searches), 0LL);
    atomic_init(&(lfht_ptr->unmarked_nodes_visited_in_succ_searches), 0LL);
    atomic_init(&(lfht_ptr->marked_nodes_visited_in_failed_searches), 0LL);
    atomic_init(&(lfht_ptr->unmarked_nodes_visited_in_failed_searches), 0LL);

    atomic_init(&(lfht_ptr->value_swaps), 0LL);
    atomic_init(&(lfht_ptr->successful_val_swaps), 0LL);
    atomic_init(&(lfht_ptr->failed_val_swaps), 0LL);
    atomic_init(&(lfht_ptr->marked_nodes_visited_in_succ_val_swaps), 0LL);
    atomic_init(&(lfht_ptr->unmarked_nodes_visited_in_succ_val_swaps), 0LL);
    atomic_init(&(lfht_ptr->marked_nodes_visited_in_failed_val_swaps), 0LL);
    atomic_init(&(lfht_ptr->unmarked_nodes_visited_in_failed_val_swaps), 0LL);

    atomic_init(&(lfht_ptr->value_searches), 0LL);
    atomic_init(&(lfht_ptr->successful_val_searches), 0LL);
    atomic_init(&(lfht_ptr->failed_val_searches), 0LL);
    atomic_init(&(lfht_ptr->marked_nodes_visited_in_val_searches), 0LL);
    atomic_init(&(lfht_ptr->unmarked_nodes_visited_in_val_searches), 0LL);
    atomic_init(&(lfht_ptr->sentinels_traversed_in_val_searches), 0LL);

    atomic_init(&(lfht_ptr->itter_inits), 0LL);
    atomic_init(&(lfht_ptr->itter_nexts), 0LL);
    atomic_init(&(lfht_ptr->itter_ends), 0LL);
    atomic_init(&(lfht_ptr->marked_nodes_visited_in_itters), 0LL);
    atomic_init(&(lfht_ptr->unmarked_nodes_visited_in_itters), 0LL);
    atomic_init(&(lfht_ptr->sentinels_traversed_in_itters), 0LL);


    /* setup hash table */

    head_sentinel_ptr = lfht_create_node(lfht_ptr, 0ULL, 0ULL, true, NULL);
    assert(head_sentinel_ptr);
    assert(head_sentinel_ptr->tag == LFHT_VALID_NODE);

    tail_sentinel_ptr = lfht_create_node(lfht_ptr, 0ULL, 0ULL, true, NULL);
    tail_sentinel_ptr->hash = LLONG_MAX;
    assert(tail_sentinel_ptr);
    assert(tail_sentinel_ptr->tag == LFHT_VALID_NODE);

    assert(NULL == atomic_load(&(tail_sentinel_ptr->next)));
    atomic_store(&(head_sentinel_ptr->next), tail_sentinel_ptr);
    atomic_store(&(lfht_ptr->lfsll_root), head_sentinel_ptr);

    assert(NULL == atomic_load(&(tail_sentinel_ptr->next)));
    atomic_store(&(head_sentinel_ptr->next), tail_sentinel_ptr);
    atomic_store(&(lfht_ptr->lfsll_root), head_sentinel_ptr);

    /* insert the zero-th bucket sentinel manually. */
    atomic_store(&((lfht_ptr->bucket_idx)[0]), head_sentinel_ptr);
    atomic_fetch_add(&(lfht_ptr->buckets_initialized), 1);


    /* Setup the free list.
     *
     * The free list must always have at least one node.  Allocate, 
     * initialize, and insert a node in the free list. 
     */
    fl_node_ptr = (struct lfht_fl_node_t *)lfht_create_node(lfht_ptr, 0ULL, 0ULL, false, NULL);
    assert(fl_node_ptr);
    assert(LFHT_FL_NODE_IN_USE == fl_node_ptr->tag);
    atomic_store(&(fl_node_ptr->tag), LFHT_FL_NODE_ON_FL);
#if LFHT__USE_SPTR
    snext = atomic_load(&(fl_node_ptr->snext));
    assert(NULL == snext.ptr);
    assert(0 == atomic_load(&(fl_node_ptr->ref_count)));
    fl_shead.ptr = fl_node_ptr;
    fl_shead.sn  = 1ULL;
    atomic_store(&(lfht_ptr->fl_shead), fl_shead);
    fl_stail.ptr = fl_node_ptr;
    fl_stail.sn  = 1ULL;
    atomic_store(&(lfht_ptr->fl_stail), fl_stail);
#else /* LFHT__USE_SPTR */
    assert(NULL == atomic_load(&(fl_node_ptr->next)));
    assert(0 == atomic_load(&(fl_node_ptr->ref_count)));
    atomic_store(&(lfht_ptr->fl_head), fl_node_ptr);
    atomic_store(&(lfht_ptr->fl_tail), fl_node_ptr);
#endif /* LFHT__USE_SPTR */

    return;

} /* lfht_init() */


/************************************************************************
 *
 * lfht_swap_value()
 *
 * Search the supplied lfht looking for a node with the supplied id..
 *
 * If it is found, and the node is not marked for deletion, set the 
 * node's value to the supplied new_value, set *old_value_ptr to 
 * the old value of the node, and return true.
 *
 * Otherwise, return false.
 *
 *                                           JRM -- 7/15/23
 *
 * Changes:
 *
 *    None.
 *
 ************************************************************************/

bool lfht_swap_value(struct lfht_t * lfht_ptr,
                     unsigned long long int id,
                     void * new_value,
                     void ** old_value_ptr)

{
    bool success = false;
    long long int marked_nodes_visited = 0;
    long long int unmarked_nodes_visited = 0;
    long long int sentinels_traversed = 0;
    unsigned long long int hash;
    struct lfht_node_t * node_ptr = NULL;
    struct lfht_fl_node_t * fl_node_ptr;
    
    assert(lfht_ptr);
    assert(LFHT_VALID == lfht_ptr->tag);
    assert(id <= LFHT__MAX_ID);
    assert(old_value_ptr);

    fl_node_ptr = lfht_enter(lfht_ptr);

    hash = lfht_id_to_hash(id, false);

    /* now attempt to find the target */

    node_ptr = lfht_find_internal(lfht_ptr, hash, &marked_nodes_visited, 
                                  &unmarked_nodes_visited, &sentinels_traversed);

    if ( ( NULL == node_ptr ) || ( node_ptr->hash != hash ) ||
         ( ((unsigned long long)atomic_load(&(node_ptr->next))) & 0x01ULL)) {

        success = false;

    } else {

        assert(! node_ptr->sentinel);
        assert(hash == node_ptr->hash);
        *old_value_ptr = atomic_exchange(&(node_ptr->value), new_value);
        success = true;
    }

    /* update statistics */

    assert(marked_nodes_visited >= 0);
    assert(unmarked_nodes_visited >= 0);
    assert(sentinels_traversed >= 0);
    
    atomic_fetch_add(&(lfht_ptr->value_swaps), 1);

    if ( success ) {

        atomic_fetch_add(&(lfht_ptr->successful_val_swaps), 1);
        atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_succ_val_swaps), marked_nodes_visited);
        atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_succ_val_swaps), unmarked_nodes_visited);

    } else {

        atomic_fetch_add(&(lfht_ptr->failed_val_swaps), 1);
        atomic_fetch_add(&(lfht_ptr->marked_nodes_visited_in_failed_val_swaps), marked_nodes_visited);
        atomic_fetch_add(&(lfht_ptr->unmarked_nodes_visited_in_failed_val_swaps), unmarked_nodes_visited);
    }

    if ( sentinels_traversed > 0 ) {

        atomic_fetch_add(&(lfht_ptr->sentinels_traversed), sentinels_traversed);
    }

    lfht_exit(lfht_ptr, fl_node_ptr);

    return(success);

} /* lfht_swap_value() */
