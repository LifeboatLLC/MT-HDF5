/* Lock free hash table code */

#include <stdatomic.h>


/************************************************************************
 *
 * LFHT_ADD()
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
 *    Converted function to macro, added an output parameter for the return 
 *    value. Removed a conditional compile for adding a bit mask so that
 *    it is applied in all cases.
 * 
 *                                           AZO -- 2/12/25
 *
 ************************************************************************/

#define LFHT_ADD(lfht_ptr, id, value, lfht_add_success)                                                     \
    do{                                                                                                     \
                                                                                                            \
        int lfht_add__index_bits;                                                                           \
        unsigned long long lfht_add__curr_buckets_defined;                                                  \
        unsigned long long lfht_add__new_buckets_defined;                                                   \
        unsigned long long int lfht_add__lfht_add_hash;                                                     \
        struct lfht_node_t * lfht_add__bucket_head_ptr;                                                     \
        struct lfht_fl_node_t * lfht_add__fl_node_ptr = NULL;                                               \
        bool lfht_add__success;                                                                             \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert((id & LFHT_ID_BIT_MASK) <= LFHT__MAX_ID);          \
        /* if( H5_HAVE_MULTITHREAD )   */                                                                   \
            /* assert((id & ID_MASK) <= LFHT__MAX_ID);    */                                                \
        /* else */ /* H5_HAVE_MULTITHREAD */                                                                \
        /*assert(id <= LFHT__MAX_ID);                  */                                                       \
        /*#endif */ /* H5_HAVE_MULTITHREAD */                                                               \
                                                                                                            \
        LFHT_ENTER(lfht_ptr, &lfht_add__fl_node_ptr);                                                       \
                                                                                                            \
         /* if( H5_HAVE_MULTITHREAD )     */                                                                \
        LFHT_ID_TO_HASH((id & LFHT_ID_BIT_MASK), false, &lfht_add__lfht_add_hash);                                              \
        /* else */ /* H5_HAVE_MULTITHREAD */                                                                \
        /*LFHT_ID_TO_HASH(id, false, &lfht_add__lfht_add_hash);             */                                  \
        /*#endif */ /* H5_HAVE_MULTITHREAD */                                                               \
                                                                                                            \
        LFHT_GET_HASH_BUCKET_SENTINEL(lfht_ptr, lfht_add__lfht_add_hash, &lfht_add__bucket_head_ptr);       \
                                                                                                            \
        LFHT_ADD_INTERNAL(lfht_ptr, lfht_add__bucket_head_ptr, id, lfht_add__lfht_add_hash, false,          \
                          value, NULL, &lfht_add__success);                                                 \
                                                                                                            \
        /* Test to see if the logical length of the LFSLL has increased to  */                              \
        /* the point that we should double the (logical) size of the bucket */                              \
        /* index.  If so, increment the index_bits and buckets_defined      */                              \
        /* fields accordingly.                                              */                              \
                                                                                                            \
        lfht_add__index_bits = atomic_load(&((lfht_ptr)->index_bits));                                      \
        lfht_add__curr_buckets_defined = 0x01ULL << lfht_add__index_bits;                                   \
                                                                                                            \
        if ( lfht_add__index_bits < (lfht_ptr)->max_index_bits ) {                                          \
                                                                                                            \
            if ( (atomic_load(&((lfht_ptr)->lfsll_log_len)) / lfht_add__curr_buckets_defined) >= 8 ) {      \
                                                                                                            \
                /* Attempt to increment lfht_ptr->index_bits and lfht_buckets_defined. Must do  */          \
                /* this with a compare and exchange, as it is likely that other threads will be */          \
                /* attempting to do the same thing at more or less the same time.               */          \
                /**/                                                                                        \
                /* Do nothing if the compare and exchange fails, as that only means that        */          \
                /* another thread beat us to it.                                                */          \
                /**/                                                                                        \
                /* However, do collect stats.                                                   */          \
                                                                                                            \
                if ( atomic_compare_exchange_strong(&((lfht_ptr)->index_bits), &lfht_add__index_bits,       \
                     lfht_add__index_bits + 1) ) {                                                          \
                                                                                                            \
                    /* set of lfht_ptr->index_bits succeeded -- must update lfht_ptr->buckets_defined */    \
                    /* as well.  As it is possible that this update could be interleaved with another */    \
                    /* index_bits increment, this can get somewhat involved.                          */    \
                    /**/                                                                                    \
                    /* For the first pass, use the computed current and new value for buckets defined */    \
                    /* in the call to atomic_compare_exchange_strong(). If this succeeds, we are done.*/    \
                    /**/                                                                                    \
                    /* Otherwise, load the current values of lfht_ptr->index_bits and                 */    \
                    /* lfht_ptr->buckets_defined, and compute what lfht_ptr->buckets_defined should   */    \
                    /* be given the current value of lfht_ptr->index_bits. If the two values match,   */    \
                    /* or if the actual value is greater than the computed value, we are done.        */    \
                    /**/                                                                                    \
                    /* If not, call atomic_compare_exchange_strong() again to correct the value       */    \
                    /* of lfht_ptr->buckets_defined, and repeat as necessary.                         */    \
                                                                                                            \
                    bool first = true;                                                                      \
                    bool done = false;                                                                      \
                                                                                                            \
                    lfht_add__new_buckets_defined = lfht_add__curr_buckets_defined << 1;                    \
                                                                                                            \
                    do {                                                                                    \
                                                                                                            \
                        if ( atomic_compare_exchange_strong(&((lfht_ptr)->buckets_defined),                 \
                             &lfht_add__curr_buckets_defined, lfht_add__new_buckets_defined) ) {            \
                                                                                                            \
                            done = true;                                                                    \
                                                                                                            \
                        } else {                                                                            \
                                                                                                            \
                            if ( first ) {                                                                  \
                                                                                                            \
                                first = false;                                                              \
                                atomic_fetch_add(&((lfht_ptr)->buckets_defined_update_cols), 1);            \
                            }                                                                               \
                                                                                                            \
                            lfht_add__index_bits = atomic_load(&((lfht_ptr)->index_bits));                  \
                                                                                                            \
                            assert(lfht_add__index_bits <= (lfht_ptr)->max_index_bits);                     \
                                                                                                            \
                            lfht_add__new_buckets_defined = 0x01ULL << lfht_add__index_bits;                \
                                                                                                            \
                            lfht_add__curr_buckets_defined = atomic_load(&((lfht_ptr)->buckets_defined));   \
                                                                                                            \
                            if ( lfht_add__curr_buckets_defined >= lfht_add__new_buckets_defined ) {        \
                                                                                                            \
                                done = true;                                                                \
                                                                                                            \
                            } else {                                                                        \
                                                                                                            \
                                atomic_fetch_add(&((lfht_ptr)->buckets_defined_update_retries), 1);         \
                            }                                                                               \
                        }                                                                                   \
                    } while ( ! done );                                                                     \
                } else {                                                                                    \
                                                                                                            \
                    atomic_fetch_add(&((lfht_ptr)->index_bits_incr_cols), 1);                               \
                }                                                                                           \
            }                                                                                               \
        }                                                                                                   \
                                                                                                            \
        LFHT_EXIT(lfht_ptr, lfht_add__fl_node_ptr);                                                         \
                                                                                                            \
        *(lfht_add_success) = lfht_add__success;                                                            \
                                                                                                            \
    }while( 0 )/* LFHT_ADD() */

/***********************************************************************************
 *
 * LFHT_GET_NEXT()
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
 *    Converted from a function to a macro. Added an output parameter variable in order 
 *    to return the boolean value the function evaluates to.
 *  
 *                                                  AZO -- 2/15/25
 *
 ***********************************************************************************/

#define LFHT_GET_NEXT(lfht_ptr, old_id, id_ptr, value_ptr, lfht_get_next_success)                           \
    do {                                                                                                    \
        bool lfht_GN__success = false;                                                                      \
        bool lfht_GN__marked;                                                                               \
        unsigned long long int lfht_GN__marked_nodes_visited = 0;                                           \
        unsigned long long int lfht_GN__unmarked_nodes_visited = 0;                                         \
        unsigned long long int lfht_GN__sentinels_traversed = 0;                                            \
        unsigned long long int lfht_GN__id;                                                                 \
        unsigned long long int lfht_GN__old_hash;                                                           \
        void * lfht_GN__value;                                                                              \
        struct lfht_node_t * lfht_GN__next_ptr = NULL;                                                      \
        struct lfht_node_t * lfht_GN__node_ptr = NULL;                                                      \
        struct lfht_fl_node_t * lfht_GN__fl_node_ptr;                                                       \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(id_ptr);                                                                                     \
        assert(value_ptr);                                                                                  \
                                                                                                            \
        LFHT_ENTER(lfht_ptr, &lfht_GN__fl_node_ptr);                                                        \
                                                                                                            \
        /* compute the hash of old_id.  The node with this hash should still be */                          \
        /* in the hash table, but we have no way of enforcing this.  Thus, make */                          \
        /* no assumptions.                                                      */                          \
                                                                                                            \
        LFHT_ID_TO_HASH(old_id, false, &lfht_GN__old_hash);                                                 \
                                                                                                            \
        /* now search for the node in the hash table with the smallest hash  */                             \
        /* that is greater than lfht_GN__old_hash.  This algorithm is very similar to */                    \
        /* that used in lfht_find_internal().                                */                             \
                                                                                                            \
        LFHT_GET_HASH_BUCKET_SENTINEL(lfht_ptr, lfht_GN__old_hash, &lfht_GN__node_ptr);                     \
                                                                                                            \
        assert(LFHT_VALID_NODE == (lfht_GN__node_ptr)->tag);                                                \
        assert(0x0ULL == (((unsigned long long)(lfht_GN__node_ptr)) & 0x01ULL));                            \
        assert((lfht_GN__node_ptr)->sentinel);                                                              \
        assert((lfht_GN__node_ptr)->hash < lfht_GN__old_hash);                                              \
                                                                                                            \
        while ( ( lfht_GN__node_ptr ) && ( ! lfht_GN__success ) ) {                                         \
                                                                                                            \
            assert( LFHT_VALID_NODE == (lfht_GN__node_ptr)->tag );                                          \
                                                                                                            \
            lfht_GN__next_ptr = atomic_load(&((lfht_GN__node_ptr)->next));                                  \
                                                                                                            \
            /* test to see if next_ptr is marked.  If it, remove the   */                                   \
            /* mark so we can use it.                                  */                                   \
                                                                                                            \
            if ( ((unsigned long long)(lfht_GN__next_ptr)) & 0x01ULL ) {                                    \
                                                                                                            \
                assert(!((lfht_GN__node_ptr)->sentinel));                                                   \
                                                                                                            \
                /* node is marked -- remove the mark from next_ptr, and increment marked nodes visited */   \
                lfht_GN__next_ptr = (struct lfht_node_t *)(((unsigned long long)(lfht_GN__next_ptr))        \
                                                            & (~0x01ULL));                                  \
                                                                                                            \
                lfht_GN__marked = true;                                                                     \
                                                                                                            \
                lfht_GN__marked_nodes_visited++;                                                            \
                                                                                                            \
            } else {                                                                                        \
                                                                                                            \
                lfht_GN__marked = false;                                                                    \
                                                                                                            \
                if ( ! (lfht_GN__node_ptr)->sentinel ) {                                                    \
                                                                                                            \
                    lfht_GN__unmarked_nodes_visited++;                                                      \
                }                                                                                           \
            }                                                                                               \
                                                                                                            \
            assert( LFHT_VALID_NODE == (lfht_GN__node_ptr)->tag );                                          \
                                                                                                            \
            if ( (lfht_GN__node_ptr)->sentinel ) {                                                          \
                                                                                                            \
                lfht_GN__sentinels_traversed++;                                                             \
                                                                                                            \
            } else if ( ( ! lfht_GN__marked ) && ( (lfht_GN__node_ptr)->hash > lfht_GN__old_hash ) ) {      \
                                                                                                            \
                lfht_GN__id = (lfht_GN__node_ptr)->id;                                                      \
                lfht_GN__value = atomic_load(&((lfht_GN__node_ptr)->value));                                \
                lfht_GN__success = true;                                                                    \
            }                                                                                               \
                                                                                                            \
            lfht_GN__node_ptr = lfht_GN__next_ptr;                                                          \
        }                                                                                                   \
                                                                                                            \
        if ( lfht_GN__success ) {                                                                           \
                                                                                                            \
            *id_ptr    = lfht_GN__id;                                                                       \
            *value_ptr = lfht_GN__value;                                                                    \
        }                                                                                                   \
                                                                                                            \
        /* update statistics */                                                                             \
                                                                                                            \
        if ( lfht_GN__success ) {                                                                           \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->itter_nexts), 1);                                                \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->itter_ends), 1);                                                 \
        }                                                                                                   \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->marked_nodes_visited_in_itters), lfht_GN__marked_nodes_visited);     \
        atomic_fetch_add(&((lfht_ptr)->unmarked_nodes_visited_in_itters), lfht_GN__unmarked_nodes_visited); \
        atomic_fetch_add(&((lfht_ptr)->sentinels_traversed), lfht_GN__sentinels_traversed);                 \
        atomic_fetch_add(&((lfht_ptr)->sentinels_traversed_in_itters), lfht_GN__sentinels_traversed);       \
                                                                                                            \
        LFHT_EXIT(lfht_ptr, lfht_GN__fl_node_ptr);                                                          \
                                                                                                            \
        *lfht_get_next_success = lfht_GN__success;                                                          \
                                                                                                            \
    } while( 0 )/* LFHT_GET_NEXT() */


/************************************************************************
 *
 * LFHT_INIT()
 *
 *     Initialize an instance of lfht_t.
 *
 *                           JRM -- 6/30/23
 * 
 * Changes: 
 *     
 *   - Converted from a function to a macro.
 * 
 *                          AZO -- 2/24/25
 * 
 *
 ************************************************************************/

#define LFHT_INIT(lfht_ptr)                                                                                 \
    do {                                                                                                    \
        int lfht_init__i;                                                                                   \
        unsigned long long int lfht_init__mask = 0x0ULL;                                                    \
        struct lfht_flsptr_t lfht_init__init_lfht_flsptr = {NULL, 0x0ULL};                                  \
        struct lfht_flsptr_t lfht_init__fl_shead;                                                           \
        struct lfht_flsptr_t lfht_init__fl_stail;                                                           \
        struct lfht_flsptr_t lfht_init__snext;                                                              \
                                                                                                            \
                                                                                                            \
        struct lfht_node_t * lfht_init__head_sentinel_ptr = NULL;                                           \
        struct lfht_node_t * lfht_init__tail_sentinel_ptr = NULL;                                           \
        struct lfht_fl_node_t * lfht_init__fl_node_ptr = NULL;                                              \
        struct lfht_node_t * lfht_init__fl_node_ptr_ptr = NULL;                                             \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
                                                                                                            \
        (lfht_ptr)->tag = LFHT_VALID;                                                                       \
                                                                                                            \
                                                                                                            \
        /* lock free singly linked list */                                                                  \
        atomic_init(&((lfht_ptr)->lfsll_root), NULL);                                                       \
        atomic_init(&((lfht_ptr)->lfsll_log_len), 0ULL);                                                    \
        atomic_init(&((lfht_ptr)->lfsll_phys_len), 2ULL);                                                   \
                                                                                                            \
                                                                                                            \
        /* free list */                                                                                     \
        atomic_init(&((lfht_ptr)->fl_shead), lfht_init__init_lfht_flsptr);                                  \
        atomic_init(&((lfht_ptr)->fl_stail), lfht_init__init_lfht_flsptr);                                  \
        atomic_init(&((lfht_ptr)->fl_len), 1LL);                                                            \
        (lfht_ptr)->max_desired_fl_len = LFHT__MAX_DESIRED_FL_LEN;                                          \
        atomic_init(&((lfht_ptr)->next_sn), 0ULL);                                                          \
                                                                                                            \
                                                                                                            \
        /* hash bucket index */                                                                             \
        atomic_init(&((lfht_ptr)->index_bits), 0);                                                          \
        (lfht_ptr)->max_index_bits = LFHT__MAX_INDEX_BITS;                                                  \
        for ( lfht_init__i = 0; lfht_init__i <= LFHT__NUM_HASH_BITS; lfht_init__i++ ) {                     \
                                                                                                            \
            (lfht_ptr)->index_masks[lfht_init__i] = lfht_init__mask;                                        \
            lfht_init__mask <<= 1;                                                                          \
            lfht_init__mask |= 0x01ULL;                                                                     \
        }                                                                                                   \
        atomic_init(&((lfht_ptr)->buckets_defined), 1);                                                     \
        atomic_init(&((lfht_ptr)->buckets_initialized), 0);                                                 \
        for ( lfht_init__i = 0; lfht_init__i < LFHT__BASE_IDX_LEN; lfht_init__i++ ) {                       \
                                                                                                            \
            atomic_init(&(((lfht_ptr)->bucket_idx)[lfht_init__i]), NULL);                                   \
        }                                                                                                   \
                                                                                                            \
        /* statistics */                                                                                    \
        atomic_init(&((lfht_ptr)->max_lfsll_log_len), 0LL);                                                 \
        atomic_init(&((lfht_ptr)->max_lfsll_phys_len), 0LL);                                                \
                                                                                                            \
        atomic_init(&((lfht_ptr)->max_fl_len), 1LL);                                                        \
        atomic_init(&((lfht_ptr)->num_nodes_allocated), 0LL);                                               \
        atomic_init(&((lfht_ptr)->num_nodes_freed), 0LL);                                                   \
        atomic_init(&((lfht_ptr)->num_node_free_candidate_selection_restarts), 0LL);                        \
        atomic_init(&((lfht_ptr)->num_nodes_added_to_fl), 0LL);                                             \
        atomic_init(&((lfht_ptr)->num_nodes_drawn_from_fl), 0LL);                                           \
        atomic_init(&((lfht_ptr)->num_fl_head_update_cols), 0LL);                                           \
        atomic_init(&((lfht_ptr)->num_fl_tail_update_cols), 0LL);                                           \
        atomic_init(&((lfht_ptr)->num_fl_append_cols), 0LL);                                                \
        atomic_init(&((lfht_ptr)->num_fl_req_denied_due_to_empty), 0LL);                                    \
        atomic_init(&((lfht_ptr)->num_fl_req_denied_due_to_ref_count), 0LL);                                \
        atomic_init(&((lfht_ptr)->num_fl_node_ref_cnt_incs), 0LL);                                          \
        atomic_init(&((lfht_ptr)->num_fl_node_ref_cnt_inc_retrys), 0LL);                                    \
        atomic_init(&((lfht_ptr)->num_fl_node_ref_cnt_decs), 0LL);                                          \
        atomic_init(&((lfht_ptr)->num_fl_frees_skiped_due_to_empty), 0LL);                                  \
        atomic_init(&((lfht_ptr)->num_fl_frees_skiped_due_to_ref_count), 0LL);                              \
                                                                                                            \
        atomic_init(&((lfht_ptr)->index_bits_incr_cols), 0LL);                                              \
        atomic_init(&((lfht_ptr)->buckets_defined_update_cols), 0LL);                                       \
        atomic_init(&((lfht_ptr)->buckets_defined_update_retries), 0LL);                                    \
        atomic_init(&((lfht_ptr)->bucket_init_cols), 0LL);                                                  \
        atomic_init(&((lfht_ptr)->bucket_init_col_sleeps), 0LL);                                            \
        atomic_init(&((lfht_ptr)->recursive_bucket_inits), 0LL);                                            \
        atomic_init(&((lfht_ptr)->sentinels_traversed), 0LL);                                               \
                                                                                                            \
        atomic_init(&((lfht_ptr)->insertions), 0LL);                                                        \
        atomic_init(&((lfht_ptr)->insertion_failures), 0LL);                                                \
        atomic_init(&((lfht_ptr)->ins_restarts_due_to_ins_col), 0LL);                                       \
        atomic_init(&((lfht_ptr)->ins_restarts_due_to_del_col), 0LL);                                       \
        atomic_init(&((lfht_ptr)->ins_deletion_completions), 0LL);                                          \
        atomic_init(&((lfht_ptr)->nodes_visited_during_ins), 0LL);                                          \
                                                                                                            \
        atomic_init(&((lfht_ptr)->deletion_attempts), 0LL);                                                 \
        atomic_init(&((lfht_ptr)->deletion_starts), 0LL);                                                   \
        atomic_init(&((lfht_ptr)->deletion_start_cols), 0LL);                                               \
        atomic_init(&((lfht_ptr)->deletion_failures), 0LL);                                                 \
        atomic_init(&((lfht_ptr)->del_restarts_due_to_del_col), 0LL);                                       \
        atomic_init(&((lfht_ptr)->del_retries), 0LL);                                                       \
        atomic_init(&((lfht_ptr)->del_deletion_completions), 0LL);                                          \
        atomic_init(&((lfht_ptr)->nodes_visited_during_dels), 0LL);                                         \
                                                                                                            \
        atomic_init(&((lfht_ptr)->searches), 0LL);                                                          \
        atomic_init(&((lfht_ptr)->successful_searches), 0LL);                                               \
        atomic_init(&((lfht_ptr)->failed_searches), 0LL);                                                   \
        atomic_init(&((lfht_ptr)->marked_nodes_visited_in_succ_searches), 0LL);                             \
        atomic_init(&((lfht_ptr)->unmarked_nodes_visited_in_succ_searches), 0LL);                           \
        atomic_init(&((lfht_ptr)->marked_nodes_visited_in_failed_searches), 0LL);                           \
        atomic_init(&((lfht_ptr)->unmarked_nodes_visited_in_failed_searches), 0LL);                         \
                                                                                                            \
        atomic_init(&((lfht_ptr)->value_swaps), 0LL);                                                       \
        atomic_init(&((lfht_ptr)->successful_val_swaps), 0LL);                                              \
        atomic_init(&((lfht_ptr)->failed_val_swaps), 0LL);                                                  \
        atomic_init(&((lfht_ptr)->marked_nodes_visited_in_succ_val_swaps), 0LL);                            \
        atomic_init(&((lfht_ptr)->unmarked_nodes_visited_in_succ_val_swaps), 0LL);                          \
        atomic_init(&((lfht_ptr)->marked_nodes_visited_in_failed_val_swaps), 0LL);                          \
        atomic_init(&((lfht_ptr)->unmarked_nodes_visited_in_failed_val_swaps), 0LL);                        \
                                                                                                            \
        atomic_init(&((lfht_ptr)->value_searches), 0LL);                                                    \
        atomic_init(&((lfht_ptr)->successful_val_searches), 0LL);                                           \
        atomic_init(&((lfht_ptr)->failed_val_searches), 0LL);                                               \
        atomic_init(&((lfht_ptr)->marked_nodes_visited_in_val_searches), 0LL);                              \
        atomic_init(&((lfht_ptr)->unmarked_nodes_visited_in_val_searches), 0LL);                            \
        atomic_init(&((lfht_ptr)->sentinels_traversed_in_val_searches), 0LL);                               \
                                                                                                            \
        atomic_init(&((lfht_ptr)->itter_inits), 0LL);                                                       \
        atomic_init(&((lfht_ptr)->itter_nexts), 0LL);                                                       \
        atomic_init(&((lfht_ptr)->itter_ends), 0LL);                                                        \
        atomic_init(&((lfht_ptr)->marked_nodes_visited_in_itters), 0LL);                                    \
        atomic_init(&((lfht_ptr)->unmarked_nodes_visited_in_itters), 0LL);                                  \
        atomic_init(&((lfht_ptr)->sentinels_traversed_in_itters), 0LL);                                     \
                                                                                                            \
        /* setup hash table */                                                                              \
                                                                                                            \
        LFHT_CREATE_NODE(lfht_ptr, 0ULL, 0ULL, true, NULL, &lfht_init__head_sentinel_ptr);                  \
        assert(lfht_init__head_sentinel_ptr);                                                               \
        assert((lfht_init__head_sentinel_ptr)->tag == LFHT_VALID_NODE);                                     \
                                                                                                            \
        LFHT_CREATE_NODE(lfht_ptr, 0ULL, 0ULL, true, NULL, &lfht_init__tail_sentinel_ptr);                  \
        (lfht_init__tail_sentinel_ptr)->hash = LLONG_MAX;                                                   \
        assert(lfht_init__tail_sentinel_ptr);                                                               \
        assert((lfht_init__tail_sentinel_ptr)->tag == LFHT_VALID_NODE);                                     \
                                                                                                            \
        assert(NULL == atomic_load(&((lfht_init__tail_sentinel_ptr)->next)));                               \
        atomic_store(&((lfht_init__head_sentinel_ptr)->next), lfht_init__tail_sentinel_ptr);                \
        atomic_store(&((lfht_ptr)->lfsll_root), lfht_init__head_sentinel_ptr);                              \
                                                                                                            \
        assert(NULL == atomic_load(&((lfht_init__tail_sentinel_ptr)->next)));                               \
        atomic_store(&((lfht_init__head_sentinel_ptr)->next), lfht_init__tail_sentinel_ptr);                \
        atomic_store(&((lfht_ptr)->lfsll_root), lfht_init__head_sentinel_ptr);                              \
                                                                                                            \
        /* insert the zero-th bucket sentinel manually. */                                                  \
        atomic_store(&(((lfht_ptr)->bucket_idx)[0]), lfht_init__head_sentinel_ptr);                         \
        atomic_fetch_add(&((lfht_ptr)->buckets_initialized), 1);                                            \
                                                                                                            \
        /* Setup the free list.                                          */                                 \
        /*                                                               */                                 \
        /* The free list must always have at least one node.  Allocate,  */                                 \
        /* initialize, and insert a node in the free list.               */                                 \
                                                                                                            \
        LFHT_CREATE_NODE(lfht_ptr, 0ULL, 0ULL, false, NULL, &lfht_init__fl_node_ptr_ptr);                   \
        lfht_init__fl_node_ptr = (struct lfht_fl_node_t *)lfht_init__fl_node_ptr_ptr;                       \
        assert(lfht_init__fl_node_ptr);                                                                     \
        assert(LFHT_FL_NODE_IN_USE == (lfht_init__fl_node_ptr)->tag);                                       \
        atomic_store(&((lfht_init__fl_node_ptr)->tag), LFHT_FL_NODE_ON_FL);                                 \
        lfht_init__snext = atomic_load(&((lfht_init__fl_node_ptr)->snext));                                 \
        assert(NULL == lfht_init__snext.ptr);                                                               \
        assert(0 == atomic_load(&((lfht_init__fl_node_ptr)->ref_count)));                                   \
        lfht_init__fl_shead.ptr = lfht_init__fl_node_ptr;                                                   \
        lfht_init__fl_shead.sn  = 1ULL;                                                                     \
        atomic_store(&((lfht_ptr)->fl_shead), lfht_init__fl_shead);                                         \
        lfht_init__fl_stail.ptr = lfht_init__fl_node_ptr;                                                   \
        lfht_init__fl_stail.sn  = 1ULL;                                                                     \
        atomic_store(&((lfht_ptr)->fl_stail), lfht_init__fl_stail);                                         \
                                                                                                            \
    } while( 0 ) /* LFHT_INIT() */



/************************************************************************
 *
 * LFHT_ADD_INTERNAL()
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
 *    Converted function to a macro - added an output parameter variable for
 *    the return value. 
 *                                           AZO -- 2/12/25
 *
 ************************************************************************/
#define LFHT_ADD_INTERNAL(lfht_ptr, bucket_head_ptr, id, lfht_hash, lfht_sentinel, value,                   \
                          new_node_ptr_ptr, lfht_add_internal_success)                                      \
    do {                                                                                                    \
        bool lfht_AI__done = false;                                                                         \
        bool lfht_AI__success = false;                                                                      \
        int lfht_AI__del_completions = 0;                                                                   \
        int lfht_AI__del_completion_cols = 0;                                                               \
        int lfht_AI__insertion_cols = 0;                                                                    \
        int lfht_AI__nodes_visited = 0;                                                                     \
        unsigned long long int lfht_AI__lfsll_log_len;                                                      \
        unsigned long long int lfht_AI__max_lfsll_log_len;                                                  \
        unsigned long long int lfht_AI__lfsll_phys_len;                                                     \
        unsigned long long int lfht_AI__max_lfsll_phys_len;                                                 \
        struct lfht_node_t *lfht_AI__new_node_ptr = NULL;                                                   \
        struct lfht_node_t *lfht_AI__first_node_ptr = NULL;                                                 \
        struct lfht_node_t *lfht_AI__second_node_ptr = NULL;                                                \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(bucket_head_ptr);                                                                            \
        assert(LFHT_VALID_NODE == (bucket_head_ptr)->tag);                                                  \
        assert((bucket_head_ptr)->sentinel);                                                                \
        assert((bucket_head_ptr)->hash < lfht_hash);                                                        \
        assert((lfht_sentinel) || (0x01ULL == (lfht_hash & 0x01ULL)));                                      \
                                                                                                            \
         /* Allocate the new node now. The objective is to         */                                       \
         /* minimize the window between when lfmt_find_mod_point() */                                       \
         /* returns, and we actually perform the insertion.        */                                       \
         /**/                                                                                               \
         /* This has a cost -- as the new value may already exist, */                                       \
         /* in which case we must discard the node and return      */                                       \
         /* failure.                                               */                                       \
                                                                                                            \
        LFHT_CREATE_NODE(lfht_ptr, id, lfht_hash, lfht_sentinel, value, &lfht_AI__new_node_ptr);            \
        assert(lfht_AI__new_node_ptr);                                                                      \
        assert(LFHT_VALID_NODE == (lfht_AI__new_node_ptr)->tag);                                            \
                                                                                                            \
         /* Now do the insertion.  We repeat until we are successful, */                                    \
         /* or we discover that the sll already contains a node with  */                                    \
         /* the specified hash.                                       */                                    \
                                                                                                            \
        do {                                                                                                \
            lfht_AI__first_node_ptr = NULL;                                                                 \
            lfht_AI__second_node_ptr = NULL;                                                                \
                                                                                                            \
             /* In its current implementation, lfsll_find_mod_point() will */                               \
             /*  either succeed or trigger an assertion -- thus no need to */                               \
             /*  check return value at present.                            */                               \
                                                                                                            \
            LFHT_FIND_MOD_POINT((lfht_ptr),                                                                 \
                                (bucket_head_ptr),                                                          \
                                &lfht_AI__first_node_ptr,                                                   \
                                &lfht_AI__second_node_ptr,                                                  \
                                &lfht_AI__del_completion_cols,                                              \
                                &lfht_AI__del_completions,                                                  \
                                &lfht_AI__nodes_visited,                                                    \
                                lfht_hash);                                                                 \
                                                                                                            \
            assert(lfht_AI__first_node_ptr);                                                                \
                                                                                                            \
            if ((lfht_hash) == (lfht_AI__first_node_ptr)->hash) {                                           \
                                                                                                            \
                 /* Value already exists in the SLL. Discard the new node, */                               \
                 /*  and report failure. Note that we must mark new_node_ptr->next */                       \
                 /*  to keep lfsll_discard_node() happy.*/                                                  \
                                                                                                            \
                atomic_store(&((lfht_AI__new_node_ptr)->next), (struct lfht_node_t *)0x01ULL);              \
                LFHT_DISCARD_NODE((lfht_ptr), lfht_AI__new_node_ptr, 0);                                    \
                lfht_AI__new_node_ptr = NULL;                                                               \
                lfht_AI__done = true;                                                                       \
                lfht_AI__success = false;                                                                   \
            } else {                                                                                        \
                assert(lfht_AI__second_node_ptr);                                                           \
                 /* Load the new node next ptr with the second_ptr */                                       \
                atomic_store(&((lfht_AI__new_node_ptr)->next), lfht_AI__second_node_ptr);                   \
                                                                                                            \
                 /* Attempt to insert *new_node_ptr into the hash table's SLL */                            \
                if (atomic_compare_exchange_strong(&((lfht_AI__first_node_ptr)->next),                      \
                                                   &lfht_AI__second_node_ptr, lfht_AI__new_node_ptr)) {     \
                                                                                                            \
                    /* insertion succeeded */                                                               \
                                                                                                            \
                    /* increment the logical and physical length of the lfsll */                            \
                    if (!(lfht_sentinel)) {                                                                 \
                        atomic_fetch_add(&((lfht_ptr)->lfsll_log_len), 1);                                  \
                    }                                                                                       \
                    atomic_fetch_add(&((lfht_ptr)->lfsll_phys_len), 1);                                     \
                    lfht_AI__done = true;                                                                   \
                    lfht_AI__success = true;                                                                \
                } else {                                                                                    \
                    lfht_AI__insertion_cols++;                                                              \
                }                                                                                           \
            }                                                                                               \
        } while (!lfht_AI__done);                                                                           \
                                                                                                            \
        if ((lfht_AI__success) && (new_node_ptr_ptr)) {                                                     \
            *((struct lfht_node_t **)new_node_ptr_ptr) = (lfht_AI__new_node_ptr);                           \
        }                                                                                                   \
                                                                                                            \
         /* Update statistics */                                                                            \
        if (lfht_AI__success) {                                                                             \
            if (!(lfht_sentinel)) {                                                                         \
                atomic_fetch_add(&((lfht_ptr)->insertions), 1);                                             \
            }                                                                                               \
         /* Collect stats on successful sentinel insertions? */ /* JRM */                                   \
        } else {                                                                                            \
            if (!(lfht_sentinel)) {                                                                         \
                atomic_fetch_add(&((lfht_ptr)->insertion_failures), 1);                                     \
            }                                                                                               \
            /* Collect stats on failed sentinel insertions? */ /* JRM */                                    \
        }                                                                                                   \
                                                                                                            \
         /* If appropriate, attempt updating lfht_ptr->max_lfsll_log_len and lfht_ptr->max_lfsll_phys_len.*/\
         /* In the event of a collision, just ignore it and go on, as I don't see any reasonable way to   */\
         /* recover.                                                                                      */\
                                                                                                            \
        if (((lfht_AI__lfsll_log_len) = atomic_load(&((lfht_ptr)->lfsll_log_len))) >                        \
            ((lfht_AI__max_lfsll_log_len) = atomic_load(&((lfht_ptr)->max_lfsll_log_len)))) {               \
                                                                                                            \
            atomic_compare_exchange_strong(&((lfht_ptr)->max_lfsll_log_len),                                \
                                           &lfht_AI__max_lfsll_log_len, lfht_AI__lfsll_log_len);            \
        }                                                                                                   \
                                                                                                            \
        if (((lfht_AI__lfsll_phys_len) = atomic_load(&((lfht_ptr)->lfsll_phys_len))) >                      \
            ((lfht_AI__max_lfsll_phys_len) = atomic_load(&((lfht_ptr)->max_lfsll_phys_len)))) {             \
            atomic_compare_exchange_strong(&((lfht_ptr)->max_lfsll_phys_len),                               \
                                           &lfht_AI__max_lfsll_phys_len, lfht_AI__lfsll_phys_len);          \
        }                                                                                                   \
                                                                                                            \
        assert(lfht_AI__insertion_cols >= 0);                                                               \
        assert(lfht_AI__del_completion_cols >= 0);                                                          \
        assert(lfht_AI__del_completions >= 0);                                                              \
        assert(lfht_AI__nodes_visited >= 0);                                                                \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->ins_restarts_due_to_ins_col), lfht_AI__insertion_cols);              \
        atomic_fetch_add(&((lfht_ptr)->ins_restarts_due_to_del_col), lfht_AI__del_completion_cols);         \
        atomic_fetch_add(&((lfht_ptr)->ins_deletion_completions), lfht_AI__del_completions);                \
        atomic_fetch_add(&((lfht_ptr)->nodes_visited_during_ins), lfht_AI__nodes_visited);                  \
                                                                                                            \
        *lfht_add_internal_success = (lfht_AI__success);                                                    \
                                                                                                            \
    } while( 0 ) /* LFHT_ADD_INTERNAL() */

/************************************************************************
 *
 * LFHT_CLEAR()
 *
 *     Clear the supplied instance of lfht_t in preparation for deletion.
 *
 *                                           JRM -- 5/30/23
 * 
 *     Changes:
 *          
 *     Converted from a function to a macro. 
 *                                           
 *                                           Anna Burton -- 2/05/25
 *
 ************************************************************************/
#define LFHT_CLEAR(lfht_ptr)                                                                                \
    do {                                                                                                    \
        unsigned long long lfht_clear__marked_nodes_discarded = 0;                                          \
        unsigned long long lfht_clear__unmarked_nodes_discarded = 0;                                        \
        unsigned long long lfht_clear__sentinel_nodes_discarded = 0;                                        \
        struct lfht_node_t *lfht_clear__discard_ptr = NULL;                                                 \
        struct lfht_node_t *lfht_clear__node_ptr = NULL;                                                    \
        struct lfht_fl_node_t *lfht_clear__fl_discard_ptr = NULL;                                           \
        struct lfht_fl_node_t *lfht_clear__fl_node_ptr = NULL;                                              \
        struct lfht_flsptr_t lfht_clear__init_flsptr = {NULL, 0x0ULL};                                      \
        struct lfht_flsptr_t lfht_clear__fl_shead;                                                          \
        struct lfht_flsptr_t lfht_clear__snext;                                                             \
                                                                                                            \
        assert((lfht_ptr) != NULL);                                                                         \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
                                                                                                            \
         /* Delete the elements of the LFSLL -- note that this moves */                                     \
         /*  all elements from the LFHT to the free list.            */                                     \
                                                                                                            \
        lfht_clear__node_ptr = atomic_load(&((lfht_ptr)->lfsll_root));                                      \
        atomic_store(&((lfht_ptr)->lfsll_root), NULL);                                                      \
                                                                                                            \
        while (lfht_clear__node_ptr) {                                                                      \
            assert(LFHT_VALID_NODE == (lfht_clear__node_ptr)->tag);                                         \
            lfht_clear__discard_ptr = lfht_clear__node_ptr;                                                 \
            lfht_clear__node_ptr = atomic_load(&((lfht_clear__discard_ptr)->next));                         \
                                                                                                            \
             /* First test to see if the node is a sentinel node -- */                                      \
             /* if it is, it must not be marked                     */                                      \
                                                                                                            \
            if ((lfht_clear__discard_ptr)->sentinel) {                                                      \
                                                                                                            \
                 /* sentinel nodes can't be marked for deletion -- verify this */                           \
                assert(0x0ULL == (((unsigned long long)((lfht_clear__discard_ptr)->next)) & 0x01ULL));      \
                                                                                                            \
                 /* Mark discard_ptr->next to keep LFHT_DISCARD_NODE() happy */                             \
                atomic_store(&((lfht_clear__discard_ptr)->next),                                            \
                            (struct lfht_node_t *)(((unsigned long long)(lfht_clear__node_ptr)) | 0x01ULL));\
                lfht_clear__sentinel_nodes_discarded++;                                                     \
            } else {                                                                                        \
                                                                                                            \
                 /* Test to see if node_ptr is marked. If it is, remove the */                              \
                 /* mark so we can use it.                                  */                              \
                                                                                                            \
                if (((unsigned long long)(lfht_clear__node_ptr)) & 0x01ULL) {                               \
                     /* node_ptr is marked -- remove the mark and increment marked nodes visited */         \
                    lfht_clear__node_ptr = (struct lfht_node_t *)(((unsigned long long)                     \
                                            (lfht_clear__node_ptr)) & (~0x01ULL));                          \
                    lfht_clear__marked_nodes_discarded++;                                                   \
                } else {                                                                                    \
                     /* Mark discard_ptr->next to keep LFHT_DISCARD_NODE() happy */                         \
                    atomic_store(&((lfht_clear__discard_ptr)->next),(struct lfht_node_t *)                  \
                                  (((unsigned long long)(lfht_clear__node_ptr)) | 0x01ULL));                \
                    lfht_clear__unmarked_nodes_discarded++;                                                 \
                }                                                                                           \
            }                                                                                               \
                                                                                                            \
            LFHT_DISCARD_NODE((lfht_ptr), lfht_clear__discard_ptr, 0);                                      \
        }                                                                                                   \
                                                                                                            \
        assert(atomic_load(&((lfht_ptr)->buckets_initialized)) + 1 == lfht_clear__sentinel_nodes_discarded);\
        assert(atomic_load(&((lfht_ptr)->lfsll_phys_len)) ==                                                \
               (lfht_clear__sentinel_nodes_discarded) + (lfht_clear__marked_nodes_discarded)                \
               + (lfht_clear__unmarked_nodes_discarded));                                                   \
        assert(atomic_load(&((lfht_ptr)->lfsll_log_len)) == lfht_clear__unmarked_nodes_discarded);          \
                                                                                                            \
         /* Now delete and free all items in the free list.  Do */                                          \
         /*  this directly, as LFHT_DISCARD_NODE() will try to  */                                          \
         /*  put them back on the free list.                    */                                          \
                                                                                                            \
        lfht_clear__fl_shead = atomic_load(&((lfht_ptr)->fl_shead));                                        \
        lfht_clear__fl_node_ptr = lfht_clear__fl_shead.ptr;                                                 \
                                                                                                            \
        atomic_store(&((lfht_ptr)->fl_shead), lfht_clear__init_flsptr);                                     \
        atomic_store(&((lfht_ptr)->fl_stail), lfht_clear__init_flsptr);                                     \
        atomic_store(&((lfht_ptr)->next_sn), 0ULL);                                                         \
                                                                                                            \
        while (lfht_clear__fl_node_ptr) {                                                                   \
            assert(LFHT_FL_NODE_ON_FL == (lfht_clear__fl_node_ptr)->tag);                                   \
                                                                                                            \
            lfht_clear__fl_discard_ptr = lfht_clear__fl_node_ptr;                                           \
            lfht_clear__snext = atomic_load(&((lfht_clear__fl_discard_ptr)->snext));                        \
            lfht_clear__fl_node_ptr = lfht_clear__snext.ptr;                                                \
                                                                                                            \
            (lfht_clear__discard_ptr)->tag = LFHT_FL_NODE_INVALID;                                          \
            lfht_clear__snext.ptr = NULL;                                                                   \
            lfht_clear__snext.sn = 0ULL;                                                                    \
            atomic_store(&((lfht_clear__fl_discard_ptr)->snext), lfht_clear__snext);                        \
                                                                                                            \
            free((void *)lfht_clear__fl_discard_ptr);                                                       \
        }                                                                                                   \
    } while( 0 ) /* LFHT_CLEAR() */

/************************************************************************
 *
 * LFHT_CLEAR_STATS()
 *
 *     Set all the stats fields in the supplied instance of lfht_t
 *     to zero..
 *
 *                           JRM -- 5/30/23
 *     Changes:
 *          
 *     Converted from function to macro. 
 *  
 *                          Anna Burton -- 2/13/25
 *
 ************************************************************************/

#define LFHT_CLEAR_STATS(lfht_ptr)                                                                          \
    do{                                                                                                     \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
                                                                                                            \
        atomic_store(&((lfht_ptr)->max_lfsll_log_len), 0LL);                                                \
        atomic_store(&((lfht_ptr)->max_lfsll_phys_len), 0LL);                                               \
                                                                                                            \
        atomic_store(&((lfht_ptr)->max_fl_len), 0LL);                                                       \
        atomic_store(&((lfht_ptr)->num_nodes_allocated), 0LL);                                              \
        atomic_store(&((lfht_ptr)->num_nodes_freed), 0LL);                                                  \
        atomic_store(&((lfht_ptr)->num_node_free_candidate_selection_restarts), 0LL);                       \
        atomic_store(&((lfht_ptr)->num_nodes_added_to_fl), 0LL);                                            \
        atomic_store(&((lfht_ptr)->num_nodes_drawn_from_fl), 0LL);                                          \
        atomic_store(&((lfht_ptr)->num_fl_head_update_cols), 0LL);                                          \
        atomic_store(&((lfht_ptr)->num_fl_tail_update_cols), 0LL);                                          \
        atomic_store(&((lfht_ptr)->num_fl_append_cols), 0LL);                                               \
        atomic_store(&((lfht_ptr)->num_fl_req_denied_due_to_empty), 0LL);                                   \
        atomic_store(&((lfht_ptr)->num_fl_req_denied_due_to_ref_count), 0LL);                               \
        atomic_store(&((lfht_ptr)->num_fl_node_ref_cnt_incs), 0LL);                                         \
        atomic_store(&((lfht_ptr)->num_fl_node_ref_cnt_inc_retrys), 0LL);                                   \
        atomic_store(&((lfht_ptr)->num_fl_node_ref_cnt_decs), 0LL);                                         \
        atomic_store(&((lfht_ptr)->num_fl_frees_skiped_due_to_empty), 0LL);                                 \
        atomic_store(&((lfht_ptr)->num_fl_frees_skiped_due_to_ref_count), 0LL);                             \
                                                                                                            \
        atomic_store(&((lfht_ptr)->index_bits_incr_cols), 0LL);                                             \
        atomic_store(&((lfht_ptr)->buckets_defined_update_cols), 0LL);                                      \
        atomic_store(&((lfht_ptr)->buckets_defined_update_retries), 0LL);                                   \
        atomic_store(&((lfht_ptr)->bucket_init_cols), 0LL);                                                 \
        atomic_store(&((lfht_ptr)->bucket_init_col_sleeps), 0LL);                                           \
        atomic_store(&((lfht_ptr)->recursive_bucket_inits), 0LL);                                           \
        atomic_store(&((lfht_ptr)->sentinels_traversed), 0LL);                                              \
                                                                                                            \
        atomic_store(&((lfht_ptr)->insertions), 0LL);                                                       \
        atomic_store(&((lfht_ptr)->insertion_failures), 0LL);                                               \
        atomic_store(&((lfht_ptr)->ins_restarts_due_to_ins_col), 0LL);                                      \
        atomic_store(&((lfht_ptr)->ins_restarts_due_to_del_col), 0LL);                                      \
        atomic_store(&((lfht_ptr)->ins_deletion_completions), 0LL);                                         \
        atomic_store(&((lfht_ptr)->nodes_visited_during_ins), 0LL);                                         \
                                                                                                            \
        atomic_store(&((lfht_ptr)->deletion_attempts), 0LL);                                                \
        atomic_store(&((lfht_ptr)->deletion_starts), 0LL);                                                  \
        atomic_store(&((lfht_ptr)->deletion_start_cols), 0LL);                                              \
        atomic_store(&((lfht_ptr)->deletion_failures), 0LL);                                                \
        atomic_store(&((lfht_ptr)->del_restarts_due_to_del_col), 0LL);                                      \
        atomic_store(&((lfht_ptr)->del_retries), 0LL);                                                      \
        atomic_store(&((lfht_ptr)->del_deletion_completions), 0LL);                                         \
        atomic_store(&((lfht_ptr)->nodes_visited_during_dels), 0LL);                                        \
                                                                                                            \
        atomic_store(&((lfht_ptr)->searches), 0LL);                                                         \
        atomic_store(&((lfht_ptr)->successful_searches), 0LL);                                              \
        atomic_store(&((lfht_ptr)->failed_searches), 0LL);                                                  \
        atomic_store(&((lfht_ptr)->marked_nodes_visited_in_succ_searches), 0LL);                            \
        atomic_store(&((lfht_ptr)->unmarked_nodes_visited_in_succ_searches), 0LL);                          \
        atomic_store(&((lfht_ptr)->marked_nodes_visited_in_failed_searches), 0LL);                          \
        atomic_store(&((lfht_ptr)->unmarked_nodes_visited_in_failed_searches), 0LL);                        \
                                                                                                            \
        atomic_store(&((lfht_ptr)->value_swaps), 0LL);                                                      \
        atomic_store(&((lfht_ptr)->successful_val_swaps), 0LL);                                             \
        atomic_store(&((lfht_ptr)->failed_val_swaps), 0LL);                                                 \
        atomic_store(&((lfht_ptr)->marked_nodes_visited_in_succ_val_swaps), 0LL);                           \
        atomic_store(&((lfht_ptr)->unmarked_nodes_visited_in_succ_val_swaps), 0LL);                         \
        atomic_store(&((lfht_ptr)->marked_nodes_visited_in_failed_val_swaps), 0LL);                         \
        atomic_store(&((lfht_ptr)->unmarked_nodes_visited_in_failed_val_swaps), 0LL);                       \
                                                                                                            \
        atomic_store(&((lfht_ptr)->value_searches), 0LL);                                                   \
        atomic_store(&((lfht_ptr)->successful_val_searches), 0LL);                                          \
        atomic_store(&((lfht_ptr)->failed_val_searches), 0LL);                                              \
        atomic_store(&((lfht_ptr)->marked_nodes_visited_in_val_searches), 0LL);                             \
        atomic_store(&((lfht_ptr)->unmarked_nodes_visited_in_val_searches), 0LL);                           \
        atomic_store(&((lfht_ptr)->sentinels_traversed_in_val_searches), 0LL);                              \
                                                                                                            \
        atomic_store(&((lfht_ptr)->itter_inits), 0LL);                                                      \
        atomic_store(&((lfht_ptr)->itter_nexts), 0LL);                                                      \
        atomic_store(&((lfht_ptr)->itter_ends), 0LL);                                                       \
        atomic_store(&((lfht_ptr)->marked_nodes_visited_in_itters), 0LL);                                   \
        atomic_store(&((lfht_ptr)->unmarked_nodes_visited_in_itters), 0LL);                                 \
        atomic_store(&((lfht_ptr)->sentinels_traversed_in_itters), 0LL);                                    \
                                                                                                            \
    } while( 0 )  /* LFHT_CLEAR_STATS() */

/************************************************************************
 *
 * LFHT_CREATE_HASH_BUCKET()
 *
 *     Create a hash bucket for the supplied hash and number of hash
 *     bucket table index bits.
 *
 *     To do this, iteratively traverse the hierarchy of hash buckets,
 *     starting from the supplied index_bits and working down to ensure
 *     the necessary parent bucket exists before creating the new bucket
 *     in the LFSLL.
 *      
 *     Note that it is possible that the index bucket for the supplied
 *     hash but with index_bits minus 1 may not exist -- in which case
 *     it will be processed first by pushing it onto a stack. Further,
 *     it may be that hash buckets for the given hash and both index_bits
 *     and index_bits minus 1 are the same -- in which case there will
 *     will be nothing to do once the stack empties.
 *      
 *
 *                                          JRM -- 6/30/23
 * 
 *      Changes:
 * 
 *      Converted to a macro. Changed from recursive implementation to 
 *      an iterative one due to a lack of recursive support in C macros.
 *      
 *      Now, rather than making recursive calls to process the parent 
 *      buckets, the current version maintains a stack of index levels,
 *      ensuring buckets are created in the correct order. 
 * 
 *                                          AZO -- 2/13/25
 *      
 *
 ************************************************************************/

#define LFHT_CREATE_HASH_BUCKET(lfht_ptr, lfht_hash, index_bits)                                            \
    do{                                                                                                     \
        bool lfht_CHB__result;                                                                              \
        bool lfht_CHB__add_success;                                                                         \
        int lfht_CHB__stack_top = 0;                                                                        \
        unsigned long long int lfht_CHB__target_index, lfht_CHB__target_hash,                               \
                               lfht_CHB__parent_index, lfht_CHB__parent_hash;                               \
        struct lfht_node_t * lfht_CHB__bucket_head_ptr;                                                     \
        struct lfht_node_t * lfht_CHB__sentinel_ptr = NULL;                                                 \
        struct lfht_node_t * lfht_CHB__null_ptr = NULL;                                                     \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == lfht_ptr->tag);                                                                \
        assert(index_bits > 0);                                                                             \
                                                                                                            \
        /* stack to hold the index_bits values for processing */                                            \
        int index_level_stack[index_bits];                                                                  \
                                                                                                            \
        /* push the initial index bits onto the stack */                                                    \
        index_level_stack[lfht_CHB__stack_top] = index_bits;                                                \
                                                                                                            \
        while( lfht_CHB__stack_top >= 0 ) {                                                                 \
                                                                                                            \
            /* peek at the current level of the stack */                                                    \
            index_bits = index_level_stack[lfht_CHB__stack_top];                                            \
                                                                                                            \
            LFHT_HASH_TO_IDX(lfht_hash, index_bits, &lfht_CHB__target_index);                               \
            LFHT_HASH_TO_IDX(lfht_hash, index_bits - 1, &lfht_CHB__parent_index);                           \
                                                                                                            \
            if( NULL == atomic_load(&(lfht_ptr->bucket_idx[lfht_CHB__target_index])) ) {                    \
                                                                                                            \
                if( NULL == atomic_load(&(lfht_ptr->bucket_idx[lfht_CHB__parent_index])) ) {                \
                    /*parent bucket doesn't exist either -- push it onto the stack*/                        \
                    lfht_CHB__stack_top++;                                                                  \
                    index_level_stack[lfht_CHB__stack_top] = index_bits - 1;                                \
                                                                                                            \
                    atomic_fetch_add(&(lfht_ptr->recursive_bucket_inits) , 1);                              \
                    /* revisit this level after processing the parent (move to the next iteration) */       \
                    continue;                                                                               \
                }                                                                                           \
                                                                                                            \
                lfht_CHB__bucket_head_ptr = atomic_load(&(lfht_ptr->bucket_idx[lfht_CHB__parent_index]));   \
                assert(NULL != lfht_CHB__bucket_head_ptr);                                                  \
                                                                                                            \
                /* it is possible that parent_index == target_index -- hence the following check */         \
                                                                                                            \
                if ( NULL == atomic_load(&(lfht_ptr->bucket_idx[lfht_CHB__target_index])) ) {               \
                                                                                                            \
                    LFHT_ID_TO_HASH(lfht_CHB__target_index, true, &lfht_CHB__target_hash);                  \
                    LFHT_ID_TO_HASH(lfht_CHB__target_hash >> 1, true, &lfht_CHB__parent_hash);              \
                                                                                                            \
                    assert(lfht_CHB__target_index == (lfht_CHB__parent_hash >> 1));                         \
                                                                                                            \
                    LFHT_ADD_INTERNAL(lfht_ptr, lfht_CHB__bucket_head_ptr, 0ULL, lfht_CHB__target_hash,     \
                                      true, NULL, &lfht_CHB__sentinel_ptr, &lfht_CHB__add_success);         \
                    if ( lfht_CHB__add_success ) {                                                          \
                                                                                                            \
                        /* creation of the sentinel node for the hash bucket succeeded. */                  \
                        /* now store a pointer to the new node in the bucket index.     */                  \
                                                                                                            \
                        assert(lfht_CHB__sentinel_ptr);                                                     \
                        assert(LFHT_VALID_NODE == (lfht_CHB__sentinel_ptr)->tag);                           \
                        assert(0x0ULL == (lfht_CHB__sentinel_ptr)->id);                                     \
                        assert(lfht_CHB__target_hash == (lfht_CHB__sentinel_ptr)->hash);                    \
                        assert((lfht_CHB__sentinel_ptr)->sentinel);                                         \
                        assert(NULL == atomic_load(&((lfht_CHB__sentinel_ptr)->value)));                    \
                                                                                                            \
                        /* set lfht_ptr->bucket_idx[target_index].                              */          \
                        /* Do this via atomic_compare_exchange_strong().                        */          \
                        /* assert that this succeeds, as it should be impossible for it to fail */          \
                                                                                                            \
                        lfht_CHB__result = atomic_compare_exchange_strong(                                  \
                            &((lfht_ptr)->bucket_idx[lfht_CHB__target_index]),                              \
                            &lfht_CHB__null_ptr, lfht_CHB__sentinel_ptr);                                   \
                                                                                                            \
                        assert(lfht_CHB__result);                                                           \
                                                                                                            \
                        atomic_fetch_add(&((lfht_ptr)->buckets_initialized), 1);                            \
                                                                                                            \
                    } else {                                                                                \
                                                                                                            \
                        /* the attempt to insert the new sentinel node failed -- which means that */        \
                        /* that the node already exists.  Thus if it hasn't been set already,     */        \
                        /* lfht_ptr->bucket_idx[target_index] will be set to point to the new     */        \
                        /* sentinel shortly.                                                      */        \
                                                                                                            \
                        atomic_fetch_add(&((lfht_ptr)->bucket_init_cols), 1);                               \
                                                                                                            \
                        while ( NULL == atomic_load(&((lfht_ptr)->bucket_idx[lfht_CHB__target_index])) ) {  \
                                                                                                            \
                            /* need to do better than this.  Want to call pthread_yield(), */               \
                            /* but that call doesn't seem to be supported anymore.         */               \
                            sleep(1);                                                                       \
                                                                                                            \
                            atomic_fetch_add(&((lfht_ptr)->bucket_init_col_sleeps), 1);                     \
                        }                                                                                   \
                    }                                                                                       \
                }                                                                                           \
                                                                                                            \
            } else{                                                                                         \
                                                                                                            \
                /* Another thread beat us to defining the new bucket.                    */                 \
                /* */                                                                                       \
                /* As there is nothing to back out of, I don't think this qualifies as a */                 \
                /* a collision -- hence no stats for this case.                          */                 \
                                                                                                            \
            }                                                                                               \
            lfht_CHB__stack_top--;                                                                          \
                                                                                                            \
        }                                                                                                   \
                                                                                                            \
    } while( 0 ) /* LFHT_CREATE_HASH_BUCKET*/

/************************************************************************
 *
 * LFHT_CREATE_NODE()
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
 *     Changes:
 *      
 *     Converted from function to macro - added an output parameter variable
 *     for passing the return value when invoked. 
 * 
 *                                          AZO -- 2/13/25
 *
 ************************************************************************/

#define LFHT_CREATE_NODE(lfht_ptr, lfht_node_id, lfht_create_node_hash, lfht_sentinel,                      \
                         lfht_node_value, lfht_create_node_ptr)                                             \
    do{                                                                                                     \
        bool lfht_CN__fl_search_done = false;                                                               \
        struct lfht_node_t * lfht_CN__create_node_ptr = NULL;                                               \
        struct lfht_fl_node_t * lfht_CN__fl_node_ptr = NULL;                                                \
        bool lfht_CN__result;                                                                               \
        struct lfht_flsptr_t lfht_CN__sfirst;                                                               \
        struct lfht_flsptr_t lfht_CN__new_sfirst;                                                           \
        struct lfht_flsptr_t lfht_CN__test_sfirst;                                                          \
        struct lfht_flsptr_t lfht_CN__slast;                                                                \
        struct lfht_flsptr_t lfht_CN__new_slast;                                                            \
        struct lfht_flsptr_t lfht_CN__snext;                                                                \
        struct lfht_flsptr_t lfht_CN__new_snext;                                                            \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
                                                                                                            \
        if ( lfht_create_node_hash > LFHT__MAX_HASH ) {                                                     \
                                                                                                            \
            fprintf(stderr, "hash = 0x%llx, LFHT__MAX_HASH = 0x%llx\n",                                     \
                    lfht_create_node_hash, LFHT__MAX_HASH);                                                 \
        }                                                                                                   \
        assert(lfht_create_node_hash <= LFHT__MAX_HASH);                                                    \
                                                                                                            \
        lfht_CN__sfirst = atomic_load(&((lfht_ptr)->fl_shead));                                             \
        if ( NULL == lfht_CN__sfirst.ptr ) {                                                                \
                                                                                                            \
            /* The free list hasn't been initialized yet, so skip */                                        \
            /* the search of the free list.                       */                                        \
                                                                                                            \
        lfht_CN__fl_search_done = true;                                                                     \
        }                                                                                                   \
                                                                                                            \
        while ( ! lfht_CN__fl_search_done ) {                                                               \
                                                                                                            \
            lfht_CN__sfirst = atomic_load(&((lfht_ptr)->fl_shead));                                         \
            lfht_CN__slast = atomic_load(&((lfht_ptr)->fl_stail));                                          \
                                                                                                            \
            assert(lfht_CN__sfirst.ptr);                                                                    \
            assert(lfht_CN__slast.ptr);                                                                     \
                                                                                                            \
            lfht_CN__snext = atomic_load(&((lfht_CN__sfirst.ptr)->snext));                                  \
                                                                                                            \
            lfht_CN__test_sfirst = atomic_load(&((lfht_ptr)->fl_shead));                                    \
            if ( ( lfht_CN__test_sfirst.ptr == lfht_CN__sfirst.ptr ) &&                                     \
                 ( lfht_CN__test_sfirst.sn == lfht_CN__sfirst.sn ) ) {                                      \
                                                                                                            \
                if ( lfht_CN__sfirst.ptr == lfht_CN__slast.ptr ) {                                          \
                                                                                                            \
                    if ( NULL == lfht_CN__snext.ptr ) {                                                     \
                                                                                                            \
                        /* The free list is empty */                                                        \
                        atomic_fetch_add(&((lfht_ptr)->num_fl_req_denied_due_to_empty), 1);                 \
                        lfht_CN__fl_search_done = true;                                                     \
                        break;                                                                              \
                                                                                                            \
                    }                                                                                       \
                                                                                                            \
                    /* Attempt to set lfht_ptr->fl_tail to next. It doesn't */                              \
                    /* matter whether we succeed or fail, as if we fail, it */                              \
                    /* just means that some other thread beat us to it.     */                              \
                    /* */                                                                                   \
                    /* that said, it doesn't hurt to collect stats          */                              \
                                                                                                            \
                    lfht_CN__new_slast.ptr = lfht_CN__snext.ptr;                                            \
                    lfht_CN__new_slast.sn  = lfht_CN__slast.sn + 1;                                         \
                    if ( ! atomic_compare_exchange_strong(&((lfht_ptr)->fl_stail),                          \
                                                          &lfht_CN__slast, lfht_CN__new_slast) ) {          \
                                                                                                            \
                        atomic_fetch_add(&((lfht_ptr)->num_fl_tail_update_cols), 1);                        \
                    }                                                                                       \
                } else {                                                                                    \
                                                                                                            \
                    /* set up new_sfirst now in case we need it later.  */                                  \
                    assert(lfht_CN__snext.ptr);                                                             \
                    lfht_CN__new_sfirst.ptr = lfht_CN__snext.ptr;                                           \
                    lfht_CN__new_sfirst.sn  = lfht_CN__sfirst.sn + 1;                                       \
                                                                                                            \
                    if ( atomic_load(&((lfht_CN__sfirst.ptr)->ref_count)) > 0 ) {                           \
                                                                                                            \
                        /* The ref count on the entry at the head of the free list */                       \
                        /* has a positive ref count, which means that there may be */                       \
                        /* a pointer to it somewhere.  Rather than take the risk,  */                       \
                        /* let it sit on the free list until the ref count drops   */                       \
                        /* to zero.                                                */                       \
                                                                                                            \
                        atomic_fetch_add(&((lfht_ptr)->num_fl_req_denied_due_to_ref_count), 1);             \
                        lfht_CN__fl_search_done = true;                                                     \
                                                                                                            \
                    } else if ( ! atomic_compare_exchange_strong(&((lfht_ptr)->fl_shead),                   \
                                                                 &lfht_CN__sfirst, lfht_CN__new_sfirst) ) { \
                                                                                                            \
                        /* the attempt to remove the first item from the free list */                       \
                        /* failed. Update stats and try again.                     */                       \
                                                                                                            \
                        atomic_fetch_add(&((lfht_ptr)->num_fl_head_update_cols), 1);                        \
                                                                                                            \
                    } else {                                                                                \
                                                                                                            \
                        /* First has been removed from the free list.  Set lfht_CN__fl_node_ptr to first, */\
                        /* update stats, and exit the loop by setting fl_search_done to true.    */         \
                                                                                                            \
                        lfht_CN__fl_node_ptr = lfht_CN__sfirst.ptr;                                         \
                                                                                                            \
                        atomic_store(&((lfht_CN__fl_node_ptr)->tag), LFHT_FL_NODE_IN_USE);                  \
                        assert( 0x0ULL == atomic_load(&((lfht_CN__fl_node_ptr)->ref_count)));               \
                                                                                                            \
                        lfht_CN__new_snext.ptr = NULL;                                                      \
                        lfht_CN__new_snext.sn  = lfht_CN__snext.sn + 1;                                     \
                                                                                                            \
                        lfht_CN__result = atomic_compare_exchange_strong(&((lfht_CN__fl_node_ptr)->snext),  \
                                                                &lfht_CN__snext, lfht_CN__new_snext);       \
                        assert(lfht_CN__result);                                                            \
                                                                                                            \
                        lfht_CN__create_node_ptr = (struct lfht_node_t *)lfht_CN__fl_node_ptr;              \
                                                                                                            \
                        assert(lfht_CN__create_node_ptr);                                                   \
                                                                                                            \
                        (lfht_CN__create_node_ptr)->tag = LFHT_VALID_NODE;                                  \
                        atomic_store(&((lfht_CN__create_node_ptr)->next), NULL);                            \
                        (lfht_CN__create_node_ptr)->id = lfht_node_id;                                      \
                        (lfht_CN__create_node_ptr)->hash = lfht_create_node_hash;                           \
                        (lfht_CN__create_node_ptr)->sentinel = lfht_sentinel;                               \
                        atomic_store(&((lfht_CN__create_node_ptr)->value), lfht_node_value);                \
                                                                                                            \
                        atomic_fetch_sub(&((lfht_ptr)->fl_len), 1);                                         \
                        atomic_fetch_add(&((lfht_ptr)->num_nodes_drawn_from_fl), 1);                        \
                                                                                                            \
                        lfht_CN__fl_search_done = true;                                                     \
                    }                                                                                       \
                }                                                                                           \
            }                                                                                               \
        } /* while ( ! lfht_CN__fl_search_done ) */                                                         \
                                                                                                            \
        if ( NULL == lfht_CN__fl_node_ptr ) {                                                               \
                                                                                                            \
            lfht_CN__fl_node_ptr = (struct lfht_fl_node_t *)malloc(sizeof(struct lfht_fl_node_t));          \
                                                                                                            \
            assert(lfht_CN__fl_node_ptr);                                                                   \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->num_nodes_allocated), 1);                                        \
                                                                                                            \
            atomic_init(&((lfht_CN__fl_node_ptr)->tag), LFHT_FL_NODE_IN_USE);                               \
            atomic_init(&((lfht_CN__fl_node_ptr)->ref_count), 0);                                           \
            atomic_init(&((lfht_CN__fl_node_ptr)->sn), 0ULL);                                               \
                                                                                                            \
            lfht_CN__snext.ptr = NULL;                                                                      \
            lfht_CN__snext.sn  = 0ULL;                                                                      \
            atomic_init(&((lfht_CN__fl_node_ptr)->snext), lfht_CN__snext);                                  \
                                                                                                            \
            lfht_CN__create_node_ptr = (struct lfht_node_t *)lfht_CN__fl_node_ptr;                          \
                                                                                                            \
            assert(lfht_CN__create_node_ptr);                                                               \
                                                                                                            \
            (lfht_CN__create_node_ptr)->tag = LFHT_VALID_NODE;                                              \
            atomic_init(&((lfht_CN__create_node_ptr)->next), NULL);                                         \
            (lfht_CN__create_node_ptr)->id = lfht_node_id;                                                  \
            (lfht_CN__create_node_ptr)->hash = lfht_create_node_hash;                                       \
            (lfht_CN__create_node_ptr)->sentinel = lfht_sentinel;                                           \
            atomic_init(&((lfht_CN__create_node_ptr)->value), lfht_node_value);                             \
        }                                                                                                   \
                                                                                                            \
        assert(lfht_CN__fl_node_ptr);                                                                       \
                                                                                                            \
        *(lfht_create_node_ptr) = lfht_CN__create_node_ptr;                                                 \
                                                                                                            \
    } while( 0 ) /* LFHT_CREATE_NODE() */


/************************************************************************
 *
 * LFHT_DELETE()
 *
 * Attempt to find the target node in the lfht.
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
 *    Converted from a function to a macro. Added an output parameter variable 
 *    in order to return the boolean value the macro evaluates to. 
 *    Removed a conditional compile for adding a bit mask so that
 *    it is applied in all cases.
 *    
 * 
 *                                           AZO -- 2/18/25
 *
 ************************************************************************/

#define LFHT_DELETE(lfht_ptr, lfht_del__id, lfht_del_succeeded)                                             \
    do {                                                                                                    \
        bool lfht_del__done = false;                                                                        \
        bool lfht_del__success = false;                                                                     \
        int lfht_del__del_completions = 0;                                                                  \
        int lfht_del__del_completion_cols = 0;                                                              \
        int lfht_del__del_init_cols = 0;                                                                    \
        int lfht_del__del_retries = 0;                                                                      \
        int lfht_del__nodes_visited = 0;                                                                    \
        unsigned long long int lfht_del__hash;                                                              \
        struct lfht_node_t * lfht_del__bucket_head_ptr;                                                     \
        struct lfht_node_t * lfht_del__first_node_ptr;                                                      \
        struct lfht_node_t * lfht_del__second_node_ptr;                                                     \
        struct lfht_node_t * lfht_del__marked_second_node_ptr = NULL;                                       \
        struct lfht_fl_node_t * lfht_del__fl_node_ptr;                                                      \
                                                                                                            \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
                                                                                                            \
                                                                                                            \
        assert((lfht_del__id & LFHT_ID_BIT_MASK) <= LFHT__MAX_ID);                                          \
                                                                                                            \
                                                                                                            \
        LFHT_ENTER(lfht_ptr, &lfht_del__fl_node_ptr);                                                       \
                                                                                                            \
                                                                                                            \
                                                                                                            \
        LFHT_ID_TO_HASH((lfht_del__id & LFHT_ID_BIT_MASK), false, &lfht_del__hash);                         \
                                                                                                            \
                                                                                                            \
        LFHT_GET_HASH_BUCKET_SENTINEL(lfht_ptr, lfht_del__hash, &lfht_del__bucket_head_ptr);                \
                                                                                                            \
        do {                                                                                                \
            lfht_del__first_node_ptr = NULL;                                                                \
            lfht_del__second_node_ptr = NULL;                                                               \
            /* attempt to find the target */                                                                \
                                                                                                            \
            /* in its current implementation, LFHT_FIND_MOD_POINT() will    */                              \
            /* either succeed or trigger an assertion -- thus no need to     */                             \
            /* check return value at present.                                */                             \
                                                                                                            \
            LFHT_FIND_MOD_POINT(lfht_ptr,                                                                   \
                                lfht_del__bucket_head_ptr,                                                  \
                                &lfht_del__first_node_ptr,                                                  \
                                &lfht_del__second_node_ptr,                                                 \
                                &lfht_del__del_completion_cols,                                             \
                                &lfht_del__del_completions,                                                 \
                                &lfht_del__nodes_visited,                                                   \
                                lfht_del__hash);                                                            \
                                                                                                            \
            assert(lfht_del__first_node_ptr);                                                               \
                                                                                                            \
            if ( lfht_del__hash == (lfht_del__first_node_ptr)->hash ) {                                     \
                                                                                                            \
                assert(!(lfht_del__first_node_ptr)->sentinel);                                              \
                assert(lfht_del__id == (lfht_del__first_node_ptr)->id);                                     \
                                                                                                            \
                /* Hash exists in the SLL.  Attempt to mark the                                         */  \
                /* node for deletion.  If we fail, that means that either:                              */  \
                /**/                                                                                        \
                /* 1. another thread has beat us to marking *lfht_del__first_node_ptr as deleted.       */  \
                /**/                                                                                        \
                /* 2. another thread has either inserted a new node just after *lfht_del__first_node_ptr*/  \
                /*    or physically deleted *lfht_del__second_node_ptr.                                 */  \
                /**/                                                                                        \
                /* No worries if the former, but in latter case, we must try again.                     */  \
                                                                                                            \
                lfht_del__marked_second_node_ptr = (struct lfht_node_t *)                                   \
                                        (((unsigned long long)(lfht_del__second_node_ptr)) | 0x01ULL);      \
                                                                                                            \
                if ( atomic_compare_exchange_strong(&((lfht_del__first_node_ptr)->next),                    \
                                                    &lfht_del__second_node_ptr,                             \
                                                    lfht_del__marked_second_node_ptr) ) {                   \
                                                                                                            \
                    /* decrement the logical lfsll length.  We will decrement the physical list */          \
                    /* length when the node is physically deleted from the list.                */          \
                                                                                                            \
                    atomic_fetch_sub(&((lfht_ptr)->lfsll_log_len), 1);                                      \
                                                                                                            \
                    lfht_del__success = true;                                                               \
                    lfht_del__done = true;                                                                  \
                                                                                                            \
                } else if ( 0 != (((unsigned long long)(lfht_del__second_node_ptr)) & 0x01ULL) ) {          \
                                                                                                            \
                    /* recall that atomic_compare_exchamge_strong replaces the expected value          */   \
                    /* with the actual value on failure.  If the low order bit is set, we are          */   \
                    /* in case 1 above -- another thread beat us to marking *lfht_del__first_node_ptr  */   \
                    /* as deleted.                                                                     */   \
                                                                                                            \
                    lfht_del__success = true;                                                               \
                    lfht_del__done = true;                                                                  \
                    lfht_del__del_init_cols++;                                                              \
                                                                                                            \
                } else {                                                                                    \
                                                                                                            \
                    /* a node has been added or deleted just after *first_node_ptr.  Must */                \
                    /* retry the deletion.                                                */                \
                                                                                                            \
                    lfht_del__del_retries++;                                                                \
                }                                                                                           \
            } else {                                                                                        \
                                                                                                            \
                /* target not in lfht */                                                                    \
                                                                                                            \
                lfht_del__success = false;                                                                  \
                lfht_del__done = true;                                                                      \
            }                                                                                               \
        }                                                                                                   \
        while ( ! lfht_del__done );                                                                         \
                                                                                                            \
        /* update statistics */                                                                             \
                                                                                                            \
        assert(lfht_del__del_init_cols >= 0);                                                               \
        assert(lfht_del__del_completion_cols >= 0);                                                         \
        assert(lfht_del__del_completions >= 0);                                                             \
        assert(lfht_del__nodes_visited >= 0);                                                               \
        assert(lfht_del__del_retries >= 0);                                                                 \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->deletion_attempts), 1);                                              \
                                                                                                            \
        if ( lfht_del__success ) {                                                                          \
                                                                                                            \
            if ( lfht_del__del_init_cols == 0 ) {                                                           \
                                                                                                            \
                atomic_fetch_add(&((lfht_ptr)->deletion_starts), 1);                                        \
                                                                                                            \
            } else {                                                                                        \
                                                                                                            \
                atomic_fetch_add(&((lfht_ptr)->deletion_start_cols), 1);                                    \
        }                                                                                                   \
        } else {                                                                                            \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->deletion_failures), 1);                                          \
        }                                                                                                   \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->del_retries),                 (long long)lfht_del__del_retries);     \
        atomic_fetch_add(&((lfht_ptr)->del_restarts_due_to_del_col),                                        \
                         (long long)lfht_del__del_completion_cols);                                         \
        atomic_fetch_add(&((lfht_ptr)->del_deletion_completions),    (long long)lfht_del__del_completions); \
        atomic_fetch_add(&((lfht_ptr)->nodes_visited_during_dels),   (long long)lfht_del__nodes_visited);   \
                                                                                                            \
        LFHT_EXIT(lfht_ptr, lfht_del__fl_node_ptr);                                                         \
                                                                                                            \
        *(lfht_del_succeeded) = lfht_del__success;                                                          \
                                                                                                            \
    }while( 0 ) /* LFHT_DELETE() */

/************************************************************************
 *
 * LFHT_DISCARD_NODE()
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
 *      Changes:
 *      
 *      Converted from a function to a macro.
 * 
 *                                         Anna Burton -- 2/14/25
 *
 ************************************************************************/

#define LFHT_DISCARD_NODE(lfht_ptr, node_ptr, expected_ref_count)                                           \
    do{                                                                                                     \
        bool lfht_DN__done = false;                                                                         \
        bool lfht_DN__result;                                                                               \
        unsigned int lfht_DN__in_use_tag = LFHT_FL_NODE_IN_USE;                                             \
        long long int lfht_DN__fl_len;                                                                      \
        long long int lfht_DN__max_fl_len;                                                                  \
        struct lfht_node_t * lfht_DN__next;                                                                 \
        struct lfht_fl_node_t * lfht_DN__fl_node_ptr;                                                       \
        struct lfht_flsptr_t lfht_DN__snext = {NULL, 0ULL};                                                 \
        struct lfht_flsptr_t lfht_DN__fl_stail;                                                             \
        struct lfht_flsptr_t lfht_DN__fl_snext;                                                             \
        struct lfht_flsptr_t lfht_DN__new_fl_snext;                                                         \
        struct lfht_flsptr_t lfht_DN__test_fl_stail;                                                        \
        struct lfht_flsptr_t lfht_DN__new_fl_stail;                                                         \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(node_ptr);                                                                                   \
        assert((node_ptr)->tag == LFHT_VALID_NODE);                                                         \
                                                                                                            \
        lfht_DN__next = atomic_load(&((node_ptr)->next));                                                   \
                                                                                                            \
        assert(0x01ULL == (((unsigned long long)(lfht_DN__next)) & 0x01ULL));                               \
                                                                                                            \
        lfht_DN__fl_node_ptr = (struct lfht_fl_node_t *)node_ptr;                                           \
                                                                                                            \
        assert(LFHT_FL_NODE_IN_USE == atomic_load(&((lfht_DN__fl_node_ptr)->tag)));                         \
        assert(expected_ref_count == atomic_load(&((lfht_DN__fl_node_ptr)->ref_count)));                    \
                                                                                                            \
        lfht_DN__snext = atomic_load(&((lfht_DN__fl_node_ptr)->snext));                                     \
        assert(NULL == lfht_DN__snext.ptr);                                                                 \
                                                                                                            \
        lfht_DN__result = atomic_compare_exchange_strong(&((lfht_DN__fl_node_ptr)->tag),                    \
                                                         &lfht_DN__in_use_tag, LFHT_FL_NODE_ON_FL);         \
        assert(lfht_DN__result);                                                                            \
                                                                                                            \
        atomic_store(&((lfht_DN__fl_node_ptr)->sn), atomic_fetch_add(&((lfht_ptr)->next_sn), 1));           \
                                                                                                            \
        while ( ! lfht_DN__done ) {                                                                         \
                                                                                                            \
            lfht_DN__fl_stail = atomic_load(&((lfht_ptr)->fl_stail));                                       \
                                                                                                            \
            assert(lfht_DN__fl_stail.ptr);                                                                  \
                                                                                                            \
            /* It is possible that *fl_tail.ptr has passed through the free list     */                     \
            /* and been re-allocated between the time we loaded it, and now.         */                     \
            /* If so, fl_stail_ptr->tag will no longer be LFHT_FL_NODE_ON_FL.        */                     \
            /* This isn't a problem, as if so, the following if statement will fail. */                     \
                                                                                                            \
            /* Assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_stail.ptr->tag)));      */                     \
                                                                                                            \
            lfht_DN__fl_snext = atomic_load(&((lfht_DN__fl_stail).ptr->snext));                             \
                                                                                                            \
            lfht_DN__test_fl_stail = atomic_load(&((lfht_ptr)->fl_stail));                                  \
                                                                                                            \
            if ( ( lfht_DN__test_fl_stail.ptr == lfht_DN__fl_stail.ptr ) &&                                 \
                 ( lfht_DN__test_fl_stail.sn == lfht_DN__fl_stail.sn ) ) {                                  \
                                                                                                            \
                if ( NULL == lfht_DN__fl_snext.ptr ) {                                                      \
                                                                                                            \
                    /* Attempt to append lfht_DN__fl_node_ptr by setting fl_tail->next to              */   \
                    /* lfht_DN__fl_node_ptr. If this succeeds, update stats and attempt to set         */   \
                    /* lfht_ptr->fl_tail to lfht_DN__fl_node_ptr as well.  This may or may not succeed,*/   \
                    /* but in either case we are lfht_DN__done.                                        */   \
                                                                                                            \
                    lfht_DN__new_fl_snext.ptr = lfht_DN__fl_node_ptr;                                       \
                    lfht_DN__new_fl_snext.sn  = lfht_DN__fl_snext.sn + 1;                                   \
                    if ( atomic_compare_exchange_strong(&((lfht_DN__fl_stail).ptr->snext),                  \
                                                        &lfht_DN__fl_snext, lfht_DN__new_fl_snext))         \
                        {                                                                                   \
                                                                                                            \
                        atomic_fetch_add(&((lfht_ptr)->fl_len), 1);                                         \
                        atomic_fetch_add(&((lfht_ptr)->num_nodes_added_to_fl), 1);                          \
                                                                                                            \
                        lfht_DN__new_fl_stail.ptr = lfht_DN__fl_node_ptr;                                   \
                        lfht_DN__new_fl_stail.sn  = lfht_DN__fl_stail.sn + 1;                               \
                        if (! atomic_compare_exchange_strong(&((lfht_ptr)->fl_stail),                       \
                                                             &lfht_DN__fl_stail, lfht_DN__new_fl_stail))    \
                        {                                                                                   \
                            atomic_fetch_add(&((lfht_ptr)->num_fl_tail_update_cols), 1);                    \
                        }                                                                                   \
                                                                                                            \
                        /* if appropriate, attempt to update lfht_ptr->lfht_DN__max_fl_len.  In the    */   \
                        /* event of a collision, just ignore it and go on, as I don't see any */            \
                        /* reasonable way to recover.                                         */            \
                                                                                                            \
                        if ( (lfht_DN__fl_len = atomic_load(&((lfht_ptr)->fl_len))) >                       \
                            (lfht_DN__max_fl_len = atomic_load(&((lfht_ptr)->max_fl_len))) ) {              \
                                                                                                            \
                            atomic_compare_exchange_strong(&((lfht_ptr)->max_fl_len),                       \
                                                            &lfht_DN__max_fl_len, lfht_DN__fl_len);         \
                        }                                                                                   \
                                                                                                            \
                        lfht_DN__done = true;                                                               \
                                                                                                            \
                    } else {                                                                                \
                                                                                                            \
                        /* append failed -- update stats and try again */                                   \
                        atomic_fetch_add(&((lfht_ptr)->num_fl_append_cols), 1);                             \
                                                                                                            \
                    }                                                                                       \
                } else {                                                                                    \
                                                                                                            \
                    /* assert(LFHT_FL_NODE_ON_FL == atomic_load(&(fl_next->tag))); */                       \
                                                                                                            \
                    /* Attempt to set lfht_ptr->fl_stail to fl_next.  It doesn't */                         \
                    /* matter whether we succeed or fail, as if we fail, it      */                         \
                    /* just means that some other thread beat us to it.          */                         \
                                                                                                            \
                    /* That said, it doesn't hurt to collect stats.              */                         \
                                                                                                            \
                    lfht_DN__new_fl_stail.ptr = lfht_DN__fl_snext.ptr;                                      \
                    lfht_DN__new_fl_stail.sn  = lfht_DN__fl_stail.sn + 1;                                   \
                    if ( ! atomic_compare_exchange_strong(&((lfht_ptr)->fl_stail),                          \
                                                          &lfht_DN__fl_stail, lfht_DN__new_fl_stail) ) {    \
                                                                                                            \
                        atomic_fetch_add(&((lfht_ptr)->num_fl_tail_update_cols), 1);                        \
                    }                                                                                       \
                }                                                                                           \
            }                                                                                               \
        }                                                                                                   \
    } while( 0 ) /* LFHT_DISCARD_NODE */


/************************************************************************
 *
 * LFHT_DUMP_LIST()
 *
 *     Print the contents of the lfht_t to the supplied file.  For now
 *     this means displaying the contents of the LFSLL in the lock free
 *     hash table.
 *
 *
 *                                          JRM -- 6/14/23
 * 
 *     Changes:
 * 
 *     Converted from a function to a macro. 
 *          
 *                                          Anna Burton -- 2/14/25
 *
 ************************************************************************/

#define LFHT_DUMP_LIST(lfht_ptr, file_ptr)                                                                  \
    do{                                                                                                     \
        long long int lfht_DL__node_num = 0;                                                                \
        struct lfht_node_t * lfht_DL__node_ptr;                                                             \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(file_ptr);                                                                                   \
                                                                                                            \
        fprintf(file_ptr, "\n\n***** CONTENTS OF LFSLL IN THE LFHT *****\n");                               \
                                                                                                            \
        fprintf(file_ptr, "\nLFSLL Logical / Physical Length = %lld/%lld, Free List Len = %lld.\n\n",       \
                atomic_load(&((lfht_ptr)->lfsll_log_len)), atomic_load(&((lfht_ptr)->lfsll_phys_len)),      \
                atomic_load(&((lfht_ptr)->fl_len)));                                                        \
                                                                                                            \
        lfht_DL__node_ptr = atomic_load(&((lfht_ptr)->lfsll_root));                                         \
                                                                                                            \
        while ( lfht_DL__node_ptr ) {                                                                       \
                                                                                                            \
            fprintf(file_ptr,                                                                               \
                    "Node num = %lld, marked = %lld, sentinel = %d,"                                        \
                    "id = 0x%lld, hash = 0x%llx, value = 0x%llx\n",                                         \
                    lfht_DL__node_num++, (((unsigned long long)(atomic_load(&((lfht_DL__node_ptr)->next)))) \
                    & 0x01ULL), (int)((lfht_DL__node_ptr)->sentinel), (lfht_DL__node_ptr)->id,              \
                    (lfht_DL__node_ptr)->hash,                                                              \
                    (unsigned long long)atomic_load(&((lfht_DL__node_ptr)->value)));                        \
                                                                                                            \
            lfht_DL__node_ptr = atomic_load(&((lfht_DL__node_ptr)->next));                                  \
                                                                                                            \
            /* Clear the low order bit of node ptr whether it is set or not. */                             \
            lfht_DL__node_ptr = (struct lfht_node_t *)(((unsigned long long)(lfht_DL__node_ptr)) &          \
                                                        (~0x01ULL));                                        \
        }                                                                                                   \
                                                                                                            \
        fprintf(file_ptr, "\n***** END LFHT CONTENTS *****\n\n");                                           \
                                                                                                            \
    } while( 0 ) /* LFHT_DUMP_LIST */


/************************************************************************
 *
 * LFHT_DUMP_STATS()
 *
 *     Print the contents of the statistics fields of the supplied
 *     intance of lfht_t to the supplied file.
 *
 *
 *                                          JRM -- 6/14/23
 * 
 *     Changes: 
 *          
 *     Converted from a function to a macro.
 * 
 *                                          Anna Burton -- 2/13/25
 *
 ************************************************************************/

#define LFHT_DUMP_STATS(lfht_ptr, file_ptr)                                                                 \
    do{                                                                                                     \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(file_ptr);                                                                                   \
                                                                                                            \
        fprintf(file_ptr, "\n\n***** LFSLL STATS *****\n");                                                 \
                                                                                                            \
        fprintf(file_ptr, "\nCurrent logical / physical LFSLL length = %lld / %lld \n",                     \
                atomic_load(&((lfht_ptr)->lfsll_log_len)), atomic_load(&((lfht_ptr)->lfsll_phys_len)));     \
        fprintf(file_ptr, "Max logical / physical LFSLL length = %lld / %lld\n",                            \
                atomic_load(&((lfht_ptr)->max_lfsll_log_len)),                                              \
                atomic_load(&((lfht_ptr)->max_lfsll_phys_len)));                                            \
                                                                                                            \
        fprintf(file_ptr, "\nFree List:\n");                                                                \
        fprintf(file_ptr,                                                                                   \
                "Max / current FL Length = %lld /%lld, Nodes added / deleted from free list = %lld / %lld\n",\
                atomic_load(&((lfht_ptr)->max_fl_len)),                                                     \
                atomic_load(&((lfht_ptr)->fl_len)),                                                         \
                atomic_load(&((lfht_ptr)->num_nodes_added_to_fl)),                                          \
                atomic_load(&((lfht_ptr)->num_nodes_drawn_from_fl)));                                       \
        fprintf(file_ptr, "FL head / tail / append cols = %lld / %lld / %lld.\n",                           \
                atomic_load(&((lfht_ptr)->num_fl_head_update_cols)),                                        \
                atomic_load(&((lfht_ptr)->num_fl_tail_update_cols)),                                        \
                atomic_load(&((lfht_ptr)->num_fl_append_cols)));                                            \
        fprintf(file_ptr, "FL reqs failed due to empty / ref count = %lld / %lld.\n",                       \
                atomic_load(&((lfht_ptr)->num_fl_req_denied_due_to_empty)),                                 \
                atomic_load(&((lfht_ptr)->num_fl_req_denied_due_to_ref_count)));                            \
        fprintf(file_ptr, "FL node ref count inc / decs = %lld / %lld, ref count inc retrys = %lld.\n",     \
                atomic_load(&((lfht_ptr)->num_fl_node_ref_cnt_incs)),                                       \
                atomic_load(&((lfht_ptr)->num_fl_node_ref_cnt_decs)),                                       \
                atomic_load(&((lfht_ptr)->num_fl_node_ref_cnt_inc_retrys)));                                \
        fprintf(file_ptr,                                                                                   \
                "Nodes allocated / freed = %lld / %lld, candidate selection for free retries = %lld\n",     \
                atomic_load(&((lfht_ptr)->num_nodes_allocated)),                                            \
                atomic_load(&((lfht_ptr)->num_nodes_freed)),                                                \
                atomic_load(&((lfht_ptr)->num_node_free_candidate_selection_restarts)));                    \
        fprintf(file_ptr, "Frees skiped due to empty / ref_count = %lld / %lld.\n",                         \
                atomic_load(&((lfht_ptr)->num_fl_frees_skiped_due_to_empty)),                               \
                atomic_load(&((lfht_ptr)->num_fl_frees_skiped_due_to_ref_count)));                          \
                                                                                                            \
        fprintf(file_ptr, "\nHash Buckets:\n");                                                             \
        fprintf(file_ptr,                                                                                   \
                "Hash buckets defined / initialized = %lld / %lld, index_bits = %d, max index_bits = %d\n", \
                atomic_load(&((lfht_ptr)->buckets_defined)),                                                \
                atomic_load(&((lfht_ptr)->buckets_initialized)),                                            \
                atomic_load(&((lfht_ptr)->index_bits)),                                                     \
                (lfht_ptr)->max_index_bits);                                                                \
        fprintf(file_ptr, "Index bits incr cols = %lld, buckets defined update cols / retries = %lld / %lld.\n",\
                atomic_load(&((lfht_ptr)->index_bits_incr_cols)),                                           \
                atomic_load(&((lfht_ptr)->buckets_defined_update_cols)),                                    \
                atomic_load(&((lfht_ptr)->buckets_defined_update_retries)));                                \
        fprintf(file_ptr, "Hash bucket init cols / col sleeps = %lld / %lld\n",                             \
                atomic_load(&((lfht_ptr)->bucket_init_cols)),                                               \
                atomic_load(&((lfht_ptr)->bucket_init_col_sleeps)));                                        \
        fprintf(file_ptr, "recursive bucket inits = %lld, sentinels traversed = %lld.\n",                   \
                atomic_load(&((lfht_ptr)->recursive_bucket_inits)),                                         \
                atomic_load(&((lfht_ptr)->sentinels_traversed)));                                           \
                                                                                                            \
        fprintf(file_ptr, "\nInsertions:\n");                                                               \
        fprintf(file_ptr, "successful / failed = %lld/%lld, ins / del cols = %lld/%lld\n",                  \
                atomic_load(&((lfht_ptr)->insertions)), atomic_load(&((lfht_ptr)->insertion_failures)),     \
                atomic_load(&((lfht_ptr)->ins_restarts_due_to_ins_col)),                                    \
                atomic_load(&((lfht_ptr)->ins_restarts_due_to_del_col)));                                   \
        fprintf(file_ptr, "del completions = %lld, nodes visited = %lld\n",                                 \
                atomic_load(&((lfht_ptr)->ins_deletion_completions)),                                       \
                atomic_load(&((lfht_ptr)->nodes_visited_during_ins)));                                      \
                                                                                                            \
        fprintf(file_ptr, "\nDeletions:\n");                                                                \
        fprintf(file_ptr, "attempted / failed = %lld/%lld, starts / start cols = %lld/%lld, retries = %lld\n",\
                atomic_load(&((lfht_ptr)->deletion_attempts)),                                              \
                atomic_load(&((lfht_ptr)->deletion_failures)),                                              \
                atomic_load(&((lfht_ptr)->deletion_starts)),                                                \
                atomic_load(&((lfht_ptr)->deletion_start_cols)),                                            \
                atomic_load(&((lfht_ptr)->del_retries)));                                                   \
        fprintf(file_ptr, "del completions = %lld, del col restarts = %lld, nodes visited = %lld\n",        \
                atomic_load(&((lfht_ptr)->del_deletion_completions)),                                       \
                atomic_load(&((lfht_ptr)->del_restarts_due_to_del_col)),                                    \
                atomic_load(&((lfht_ptr)->nodes_visited_during_dels)));                                     \
                                                                                                            \
        fprintf(file_ptr, "\nSearches:\n");                                                                 \
        fprintf(file_ptr, "attempted / successful / failed = %lld/%lld/%lld\n",                             \
                atomic_load(&((lfht_ptr)->searches)), atomic_load(&((lfht_ptr)->successful_searches)),      \
                atomic_load(&((lfht_ptr)->failed_searches)));                                               \
        fprintf(file_ptr,                                                                                   \
                "marked/unmoard nodes visited in: successful search %lld/%lld, failed search %lld/%lld\n",  \
                atomic_load(&((lfht_ptr)->marked_nodes_visited_in_succ_searches)),                          \
                atomic_load(&((lfht_ptr)->unmarked_nodes_visited_in_succ_searches)),                        \
                atomic_load(&((lfht_ptr)->marked_nodes_visited_in_failed_searches)),                        \
                atomic_load(&((lfht_ptr)->unmarked_nodes_visited_in_failed_searches)));                     \
                                                                                                            \
        if ( atomic_load(&((lfht_ptr)->value_swaps)) > 0LL ) {                                              \
                                                                                                            \
            fprintf(file_ptr, "\nValue Swaps:\n");                                                          \
            fprintf(file_ptr, "attempted / successful / failed = %lld/%lld/%lld\n",                         \
                    atomic_load(&((lfht_ptr)->value_swaps)),                                                \
                    atomic_load(&((lfht_ptr)->successful_val_swaps)),                                       \
                    atomic_load(&((lfht_ptr)->failed_val_swaps)));                                          \
            fprintf(file_ptr,                                                                               \
                "marked/unmoard nodes visited in: successful value swaps %lld/%lld,                         \
                 failed value swaps %lld/%lld\n",                                                           \
                    atomic_load(&((lfht_ptr)->marked_nodes_visited_in_succ_val_swaps)),                     \
                    atomic_load(&((lfht_ptr)->unmarked_nodes_visited_in_succ_val_swaps)),                   \
                    atomic_load(&((lfht_ptr)->marked_nodes_visited_in_failed_val_swaps)),                   \
                    atomic_load(&((lfht_ptr)->unmarked_nodes_visited_in_failed_val_swaps)));                \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            fprintf(file_ptr, "\nNo Value Swaps.\n");                                                       \
        }                                                                                                   \
                                                                                                            \
        if ( atomic_load(&((lfht_ptr)->value_searches)) > 0LL ) {                                           \
                                                                                                            \
            fprintf(file_ptr, "\nSearches by Value:\n");                                                    \
            fprintf(file_ptr, "attempted / successful / failed = %lld/%lld/%lld\n",                         \
                    atomic_load(&((lfht_ptr)->value_searches)),                                             \
                    atomic_load(&((lfht_ptr)->successful_val_searches)),                                    \
                    atomic_load(&((lfht_ptr)->failed_val_searches)));                                       \
            fprintf(file_ptr,                                                                               \
                    "marked/unmoard nodes visited in value searches %lld/%lld, sentinels traversed %lld\n", \
                    atomic_load(&((lfht_ptr)->marked_nodes_visited_in_val_searches)),                       \
                    atomic_load(&((lfht_ptr)->unmarked_nodes_visited_in_val_searches)),                     \
                    atomic_load(&((lfht_ptr)->sentinels_traversed_in_val_searches)));                       \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            fprintf(file_ptr, "\nNo Searches by Value.\n");                                                 \
        }                                                                                                   \
                                                                                                            \
        if ( atomic_load(&((lfht_ptr)->itter_inits)) > 0LL ) {                                              \
                                                                                                            \
            fprintf(file_ptr, "\nItterations:\n");                                                          \
            fprintf(file_ptr, "initiated / nexts / completed = %lld/%lld/%lld\n",                           \
                    atomic_load(&((lfht_ptr)->itter_inits)), atomic_load(&((lfht_ptr)->itter_nexts)),       \
                    atomic_load(&((lfht_ptr)->itter_ends)));                                                \
            fprintf(file_ptr,                                                                               \
                    "marked/unmoard nodes visited in itterations %lld/%lld, sentinels traversed %lld\n",    \
                    atomic_load(&((lfht_ptr)->marked_nodes_visited_in_itters)),                             \
                    atomic_load(&((lfht_ptr)->unmarked_nodes_visited_in_itters)),                           \
                    atomic_load(&((lfht_ptr)->sentinels_traversed_in_itters)));                             \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            fprintf(file_ptr, "\nNo Iterations Initiated.\n");                                              \
        }                                                                                                   \
                                                                                                            \
        fprintf(file_ptr, "\n***** END LFSLL STATS *****\n\n");                                             \
                                                                                                            \
    } while( 0 )  /* LFHT_DUMP_STATS() */              

/************************************************************************
 *
 * LFHT_ENTER()
 *
 * Function to be called on entry to any API call that touches the LFHT
 * data structures.
 *
 * At present, this function exists to insert an entry with refcount 1
 * at the end of the free list, or (if such a node already exists) to
 * increment its ref count.
 *
 * In either case, the pointer to the relevant node is returned to the
 * caller,  where it is then used by LFHT_EXIT() to decrement the same
 * ref_count.
 *
 * Geven the frequency with which this function will be called, it may
 * be useful to turn it into a macro.
 *
 *                                           JRM -- 6/30/23
 *
 * Changes:
 *
 *    Converted from a function to a macro. Added an output parameter variable 
 *    to pass the return value when invoked. 
 *    
 * 
 *                                          AZO -- 2/13/25
 *
 ************************************************************************/

#define LFHT_ENTER( lfht_ptr, lfht_enter_ptr )                                                              \
    do{                                                                                                     \
        bool lfht_enter__done = false;                                                                      \
        struct lfht_node_t * lfht_enter__node_ptr = NULL;                                                   \
        struct lfht_fl_node_t * lfht_enter__fl_node_ptr = NULL;                                             \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
                                                                                                            \
                                                                                                            \
        if ( ! lfht_enter__done ) {                                                                         \
                                                                                                            \
            LFHT_CREATE_NODE(lfht_ptr, 0ULL, 1ULL, false, NULL, &lfht_enter__node_ptr);                     \
                                                                                                            \
            assert(lfht_enter__node_ptr);                                                                   \
            assert(LFHT_VALID_NODE == (lfht_enter__node_ptr)->tag);                                         \
                                                                                                            \
            atomic_store(&((lfht_enter__node_ptr)->next), (struct lfht_node_t *)0x01ULL);                   \
                                                                                                            \
            lfht_enter__fl_node_ptr = (struct lfht_fl_node_t *)lfht_enter__node_ptr;                        \
                                                                                                            \
            assert(LFHT_FL_NODE_IN_USE == (lfht_enter__fl_node_ptr)->tag);                                  \
            assert(0ULL == atomic_load(&((lfht_enter__fl_node_ptr)->ref_count)));                           \
                                                                                                            \
            atomic_store(&((lfht_enter__fl_node_ptr)->ref_count), 1ULL);                                    \
                                                                                                            \
            LFHT_DISCARD_NODE(lfht_ptr, lfht_enter__node_ptr, 1);                                           \
                                                                                                            \
            lfht_enter__done = true;                                                                        \
        }                                                                                                   \
                                                                                                            \
        assert(lfht_enter__fl_node_ptr);                                                                    \
        assert(LFHT_FL_NODE_ON_FL == atomic_load(&((lfht_enter__fl_node_ptr)->tag)));                       \
        assert(atomic_load(&((lfht_enter__fl_node_ptr)->ref_count)) > 0);                                   \
                                                                                                            \
        *lfht_enter_ptr = lfht_enter__fl_node_ptr;                                                          \
                                                                                                            \
    } while( 0 ) /* LFHT_ENTER() */

/************************************************************************
 *
 * LFHT_EXIT()
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
 *    Converted from function to macro.
 *      
 *                                          Anna Burton -- 2/10/25
 *
 ************************************************************************/

#define LFHT_EXIT(lfht_ptr, fl_node_ptr)                                                                    \
    do{                                                                                                     \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(fl_node_ptr);                                                                                \
        assert(LFHT_FL_NODE_ON_FL == atomic_load(&((fl_node_ptr)->tag)));                                   \
        assert(atomic_load(&((fl_node_ptr)->ref_count)) > 0);                                               \
                                                                                                            \
        atomic_fetch_sub(&((fl_node_ptr)->ref_count), 1);                                                   \
        atomic_fetch_add(&((lfht_ptr)->num_fl_node_ref_cnt_decs), 1LL);                                     \
    } while( 0 ) /* LFHT_EXIT() */


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
 *    Converted from a function to a macro. Added an output parameter variable
 *    in order to return the boolean value the macro evaluates to. 
 *    Removed a conditional compile for adding a bit mask so that
 *    it is applied in all cases.
 * 
 *                                           AZO -- 2/19/25
 *
 ************************************************************************/

#define LFHT_FIND(lfht_ptr, id, value_ptr, lfht_find__succeeded)                                            \
    do{                                                                                                     \
        bool lfht_find__success = false;                                                                    \
        long long int lfht_find__marked_nodes_visited = 0;                                                  \
        long long int lfht_find__unmarked_nodes_visited = 0;                                                \
        long long int lfht_find__sentinels_traversed = 0;                                                   \
        unsigned long long int lfht_find__hash;                                                             \
        struct lfht_node_t * lfht_find__node_ptr = NULL;                                                    \
        struct lfht_fl_node_t * lfht_find__fl_node_ptr;                                                     \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
                                                                                                            \
                                                                                                            \
        assert((id & LFHT_ID_BIT_MASK) <= LFHT__MAX_ID);                                                    \
                                                                                                            \
        assert(value_ptr);                                                                                  \
                                                                                                            \
        LFHT_ENTER(lfht_ptr, &lfht_find__fl_node_ptr);                                                      \
                                                                                                            \
        LFHT_ID_TO_HASH((id & LFHT_ID_BIT_MASK), false, &lfht_find__hash);                                  \
                                                                                                            \
                                                                                                            \
        /* now attempt to find the target */                                                                \
                                                                                                            \
        LFHT_FIND_INTERNAL(lfht_ptr, lfht_find__hash, &lfht_find__marked_nodes_visited,                     \
                           &lfht_find__unmarked_nodes_visited, &lfht_find__sentinels_traversed,             \
                           &lfht_find__node_ptr);                                                           \
                                                                                                            \
        if ( ( NULL == lfht_find__node_ptr ) || ( (lfht_find__node_ptr)->hash != lfht_find__hash ) ||       \
             ( ((unsigned long long)atomic_load(&((lfht_find__node_ptr)->next))) & 0x01ULL)) {              \
                                                                                                            \
            lfht_find__success = false;                                                                     \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            assert(! (lfht_find__node_ptr)->sentinel);                                                      \
            assert(lfht_find__hash == (lfht_find__node_ptr)->hash);                                         \
            lfht_find__success = true;                                                                      \
            *value_ptr = atomic_load(&((lfht_find__node_ptr)->value));                                      \
        }                                                                                                   \
                                                                                                            \
        /* update statistics */                                                                             \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->searches), 1);                                                       \
                                                                                                            \
        if ( lfht_find__success ) {                                                                         \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->successful_searches), 1);                                        \
            atomic_fetch_add(&((lfht_ptr)->marked_nodes_visited_in_succ_searches),                          \
                             lfht_find__marked_nodes_visited);                                              \
            atomic_fetch_add(&((lfht_ptr)->unmarked_nodes_visited_in_succ_searches),                        \
                             lfht_find__unmarked_nodes_visited);                                            \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->failed_searches), 1);                                            \
            atomic_fetch_add(&((lfht_ptr)->marked_nodes_visited_in_failed_searches),                        \
                            lfht_find__marked_nodes_visited);                                               \
            atomic_fetch_add(&((lfht_ptr)->unmarked_nodes_visited_in_failed_searches),                      \
                            lfht_find__unmarked_nodes_visited);                                             \
        }                                                                                                   \
                                                                                                            \
        if ( lfht_find__sentinels_traversed > 0 ) {                                                         \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->sentinels_traversed), lfht_find__sentinels_traversed);           \
        }                                                                                                   \
                                                                                                            \
        LFHT_EXIT(lfht_ptr, lfht_find__fl_node_ptr);                                                        \
                                                                                                            \
        *(lfht_find__succeeded) = lfht_find__success;                                                       \
                                                                                                            \
    }while( 0 )/* lfht_find() */

/************************************************************************
 *
 * LFHT_FIND_ID_BY_VALUE()
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
 *    Converted from function to a macro. Added an output parameter variable
 *    in order to return the evaluated value when invoked.
 * 
 *                                          AZO -- 2/10/25
 *
 ************************************************************************/

#define LFHT_FIND_ID_BY_VALUE(lfht_ptr, id_ptr, lfsll_value, lfht_find_id_success)                          \
    do {                                                                                                    \
        bool lfht_FIBV__success = false;                                                                    \
        bool lfht_FIBV__marked;                                                                             \
        unsigned long long int lfht_FIBV__marked_nodes_visited = 0;                                         \
        unsigned long long int lfht_FIBV__unmarked_nodes_visited = 0;                                       \
        unsigned long long int lfht_FIBV__sentinels_traversed = 0;                                          \
        unsigned long long int lfht_FIBV__lfsll_id;                                                         \
        struct lfht_node_t * lfht_FIBV__next_ptr = NULL;                                                    \
        struct lfht_node_t * lfht_FIBV__node_ptr = NULL;                                                    \
        struct lfht_fl_node_t * lfht_FIBV__fl_node_ptr;                                                     \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(id_ptr);                                                                                     \
                                                                                                            \
        LFHT_ENTER(lfht_ptr, &lfht_FIBV__fl_node_ptr);                                                      \
                                                                                                            \
                                                                                                            \
        /* now attempt to find the target */                                                                \
                                                                                                            \
        lfht_FIBV__node_ptr = atomic_load(&((lfht_ptr)->lfsll_root));                                       \
                                                                                                            \
        assert(LFHT_VALID_NODE == (lfht_FIBV__node_ptr)->tag);                                              \
        assert(0x0ULL == (((unsigned long long)(lfht_FIBV__node_ptr)) & 0x01ULL));                          \
        assert((lfht_FIBV__node_ptr)->sentinel);                                                            \
                                                                                                            \
        while ( ( lfht_FIBV__node_ptr ) && ( ! lfht_FIBV__success ) ) {                                     \
                                                                                                            \
            assert( LFHT_VALID_NODE == (lfht_FIBV__node_ptr)->tag );                                        \
                                                                                                            \
            lfht_FIBV__next_ptr = atomic_load(&((lfht_FIBV__node_ptr)->next));                              \
                                                                                                            \
            /* test to see if next_ptr is marked.  If it, remove the*/                                      \
            /* mark so we can use it.*/                                                                     \
                                                                                                            \
            if ( ((unsigned long long)(lfht_FIBV__next_ptr)) & 0x01ULL ) {                                  \
                                                                                                            \
                assert(!((lfht_FIBV__node_ptr)->sentinel));                                                 \
                                                                                                            \
                /* node is marked -- remove the mark and increment marked nodes visited */                  \
                lfht_FIBV__next_ptr = (struct lfht_node_t *)(((unsigned long long)(lfht_FIBV__next_ptr)) &  \
                                                             (~0x01ULL));                                   \
                                                                                                            \
                                                                                                            \
                lfht_FIBV__marked = true;                                                                   \
                                                                                                            \
                lfht_FIBV__marked_nodes_visited++;                                                          \
                                                                                                            \
            } else {                                                                                        \
                                                                                                            \
                lfht_FIBV__marked = false;                                                                  \
                                                                                                            \
                if ( ! (lfht_FIBV__node_ptr)->sentinel ) {                                                  \
                                                                                                            \
                    lfht_FIBV__unmarked_nodes_visited++;                                                    \
                }                                                                                           \
            }                                                                                               \
                                                                                                            \
            assert( LFHT_VALID_NODE == (lfht_FIBV__node_ptr)->tag );                                        \
                                                                                                            \
            if ( (lfht_FIBV__node_ptr)->sentinel ) {                                                        \
                                                                                                            \
                lfht_FIBV__sentinels_traversed++;                                                           \
                                                                                                            \
            } else if ( ( ! lfht_FIBV__marked ) &&                                                          \
                        ( atomic_load(&((lfht_FIBV__node_ptr)->value)) == lfsll_value ) ) {                 \
                                                                                                            \
                lfht_FIBV__lfsll_id = (lfht_FIBV__node_ptr)->id;                                            \
                lfht_FIBV__success = true;                                                                  \
            }                                                                                               \
                                                                                                            \
            lfht_FIBV__node_ptr = lfht_FIBV__next_ptr;                                                      \
        }                                                                                                   \
                                                                                                            \
        if ( lfht_FIBV__success ) {                                                                         \
                                                                                                            \
            *id_ptr = lfht_FIBV__lfsll_id;                                                                  \
        }                                                                                                   \
        /* it is tempting to assert that lfht_ptr->lfsll_log_len == 0 if success is false.   */             \
        /**/                                                                                                \
        /* However, there are two problems with this.                                        */             \
        /**/                                                                                                \
        /* First, lfsll_log_len is updated only after the fact, and thus will be briefly     */             \
        /* incorrect after each insertion and deletion. */                                                  \
        /**/                                                                                                \
        /* Second, the search for the first element will fail if entries are inserted at     */             \
        /* the front of the lfsll after the scan for the first element has passed.           */             \
        /**/                                                                                                \
        /* This is OK, as the result will be correct for some ordering of the insertions and */             \
        /* the search for the first element.  If the user wishes to avoid this race, it is   */             \
        /* his responsibility to ensure that the hash table is quiecent during iterations.   */             \
                                                                                                            \
        /* update statistics */                                                                             \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->value_searches), 1);                                                 \
                                                                                                            \
        if ( lfht_FIBV__success ) {                                                                         \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->successful_val_searches), 1);                                    \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->failed_val_searches), 1);                                        \
        }                                                                                                   \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->marked_nodes_visited_in_val_searches),                               \
                         lfht_FIBV__marked_nodes_visited);                                                  \
        atomic_fetch_add(&((lfht_ptr)->unmarked_nodes_visited_in_val_searches),                             \
                         lfht_FIBV__unmarked_nodes_visited);                                                \
        atomic_fetch_add(&((lfht_ptr)->sentinels_traversed), lfht_FIBV__sentinels_traversed);               \
        atomic_fetch_add(&((lfht_ptr)->sentinels_traversed_in_val_searches),                                \
                         lfht_FIBV__sentinels_traversed);                                                   \
                                                                                                            \
        LFHT_EXIT(lfht_ptr, lfht_FIBV__fl_node_ptr);                                                        \
                                                                                                            \
        *lfht_find_id_success = (lfht_FIBV__success);                                                       \
                                                                                                            \
    } while ( 0 )  /* LFHT_FIND_ID_BY_VALUE() */

/************************************************************************
 *
 * LFHT_FIND_INTERNAL()
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
 *    Converted from function to macro. Added an output parameter variable in
 *    order to return the evaluated value upon invocation of the macro.
 * 
 *                                          AZO -- 2/13/25
 *
 ************************************************************************/
#define LFHT_FIND_INTERNAL(lfht_ptr, node_hash, marked_nodes_visited_ptr, unmarked_nodes_visited_ptr,       \
                           sentinels_traversed_ptr, lfht_node_ptr_value)                                    \
    do {                                                                                                    \
                                                                                                            \
        long long int lfht_FI__marked_nodes_visited = 0;                                                    \
        long long int lfht_FI__unmarked_nodes_visited = 0;                                                  \
        long long int lfht_FI__sentinels_traversed = 0;                                                     \
        struct lfht_node_t * lfht_FI__node_ptr = NULL;                                                      \
                                                                                                            \
        LFHT_GET_HASH_BUCKET_SENTINEL((lfht_ptr), (node_hash), (&lfht_FI__node_ptr));                       \
                                                                                                            \
        assert((lfht_ptr));                                                                                 \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert((node_hash) <= LFHT__MAX_HASH);                                                              \
        assert((marked_nodes_visited_ptr));                                                                 \
        assert((unmarked_nodes_visited_ptr));                                                               \
        assert((sentinels_traversed_ptr));                                                                  \
                                                                                                            \
        assert(LFHT_VALID_NODE == (lfht_FI__node_ptr)->tag);                                                \
        assert(0x0ULL == (((unsigned long long)(lfht_FI__node_ptr)) & 0x01ULL));                            \
        assert((lfht_FI__node_ptr)->sentinel);                                                              \
        assert((lfht_FI__node_ptr)->hash < (node_hash));                                                    \
                                                                                                            \
        while ( (lfht_FI__node_ptr)->hash < (node_hash) ) {                                                 \
                                                                                                            \
            lfht_FI__node_ptr = atomic_load(&((lfht_FI__node_ptr)->next));                                  \
                                                                                                            \
             /* Test to see if node_ptr is marked. If it is, remove the */                                  \
             /*  mark so we can use it. */                                                                  \
                                                                                                            \
                                                                                                            \
            if ( ((unsigned long long)(lfht_FI__node_ptr)) & 0x01ULL ) {                                    \
                                                                                                            \
                 /* node is marked -- remove the mark and increment marked nodes visited */                 \
                lfht_FI__node_ptr = (struct lfht_node_t *)(((unsigned long long)(lfht_FI__node_ptr))        \
                                     & (~0x01ULL));                                                         \
                                                                                                            \
                lfht_FI__marked_nodes_visited++;                                                            \
                                                                                                            \
            } else {                                                                                        \
                                                                                                            \
                lfht_FI__unmarked_nodes_visited++;                                                          \
            }                                                                                               \
                                                                                                            \
            assert( LFHT_VALID_NODE == (lfht_FI__node_ptr)->tag );                                          \
                                                                                                            \
            if ( ( (lfht_FI__node_ptr)->sentinel ) && ( (lfht_FI__node_ptr)->hash < (node_hash) ) ) {       \
                                                                                                            \
                lfht_FI__sentinels_traversed++;                                                             \
            }                                                                                               \
        }                                                                                                   \
                                                                                                            \
        if ( ( (lfht_FI__node_ptr)->hash != (node_hash) ) ||                                                \
             ( ((unsigned long long)atomic_load(&((lfht_FI__node_ptr)->next))) & 0x01ULL)) {                \
                                                                                                            \
            lfht_FI__node_ptr = NULL;                                                                       \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            assert(!(lfht_FI__node_ptr)->sentinel);                                                         \
            assert((node_hash) == (lfht_FI__node_ptr)->hash);                                               \
        }                                                                                                   \
                                                                                                            \
        *(marked_nodes_visited_ptr)   = lfht_FI__marked_nodes_visited;                                      \
        *(unmarked_nodes_visited_ptr) = lfht_FI__unmarked_nodes_visited;                                    \
        *(sentinels_traversed_ptr)    = lfht_FI__sentinels_traversed;                                       \
                                                                                                            \
        *lfht_node_ptr_value = (lfht_FI__node_ptr);                                                         \
                                                                                                            \
    } while( 0 ) /* LFHT_FIND_INTERNAL() */

/************************************************************************
 *
 * LFHT_FIND_MOD_POINT()
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
 * Converted from a function to a macro.
 * 
 *                                          Anna Burton -- 2/12/25
 *    
 *
 ************************************************************************/

#define LFHT_FIND_MOD_POINT(lfht_ptr, bucket_head_ptr, first_ptr_ptr, second_ptr_ptr, cols_ptr,             \
                            dels_ptr, nodes_visited_ptr, lfht_FMP__hash)                                    \
    do {                                                                                                    \
        bool lfht_FMP__done = false;                                                                        \
        bool lfht_FMP__retry = false;                                                                       \
        int lfht_FMP__cols = 0;                                                                             \
        int lfht_FMP__dels = 0;                                                                             \
        int lfht_FMP__nodes_visited = 0;                                                                    \
        struct lfht_node_t * lfht_FMP__first_ptr = NULL;                                                    \
        struct lfht_node_t * lfht_FMP__second_ptr = NULL;                                                   \
        struct lfht_node_t * lfht_FMP__third_ptr = NULL;                                                    \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(bucket_head_ptr);                                                                            \
        assert(first_ptr_ptr);                                                                              \
        assert(NULL == *first_ptr_ptr);                                                                     \
        assert(second_ptr_ptr);                                                                             \
        assert(NULL == *second_ptr_ptr);                                                                    \
        assert(cols_ptr);                                                                                   \
        assert(dels_ptr);                                                                                   \
        assert(nodes_visited_ptr);                                                                          \
        assert(lfht_FMP__hash <= LFHT__MAX_HASH);                                                           \
                                                                                                            \
        /* first, find the sentinel node marking the beginning of the  */                                   \
        /* hash bucket that hash maps into.  Note that this sentinel   */                                   \
        /* node may not exist -- if so, lfht_get_hash_bucket_sentinel()*/                                   \
        /* will create and insert it.                                  */                                   \
                                                                                                            \
        do {                                                                                                \
            assert(!lfht_FMP__done);                                                                        \
                                                                                                            \
            lfht_FMP__retry = false;                                                                        \
                                                                                                            \
            lfht_FMP__first_ptr = bucket_head_ptr;                                                          \
                                                                                                            \
            assert(LFHT_VALID_NODE == (lfht_FMP__first_ptr)->tag);                                          \
            assert(0x0ULL == (((unsigned long long)(lfht_FMP__first_ptr)) & 0x01ULL));                      \
            assert((lfht_FMP__first_ptr)->sentinel);                                                        \
            assert((lfht_FMP__first_ptr)->hash < lfht_FMP__hash);                                           \
                                                                                                            \
            lfht_FMP__second_ptr = atomic_load(&((lfht_FMP__first_ptr)->next));                             \
                                                                                                            \
            assert(lfht_FMP__second_ptr);                                                                   \
            assert(0x0ULL == (((unsigned long long)(lfht_FMP__second_ptr)) & 0x01ULL));                     \
            assert(LFHT_VALID_NODE == (lfht_FMP__second_ptr)->tag);                                         \
                                                                                                            \
            do {                                                                                            \
                lfht_FMP__third_ptr = atomic_load(&((lfht_FMP__second_ptr)->next));                         \
                                                                                                            \
                /* if the low order bit on lfht_FMP__third_ptr is set, *lfht_FMP__second_ptr has  */        \
                /* been marked for deletion.  Attempt to unlink and discard                       */        \
                /* *lfht_FMP__second_ptr if so, and repeat until *lfht_FMP__second_ptr no longer  */        \
                /* marked for deletion.  If any deletion completion fails, we                     */        \
                /* must re-start the search for the mod point                                     */        \
                                                                                                            \
                while ( ((unsigned long long)(lfht_FMP__third_ptr)) & 0x01ULL )                             \
                {                                                                                           \
                    assert(lfht_FMP__first_ptr);                                                            \
                    assert(LFHT_VALID_NODE == (lfht_FMP__first_ptr)->tag);                                  \
                                                                                                            \
                    assert(lfht_FMP__second_ptr);                                                           \
                    assert(LFHT_VALID_NODE == (lfht_FMP__second_ptr)->tag);                                 \
                    assert(!((lfht_FMP__second_ptr)->sentinel));                                            \
                                                                                                            \
                    /* lfht_FMP__third_ptr has its low order bit set to indicate that */                    \
                    /* *second_ptr is marked for deletion,  Before we use             */                    \
                    /* lfht_FMP__third_ptr, we must reset the low order bit.          */                    \
                                                                                                            \
                    lfht_FMP__third_ptr = (struct lfht_node_t *)(((unsigned long long)(lfht_FMP__third_ptr))\
                                                                 & ~0x01ULL);                               \
                                                                                                            \
                    assert(lfht_FMP__third_ptr);                                                            \
                    assert(LFHT_VALID_NODE == (lfht_FMP__third_ptr)->tag);                                  \
                                                                                                            \
                    if ( ! atomic_compare_exchange_strong(&((lfht_FMP__first_ptr)->next),                   \
                                                             &lfht_FMP__second_ptr, lfht_FMP__third_ptr) ) {\
                                                                                                            \
                        /* compare and exchange failed -- some other thread             */                  \
                        /* beat us to the unlink.  Increment lfht_FMP__cols, set retry  */                  \
                        /* to TRUE and then restart the search at the head of the SLL.  */                  \
                                                                                                            \
                        lfht_FMP__cols++;                                                                   \
                        lfht_FMP__retry = true;                                                             \
                        break;                                                                              \
                                                                                                            \
                    } else {                                                                                \
                                                                                                            \
                        /* unlink of *lfht_FMP__second_ptr succeeded.  Decrement the logical list length,*/ \
                        /* increment dels, increment nodes_visited, discard *lfht_FMP__second_ptr, set   */ \
                        /* lfht_FMP__second_ptr to lfht_FMP__third_ptr, and then load lfht_FMP__third_ptr*/ \
                                                                                                            \
                        atomic_fetch_sub(&((lfht_ptr)->lfsll_phys_len), 1);                                 \
                        lfht_FMP__dels++;                                                                   \
                        lfht_FMP__nodes_visited++;                                                          \
                        LFHT_DISCARD_NODE(lfht_ptr, lfht_FMP__second_ptr, 0);                               \
                        lfht_FMP__second_ptr = lfht_FMP__third_ptr;                                         \
                        lfht_FMP__third_ptr = atomic_load(&((lfht_FMP__second_ptr)->next));                 \
                                                                                                            \
                        assert(lfht_FMP__first_ptr);                                                        \
                        assert(LFHT_VALID_NODE == (lfht_FMP__first_ptr)->tag);                              \
                                                                                                            \
                        assert(lfht_FMP__second_ptr);                                                       \
                        assert(LFHT_VALID_NODE == (lfht_FMP__second_ptr)->tag);                             \
                                                                                                            \
                    }                                                                                       \
                } /* end while *lfht_FMP__second_ptr is marked for deletion */                              \
                                                                                                            \
                if ( ! lfht_FMP__retry ) {                                                                  \
                                                                                                            \
                    assert(lfht_FMP__first_ptr);                                                            \
                    assert(LFHT_VALID_NODE == (lfht_FMP__first_ptr)->tag);                                  \
                                                                                                            \
                    assert(lfht_FMP__second_ptr);                                                           \
                    assert(LFHT_VALID_NODE == (lfht_FMP__second_ptr)->tag);                                 \
                                                                                                            \
                    assert((lfht_FMP__first_ptr)->hash <= lfht_FMP__hash);                                  \
                                                                                                            \
                    if ( (lfht_FMP__second_ptr)->hash > lfht_FMP__hash ) {                                  \
                                                                                                            \
                        lfht_FMP__done = true;                                                              \
                                                                                                            \
                    } else {                                                                                \
                                                                                                            \
                        if ( (lfht_FMP__second_ptr)->sentinel ) {                                           \
                                                                                                            \
                            atomic_fetch_add(&((lfht_ptr)->sentinels_traversed), 1);                        \
                        }                                                                                   \
                                                                                                            \
                        lfht_FMP__first_ptr = lfht_FMP__second_ptr;                                         \
                        lfht_FMP__second_ptr = lfht_FMP__third_ptr;                                         \
                        lfht_FMP__nodes_visited++;                                                          \
                    }                                                                                       \
                }                                                                                           \
                                                                                                            \
            } while ( ( ! lfht_FMP__done ) && ( ! lfht_FMP__retry ) );                                      \
                                                                                                            \
            assert( ! ( lfht_FMP__done && lfht_FMP__retry ) );                                              \
                                                                                                            \
        } while ( lfht_FMP__retry );                                                                        \
                                                                                                            \
        assert(lfht_FMP__done);                                                                             \
        assert(!lfht_FMP__retry);                                                                           \
                                                                                                            \
        assert((lfht_FMP__first_ptr)->hash <= lfht_FMP__hash);                                              \
        assert(lfht_FMP__hash < (lfht_FMP__second_ptr)->hash);                                              \
                                                                                                            \
        *first_ptr_ptr = lfht_FMP__first_ptr;                                                               \
        *second_ptr_ptr = lfht_FMP__second_ptr;                                                             \
        *cols_ptr += lfht_FMP__cols;                                                                        \
        *dels_ptr += lfht_FMP__dels;                                                                        \
        *nodes_visited_ptr += lfht_FMP__nodes_visited;                                                      \
                                                                                                            \
    } while( 0 ) /* LFHT_FIND_MOD_POINT() */

/***********************************************************************************
 *
 * LFHT_GET_FIRST()
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
 *  Converted from a function to a macro. Added a boolean output parameter variable
 *  in order to return the evaluated value when the macro is invoked.
 * 
 *                                                  AZO -- 2/22/25 
 *
 ***********************************************************************************/

#define LFHT_GET_FIRST(lfht_ptr, id_ptr, value_ptr, lfht_is_empty)                                          \
    do {                                                                                                    \
        bool lfht_GF__success = false;                                                                      \
        bool lfht_GF__marked;                                                                               \
        unsigned long long int lfht_GF__marked_nodes_visited = 0;                                           \
        unsigned long long int lfht_GF__unmarked_nodes_visited = 0;                                         \
        unsigned long long int lfht_GF__sentinels_traversed = 0;                                            \
        unsigned long long int lfht_GF__id;                                                                 \
        void * lfht_GF__value;                                                                              \
        struct lfht_node_t * lfht_GF__next_ptr = NULL;                                                      \
        struct lfht_node_t * lfht_GF__node_ptr = NULL;                                                      \
        struct lfht_fl_node_t * lfht_GF__fl_node_ptr;                                                       \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(id_ptr);                                                                                     \
        assert(value_ptr);                                                                                  \
                                                                                                            \
        LFHT_ENTER(lfht_ptr, &lfht_GF__fl_node_ptr);                                                        \
                                                                                                            \
        /* search for the first entry in the hash table, if it exists */                                    \
                                                                                                            \
        lfht_GF__node_ptr = atomic_load(&((lfht_ptr)->lfsll_root));                                         \
                                                                                                            \
        assert(LFHT_VALID_NODE == (lfht_GF__node_ptr)->tag);                                                \
        assert(0x0ULL == (((unsigned long long)(lfht_GF__node_ptr)) & 0x01ULL));                            \
        assert((lfht_GF__node_ptr)->sentinel);                                                              \
                                                                                                            \
        while ( ( lfht_GF__node_ptr ) && ( ! lfht_GF__success ) ) {                                         \
                                                                                                            \
            assert( LFHT_VALID_NODE == (lfht_GF__node_ptr)->tag );                                          \
                                                                                                            \
            lfht_GF__next_ptr = atomic_load(&((lfht_GF__node_ptr)->next));                                  \
                                                                                                            \
            /* test to see if next_ptr is marked.  If it, remove the  */                                    \
            /* mark so we can use it. */                                                                    \
                                                                                                            \
            if ( ((unsigned long long)(lfht_GF__next_ptr)) & 0x01ULL ) {                                    \
                                                                                                            \
                assert(!((lfht_GF__node_ptr)->sentinel));                                                   \
                                                                                                            \
                /* node is marked -- remove the mark and increment marked nodes visited */                  \
                lfht_GF__next_ptr = (struct lfht_node_t *)(((unsigned long long)(lfht_GF__next_ptr))        \
                                                            & (~0x01ULL));                                  \
                                                                                                            \
                lfht_GF__marked = true;                                                                     \
                                                                                                            \
                lfht_GF__marked_nodes_visited++;                                                            \
                                                                                                            \
            } else {                                                                                        \
                                                                                                            \
                lfht_GF__marked = false;                                                                    \
                                                                                                            \
                if ( ! (lfht_GF__node_ptr)->sentinel ) {                                                    \
                                                                                                            \
                    lfht_GF__unmarked_nodes_visited++;                                                      \
                }                                                                                           \
            }                                                                                               \
                                                                                                            \
            assert( LFHT_VALID_NODE == (lfht_GF__node_ptr)->tag );                                          \
                                                                                                            \
            if ( (lfht_GF__node_ptr)->sentinel ) {                                                          \
                                                                                                            \
                lfht_GF__sentinels_traversed++;                                                             \
                                                                                                            \
            } else if ( ! lfht_GF__marked ) {                                                               \
                                                                                                            \
                lfht_GF__id = (lfht_GF__node_ptr)->id;                                                      \
                lfht_GF__value = atomic_load(&((lfht_GF__node_ptr)->value));                                \
                lfht_GF__success = true;                                                                    \
            }                                                                                               \
                                                                                                            \
            lfht_GF__node_ptr = lfht_GF__next_ptr;                                                          \
        }                                                                                                   \
                                                                                                            \
        if ( lfht_GF__success ) {                                                                           \
                                                                                                            \
            *id_ptr    = lfht_GF__id;                                                                       \
            *value_ptr = lfht_GF__value;                                                                    \
        }                                                                                                   \
                                                                                                            \
        /* update statistics */                                                                             \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->itter_inits), 1);                                                    \
                                                                                                            \
        if ( ! lfht_GF__success ) {                                                                         \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->itter_ends), 1);                                                 \
        }                                                                                                   \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->marked_nodes_visited_in_itters), lfht_GF__marked_nodes_visited);     \
        atomic_fetch_add(&((lfht_ptr)->unmarked_nodes_visited_in_itters), lfht_GF__unmarked_nodes_visited); \
        atomic_fetch_add(&((lfht_ptr)->sentinels_traversed), lfht_GF__sentinels_traversed);                 \
        atomic_fetch_add(&((lfht_ptr)->sentinels_traversed_in_itters), lfht_GF__sentinels_traversed);       \
                                                                                                            \
        LFHT_EXIT(lfht_ptr, lfht_GF__fl_node_ptr);                                                          \
                                                                                                            \
        *lfht_is_empty = (lfht_GF__success);                                                                \
                                                                                                            \
    } while( 0 ) /* LFHT_GET_FIRST() */

/***********************************************************************************
 *
 * LFHT_GET_HASH_BUCKET_SENTINEL()
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
 *  Converted from a function to a macro. Added a parameter variable
 *  in order to return the evaluated lfht_node_t value when the macro
 *  is invoked. 
 * 
 *                                                AZO -- 2/14/25
 *
 ***********************************************************************************/

#define LFHT_GET_HASH_BUCKET_SENTINEL(lfht_ptr, lfht_hash, lfht_sentinel_ptr)                               \
    do{                                                                                                     \
        int lfht_GHBS__index_bits;                                                                          \
        struct lfht_node_t *lfht_GHBS__sentinel_ptr = NULL;                                                 \
        unsigned long long lfht_GHBS__hash_index;                                                           \
                                                                                                            \
        lfht_GHBS__index_bits = atomic_load(&((lfht_ptr)->index_bits));                                     \
        LFHT_HASH_TO_IDX(lfht_hash, lfht_GHBS__index_bits, &lfht_GHBS__hash_index);                         \
                                                                                                            \
        if (NULL == atomic_load(&((lfht_ptr)->bucket_idx[lfht_GHBS__hash_index]))) {                        \
              /* Create bucket if it doesn't exist */                                                       \
            LFHT_CREATE_HASH_BUCKET((lfht_ptr), lfht_hash, lfht_GHBS__index_bits);                          \
        }                                                                                                   \
                                                                                                            \
        lfht_GHBS__sentinel_ptr = atomic_load(&((lfht_ptr)->bucket_idx[lfht_GHBS__hash_index]));            \
                                                                                                            \
          /* Validate the sentinel node */                                                                  \
        assert(lfht_GHBS__sentinel_ptr);                                                                    \
        assert(0x0ULL == (((unsigned long long)(lfht_GHBS__sentinel_ptr)) & 0x01ULL));                      \
        assert(LFHT_VALID_NODE == (lfht_GHBS__sentinel_ptr)->tag);                                          \
        assert((lfht_GHBS__sentinel_ptr)->sentinel);                                                        \
                                                                                                            \
            if ( (lfht_GHBS__sentinel_ptr)->hash > lfht_hash ) {                                            \
                                                                                                            \
                fprintf(stderr, "\nlfht_GHBS__hash_index = %lld, lfht_GHBS__sentinel_ptr->hash = 0x%llx,    \
                                 hash = 0x%llx.\n",                                                         \
                        lfht_GHBS__hash_index, (lfht_GHBS__sentinel_ptr)->hash, lfht_hash);                 \
            }                                                                                               \
                                                                                                            \
        assert((lfht_GHBS__sentinel_ptr)->hash < lfht_hash);                                                \
                                                                                                            \
        *lfht_sentinel_ptr = (lfht_GHBS__sentinel_ptr);                                                     \
    } while( 0 ) /* LFHT_GET_HASH_BUCKET_SENTINEL() */

/***********************************************************************************
 *
 * LFHT_HASH_TO_IDX()
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
 *    Converted from a function to a macro. Added an output parameter variable in order to 
 *    return the evaluated unsigned long long value the macro evaluates to.
 * 
 *                                                Aijun Hall -- 12/20/25
 *
 ***********************************************************************************/

#define LFHT_HASH_TO_IDX(hash, index_bits, lfht_index_id)                                                   \
    do{                                                                                                     \
        assert(0 <= (index_bits));                                                                          \
        assert((index_bits) <= LFHT__MAX_INDEX_BITS);                                                       \
        unsigned long long lfht_HTI__index_hash = (hash) >> 1;                                              \
        unsigned long long lfht_HTI__index = 0x0ULL;                                                        \
        unsigned long long lfht_HTI__hash_bit = 0x01ULL << (LFHT__NUM_HASH_BITS - 1);                       \
        unsigned long long lfht_HTI__idx_bit = 0x01ULL;                                                     \
        for (int lfht_HTI__i = 0; lfht_HTI__i < (index_bits); lfht_HTI__i++) {                              \
            if (0 != (lfht_HTI__hash_bit & lfht_HTI__index_hash)) {                                         \
                lfht_HTI__index |= lfht_HTI__idx_bit;                                                       \
            }                                                                                               \
            lfht_HTI__hash_bit >>= 1;                                                                       \
            lfht_HTI__idx_bit <<= 1;                                                                        \
        }                                                                                                   \
        *lfht_index_id = lfht_HTI__index;                                                                   \
    } while( 0 ) /* LFHT_HASH_TO_IDX() */

/***********************************************************************************
 *
 * LFHT_ID_TO_HASH()
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
 *  - Converted from a function to a macro. Added an output parameter variable in order to 
 *    return the evaluated unsigned long long value the macro evaluates to.
 * 
 *                                               Aijun Hall -- 12/20/25
 *
 ***********************************************************************************/
#define LFHT_ID_TO_HASH(id, sentinel_hash, lfht_id_hash)                                                    \
    do{                                                                                                     \
        unsigned long long lfht_ITH__id_bit = 0x01ULL << (LFHT__NUM_HASH_BITS - 1);                         \
        unsigned long long lfht_ITH__hash_bit = 0x01ULL;                                                    \
        unsigned long long lfht_ITH__hash = 0;                                                              \
        for (int lfht_ITH__i = 0; lfht_ITH__i < LFHT__NUM_HASH_BITS; lfht_ITH__i++) {                       \
            if (0 != (lfht_ITH__id_bit & (id))) {                                                           \
                lfht_ITH__hash |= lfht_ITH__hash_bit;                                                       \
            }                                                                                               \
            lfht_ITH__id_bit >>= 1;                                                                         \
            lfht_ITH__hash_bit <<= 1;                                                                       \
        }                                                                                                   \
        lfht_ITH__hash <<= 1;                                                                               \
        if (!(sentinel_hash)) {                                                                             \
            lfht_ITH__hash |= 0x01ULL;                                                                      \
        }                                                                                                   \
        *lfht_id_hash = lfht_ITH__hash;                                                                     \
                                                                                                            \
    }while( 0 ) /* LFHT_ID_TO_HASH() */



/************************************************************************
 *
 * LFHT_SWAP_VALUE()
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
 *    Converted from a function to a macro. Added a parameter variable
 *    in order to return the boolean value the function evaluates to
 *    when invoked. 
 * 
 *                                          AZO -- 2/13/25
 *
 ************************************************************************/

#define LFHT_SWAP_VALUE(lfht_ptr, swap_value_id, new_value, old_value_ptr, lfht_swap_success)               \
    do{                                                                                                     \
        bool lfht_SV__success = false;                                                                      \
        long long int lfht_SV__marked_nodes_visited = 0;                                                    \
        long long int lfht_SV__unmarked_nodes_visited = 0;                                                  \
        long long int lfht_SV__sentinels_traversed = 0;                                                     \
        unsigned long long int lfht_SV__hash;                                                               \
        struct lfht_node_t * lfht_SV__node_ptr = NULL;                                                      \
        struct lfht_fl_node_t * lfht_SV__fl_node_ptr;                                                       \
                                                                                                            \
        assert(lfht_ptr);                                                                                   \
        assert(LFHT_VALID == (lfht_ptr)->tag);                                                              \
        assert(swap_value_id <= LFHT__MAX_ID);                                                              \
        assert(old_value_ptr);                                                                              \
                                                                                                            \
        LFHT_ENTER(lfht_ptr, &lfht_SV__fl_node_ptr);                                                        \
                                                                                                            \
        LFHT_ID_TO_HASH(swap_value_id, false, &lfht_SV__hash);                                              \
                                                                                                            \
        /* Now attempt to find the target */                                                                \
                                                                                                            \
        LFHT_FIND_INTERNAL(lfht_ptr, lfht_SV__hash, &lfht_SV__marked_nodes_visited,                         \
                    &lfht_SV__unmarked_nodes_visited, &lfht_SV__sentinels_traversed, &lfht_SV__node_ptr);   \
                                                                                                            \
        if ( ( NULL == lfht_SV__node_ptr ) || ( (lfht_SV__node_ptr)->hash != lfht_SV__hash ) ||             \
            ( ((unsigned long long)atomic_load(&((lfht_SV__node_ptr)->next))) & 0x01ULL)) {                 \
                                                                                                            \
            lfht_SV__success = false;                                                                       \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            assert(! (lfht_SV__node_ptr)->sentinel);                                                        \
            assert(lfht_SV__hash == (lfht_SV__node_ptr)->hash);                                             \
            *old_value_ptr = atomic_exchange(&((lfht_SV__node_ptr)->value), new_value);                     \
            lfht_SV__success = true;                                                                        \
        }                                                                                                   \
                                                                                                            \
        /* Update statistics */                                                                             \
                                                                                                            \
        assert(lfht_SV__marked_nodes_visited >= 0);                                                         \
        assert(lfht_SV__unmarked_nodes_visited >= 0);                                                       \
        assert(lfht_SV__sentinels_traversed >= 0);                                                          \
                                                                                                            \
        atomic_fetch_add(&((lfht_ptr)->value_swaps), 1);                                                    \
                                                                                                            \
        if ( lfht_SV__success ) {                                                                           \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->successful_val_swaps), 1);                                       \
            atomic_fetch_add(&((lfht_ptr)->marked_nodes_visited_in_succ_val_swaps),                         \
                                lfht_SV__marked_nodes_visited);                                             \
            atomic_fetch_add(&((lfht_ptr)->unmarked_nodes_visited_in_succ_val_swaps),                       \
                                lfht_SV__unmarked_nodes_visited);                                           \
                                                                                                            \
        } else {                                                                                            \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->failed_val_swaps), 1);                                           \
            atomic_fetch_add(&((lfht_ptr)->marked_nodes_visited_in_failed_val_swaps),                       \
            lfht_SV__marked_nodes_visited);                                                                 \
            atomic_fetch_add(&((lfht_ptr)->unmarked_nodes_visited_in_failed_val_swaps),                     \
            lfht_SV__unmarked_nodes_visited);                                                               \
        }                                                                                                   \
                                                                                                            \
        if ( lfht_SV__sentinels_traversed > 0 ) {                                                           \
                                                                                                            \
            atomic_fetch_add(&((lfht_ptr)->sentinels_traversed), lfht_SV__sentinels_traversed);             \
        }                                                                                                   \
                                                                                                            \
        LFHT_EXIT(lfht_ptr, lfht_SV__fl_node_ptr);                                                          \
                                                                                                            \
        *lfht_swap_success = lfht_SV__success;                                                              \
                                                                                                            \
    } while( 0 ) /* LFHT_SWAP_VALUE() */


#if 0
#define LFHT__NUM_HASH_BITS     48
#define LFHT__MAX_HASH          0x1FFFFFFFFFFFFULL
#define LFHT__MAX_ID             0xFFFFFFFFFFFFULL
#else
/* Note that LFHT__NUM_HASH_BITS must be one greater than the number of bits
 * required to express the largest possible ID.  This is necessary as the
 * current implementation of the LFHT doesn't allow duplicate hash codes,
 * and one additional bit is needed to differentiate between hash codes of
 * IDs and those of sentinel nodes..
 */
#define LFHT__NUM_HASH_BITS     57
#define LFHT__MAX_HASH          0x3FFFFFFFFFFFFFFULL
#define LFHT__MAX_ID            0x1FFFFFFFFFFFFFFULL
#define LFHT_ID_BIT_MASK        0x00FFFFFFFFFFFFFFULL  

#endif
#define LFHT__MAX_INDEX_BITS    10


/***********************************************************************************
 * struct lfht_node_t
 *
 * Node in the lock free singly linked list that is used to store entries in the
 * lock free hash table.  The fields of the node are discussed individually below:
 *
 * tag:		Unsigned integer set to LFHT_VALID_NODE whenever the node is either
 *              in the SLL or the free list, and to LFHT_INVALID_NODE just before
 *              it is discarded.
 *
 * next:        Atomic pointer to the next entry in the SLL, or NULL if there is
 *              no next entry.  Note that due to the alignment guarantees of
 *              malloc() & calloc(), the least significant few bits (at least three
 *              in all cases investigated to date) will be zero.
 *
 *              This fact is used to allow atomic marking of the node for deletion.
 *              If the low order bit of the next pointer is 1, the node is logically
 *              deleted from the SLL.  It will be physically deleted from the SLL
 *              by a subsequent insert or delete call.  See section 9.8 of
 *              "The Art of Multiprocessor Programming" by Herlihy, Luchangco,
 *              Shavit, and Spear for further details.
 *
 * id:          ID associated with the contents of the node.  This field is
 *              logically undefined if the node is a sentinel node
 *
 * hash:        For regular node, this is the hash value computed from the id.
 *              For sentinel nodes, this is the smallest value that can map to
 *              the associated hash table bucket -- see section 13.3.3 of
 *              "The Art of Multiprocessor Programming" by Herlihy, Luchangco,
 *              Shavit, and Spear for further details.
 *
 *              Note that duplicate hash codes cannot appear in the LFSLL, and that
 *              nodes in the LFSLL appear in strictly increasing hash order.
 *
 * sentinel:    Boolean flag that is true if the node is a sentinel node, and
 *              false otherwise.
 *
 * value:       Pointer to whatever structure is used to contain the value associated
 *              with the id, or NULL if the node is a sentinel node.
 *
 *              This field is atomic, as we allow the client code to modify it in
 *              an existing entry in the hash table.
 *
 ***********************************************************************************/

#define LFHT_VALID_NODE    0x1066
#define LFHT_INVALID_NODE  0xDEAD

typedef struct lfht_node_t {

    unsigned int tag;
    struct lfht_node_t * _Atomic next;
    unsigned long long int id;
    unsigned long long int hash;
    bool sentinel;
    void * _Atomic value;

} lfht_node_t;


/***********************************************************************************
 * struct lfht_flsptr_t
 *
 * The lfht_flsptr_t combines a pointer to lfht_fl_node_t with a serial number in
 * a 128 bit package.
 *
 * Unfortunately, this means that operations on atomic instances of this structure
 * may or may not be truly atomic.  Instead, the C11 run time may maintain atomicity
 * with a mutex.  While this may have performance implications, there should be no
 * correctness implications.
 *
 * The combination of a pointer and a serial number is needed to address ABA
 * bugs.
 *
 * ptr:         Pointer to an instance of flht_lf_node_t.
 *
 * sn:          Serial number that should be incremented by 1 each time a new
 *              value is assigned to fl_ptr.
 *
 ***********************************************************************************/

typedef struct lfht_flsptr_t {

    struct lfht_fl_node_t * ptr;
    unsigned long long int  sn;

} lfht_flsptr_t;


/***********************************************************************************
 * struct lfht_fl_node_t
 *
 * Node in the free list for the lock free singly linked list on which entries
 * in the lock free hash table are stored.  The fields of the node are discussed
 * individually below:
 *
 * lfht_node:   Instance of lfht_node_t which is initialized and returned to the
 *              lfht code by lfht_create_node.
 *
 *              If no node is available on the free list, an instance of
 *              lfht_fl_node_t is allocated and initialized.  Its pointer is
 *              then cast to a pointer to lfht_node_t, and returned to the
 *              caller.
 *
 *              A node is available on the free list if the list contains more
 *              that one entry, and the ref count on the first node in the list
 *              is zero.  In this case, the first node is removed from the free
 *              list, re-initialzed, its pointer cast to a pointer to lfht_node_t,
 *              and returned to the caller.
 *
 * tag:		Atomic unsigned integer set to LFHT_FL_NODE_IN_USE whenever the
 *              node is in the SLL, to LFHT_FL_NODE_ON_FL when the node is on the
 *              free list, and to LFHT_FL_NODE_INVALID just before the instance of
 *              lfht_fl_node_t is freed.
 *
 * ref_count:   If this instance of lfht_fl_node_t is at the tail of the free
 *              list, the ref_count is incremented whenever a thread enters one
 *              of the LFHT API calls, and decremented when the API call exits.
 *
 * sn:          Unique, sequential serial number assigned to each node when it
 *              is placed on the free list.  Used for debugging.
 *
 * snext;       Atomic instance of struct lfht_flsptr_t, which contains a
 *              pointer (ptr) to the next node on the free list for the lock
 *              free singly linked list, and a serial number (sn) which must be
 *              incremented each time a new value is assigned to snext.
 *
 *              The objective here is to prevent ABA bugs, which would
 *              otherwise occasionally allow leakage of a node.
 *
 ***********************************************************************************/

#define LFHT_FL_NODE_IN_USE          0x1492 /*  5266 */
#define LFHT_FL_NODE_ON_FL           0xBEEF /* 48879 */
#define LFHT_FL_NODE_INVALID         0xDEAD /* 57005 */

typedef struct lfht_fl_node_t {

    struct lfht_node_t lfht_node;
    _Atomic unsigned int tag;
    _Atomic unsigned int ref_count;
    _Atomic unsigned long long int sn;
   _Atomic struct lfht_flsptr_t snext;

} lfht_fl_node_t;



/***********************************************************************************
 * struct lfht_t
 *
 * Root of a lock free hash table (LFHT).
 *
 * Entries in the hash table are stored in a lock free singly linked list (LFSLL).
 *
 * Each hash bucket has a sentinel node in the linked list that marks the
 * beginning of the bucket.  Pointers to the sentinel nodes are stored in
 * the index.
 *
 * Section 13.3.3 of "The Art of Multiprocessor Programming" by Herlihy,
 * Luchangco, Shavit, and Spear describes most of the details of the algorithm.
 * However, that discussion presumes implementation in a language with
 * garbage collection -- which simplifies matters greatly.
 *
 * The basic problem here is that we can't free a node that has been
 * removed from the LFSLL until we know that all references to it have been
 * discarded.  This is a problem, as an arbitrary number of threads may
 * have a pointer to a node at the point at which it is physically deleted
 * from the LFSLL.
 *
 * We solve this problem as follows:
 *
 * First, don't allow any node on the LFSLL to become visible outside of
 * the LFHT package.  As a result, we know that all pointers to a discarded
 * node have been discarded as well once all threads that were active in the
 * LFHT code at the point that the node was discarded have exited the LFHT
 * code.  We know this, as any such pointers must have been allocated on the
 * stack, and were therefore discarded when the associated threads left the
 * LFHT package.
 *
 * Second, maintain a free list of discarded nodes, and decorate each discarded
 * node with a reference count (see declarations of lfht_node_t and lfht_fl_node_t
 * above).
 *
 * Ideally, on entry to the LFHT package, each thread would increment the
 * reference count on the last node on the free list, and then decrement it
 * on exit. However, until I can find a way to make this operation atomic, this
 * is not workable, as the tail node may advance to the head of the free list
 * and be re-allocated in the time between the read of the pointer to the last
 * element on the free list, and the increment of the indicated ref_count.
 *
 * Instead, on entry to the LFHT package, each thread allocates a node, sets its
 * ref_count to 1, and releases it to the free list.  On exit, it decrements
 * the node’s ref_count back to zero.  This has the same net effect, but is not as
 * efficient.  Needless to say, this issue should be revisited for the production
 * version.
 *
 * If we further require that nodes on the free list are only removed from
 * the head of the list (either for re-use or discard), and then only when their
 * reference counts are zero, we have that nodes are only released to the
 * heap or re-used if all threads that were active in LFHT package at the point
 * at which the node was place on the free list have since exited the LFHT
 * package.
 *
 * Between them, these two adaptions solve the problem of avoiding accesses to
 * nodes that have been returned to the heap.
 *
 * Finally, observe that the LFSLL code is simplified if it always contains two
 * sentinel nodes with (effectively) values negative and positive infinity --
 * thus avoiding operations that touch the ends of the list.
 *
 * In this context, the index sentinel node with hash value zero is created
 * at initialization time and serves as the node with value negative infinity.
 * However, since a sentinel node with hash positive infinity is not created
 * by the index, we add a sentinel node with hash LLONG_MAX at initialization
 * to serve this purpose.
 *
 * Note that the LFSLL used in the implementation of the LFHT is a modified
 * version of the lock free singly linked list discussed in chapter 9 of
 * "The Art of Multiprocessor Programming" by Herlihy, Luchangco, Shavit,
 * and Spear.
 *
 * The fields of lfht_t are discussed individually below.
 *
 *
 * tag:         Unsigned integer set to LFHT_VALID when the instance of lfht_t
 *              is initialized, and to LFHT_INVALID just before the memory for
 *              the instance of struct lfHT_t is discarded.
 *
 *
 * Lock Free Singly Linked List related fields:
 *
 * lfsll_root:  Atomic Pointer to the head of the SLL.  Other than during setup,
 *              this field will always point to the first sentinal node in the
 *              index, whose hash will be zero.
 *
 * lfsll_log_len:  Atomic integer used to maintain a count of the number of nodes
 *              in the SLL less the sentry nodes and the regular nodes that
 *              have been marked for deletion.
 *
 *              Note that due to the delay between the insertion or deletion
 *              of a node, and the update of the field, this count may be off
 *              for brief periods of time.
 *
 * lfsll_phys_len:  Atomic integer used to maintain a count of the actual number
 *              of nodes in the SLL.  This includes the sentry nodes, and any
 *              nodes that have been marked for deletion, but that have not
 *              been physically deleted.
 *
 *              Note that due to the delay between the insertion or deletion
 *              of a node, and the update of the field, this count may be off
 *              for brief periods of time.
 *
 *
 * Free list related fields:
 *
 *
 * fl_shead:    Atomic instance of struct lfht_flsptr_t, which contains a
 *              pointer (ptr) to the head of the free list for the lock free
 *              singly linked list, and a serial number (sn) which must be
 *              incremented each time a new value is assigned to fl_shead.
 *
 *              The objective here is to prevent ABA bugs, which would
 *              otherwise occasionally allow allocation of free list
 *              nodes with positive ref counts.
 *
 * fl_stail:    Atomic instance of struct lfht_flsptr_t, which contains a
 *              pointer (ptr) to the tail of the free list for the lock free
 *              singly linked list, and a serial number (sn) which must be
 *              incremented each time a new value is assigned to fl_stail.
 *
 *              The objective here is to prevent ABA bugs, which would
 *              otherwise occasionally allow the tail of the free list to
 *              get ahead of the head -- resulting in the increment of the
 *              ref count on nodes that are no longer in the free list.
 *
 * fl_len:      Atomic integer used to maintain a count of the number of nodes
 *              on the free list.  Note that due to the delay between free list
 *              insertions and deletions, and the update of this field, this
 *              count may be off for brief periods of time.
 *
 *              Further, the free list must always contain at least one entry.
 *              Thus, when correct, fl_len will be one greater than the number
 *              of nodes available on the free list.
 *
 * max_desired_fl_len: Integer field containing the desired maximum free list
 *              length.  This is of necessity a soft limit as entries cannot
 *              be removed from the head of the free list unless their
 *              reference counts are zero.  Further, at most one entry is
 *              removed from the head of the free list per call to
 *              lfht_discard_node().
 *
 * next_sn:     Serial number to be assigned to the next node placed on the
 *              free list.
 *
 *
 * Hash Bucket Index:
 *
 * index_bits:  Number of index bits currently in use.
 *
 * max_index_bits:  Maximum value that index_bits is allowed to attain.  If
 *              this field is set to zero, the lock free hash table becomes
 *              a lock free singly linked list, as only one hash bucket is
 *              permitted.
 *
 * index_masks: Array of unsigned long long containing the bit masks used to
 *              compute the index into the hash bucket array from a hash code.
 *
 * buckets_defined: Convenience field.  This is simply 2 ** index_bits.
 *              Needless to say, buckets_initialized must always be less than
 *              or equal to buckets_initialized.
 *
 * buckets_initialized: Number of hash buckets that have been initialized --
 *              that is, their sentinel nodes have been created, and inserted
 *              into the LFSLL, and a pointer to the sentinel node has been
 *              copied into the bucket_idx.
 *
 * bucket_idx:  Array of pointers to lfht_node_t.  Each entry in the array is
 *              either NULL, or contains a pointer to the sentinel node marking
 *              the beginning of the hash bucket indicated by its index in the
 *              array.
 *
 * Statistics Fields:
 *
 * The following fields are used to record statistics on the operation of the
 * SLL for purposes of debugging and optimization.  All fields are atomic.
 *
 * max_lfsll_log_len: Maximum logical length of the LFSLL.  In the multi-thread
 *              case, this number should be viewed as aproximate.
 *
 * max_lfsll_phys_len: Maximum physical length of the LFSLL.  In the multi-thread
 *              case, this number should be viewed as aproximate.
 *
 *
 * max_fl_len:  Maximum number of nodes that have resided on the free list
 *              at any point during the current run.  In the multi-thread
 *              case, this number should be viewed as aproximate.
 *
 * num_nodes_allocated: Number of nodes allocated from the heap.
 *
 * num_nodes_freed: Number of nodes freed.
 *
 * num_node_free_candidate_selection_restarts: Number of times the attempt
 *              to pull a node from the head of the free list and free
 *              had to be restared due to a change in fl_head.
 *
 * num_nodes_added_to_fl: Number of nodes added to the free list.
 *
 * num_nodes_drawn_from_fl: Number of nodes drawn from the free list for
 *              re-use.
 *
 * num_fl_head_update_cols: Number of collisions when updating the fl_head
 *              for a node withdrawal.
 *
 * num_fl_tail_update_cols: Number of collisions when updating the fl_tail
 *              for an additions.
 *
 * num_fl_append_cols: Number of collisions when appending a newly freed
 *              node to the end of the free list.
 *
 * num_fl_req_denied_due_to_empty: Number of free list requests denied
 *              because the free list was (logically) empty. (recall that
 *              the free list must always have at least one entry.).
 *
 * num_fl_req_denied_due_to_ref_count: Number of free list requests
 *              denied because the entry at the head of the free list
 *              had a positive ref count.
 *
 * num_fl_node_ref_cnt_incs: Number of time the ref count on some node
 *              on the free list has been incremented.
 *
 * num_fl_node_ref_cnt_inc_retrys: Number of times a ref count increment
 *              on some node on the free list had to me un-done due to
 *              a change in fl_tail.
 *
 * num_fl_node_ref_cnt_decs: Number of times the ref count on some
 *              node on the free list has been decremented.
 *
 * num_fl_frees_skiped_due_to_empty:  Number of times that
 *              lfsll_discard_node() has attempted to find a node to
 *              free, but been unable to do so because the free list
 *              is empty.  Assuming a reasonable max_desired_fl_len,
 *              this is very improbable -- however, since we must
 *              check for the possibility, we may as well make note
 *              of it if it occurs.
 *
 * num_fl_frees_skiped_due_to_ref_count: Number of times that
 *              lfsll_discard_node() has attempted to find a node to
 *              free, but been unable to do so because the first item
 *              on the free list has a positive ref count.  Absent
 *              either a very small max_desired_fl_len or a very large
 *              number of threads, this is also improbable -- but again,
 *              since we must check for it, we log it if it occurs.
 *
 *
 * index_bits_incr_cols: Number of times there have been index bits
 *              increment collisions.
 *
 * buckets_defined_update_cols: Number of collisions between threads
 *              attempting to update bucket_defined.
 *
 * buckets_defined_update_retries: Number of retries updating
 *               buckets_defined.
 *
 * bucket_init_cols: Number of times there have been bucket initization
 *              collisions.
 *
 * bucket_init_col_sleeps:  Number of times a losing thread in the race
 *              to initialize a hash bucket has had to sleep while
 *              waiting for the sinner to complete the job.
 *
 * insertions:  Number of entries that have been inserted into the SLL.
 *
 * insertion_failure: Number of times an insertion attempt has failed.
 *
 * ins_restarts_due_to_ins_col: Number of times an insertion has had to restart
 *              due to a failed compare and swap when inserting the new node.
 *
 * ins_restarts_due_to_del_col: Number of times an insertion has had to restart
 *              due to a collision while removing a node marked for deletion.
 *
 * ins_deletion_completions: Number of node deletions completed in passing
 *              during insertions.
 *
 * nodes_visited_during_ins: Number of nodes visited during insertions.
 *
 *
 * deletion_attemps:  Number of times that a deletion from the SLL has been
 *              requested.
 *
 * deletions_starts:  Number of entries that have been marked for deletion
 *              from the SLL.
 *
 * deletion_start_cols: Number of failed attempts to mark an entry in the SLL
 *              for deletion.
 *
 * deletion_failures: Number of times that a deletion request has failed because
 *              the target can't be found.
 *
 * del_restarts_due_to_del_col: Number of times a deletion has had to restart
 *              due to a collision while removing a node marked for deletion.
 *
 * del_retries: Number of time marking a mode as deleted has had to be retried
 *              due to an un-expected value in the next field -- typically,
 *              this is due to a node insertion / deletion just after the
 *              the target in the interval between finding the target and
 *              marking it.
 *
 * del_deletion_completions: Number of node deletions completed in passing
 *              during deletions.
 *
 * nodes_visited_during_dels: Number of nodes visited during deletions.
 *
 *
 * searches:    Number of searches.
 *
 * successful_searches: Number of successful searches.
 *
 * failed_searches: Number of failed searches.
 *
 * marked_nodes_visited_in_succ_searches:  Number of marked nodes visited
 *              during successful searches.
 *
 * unmarked_nodes_visited_in_succ_searches:  Number of unmarked nodes visited
 *              during successful searches.
 *
 * marked_nodes_visited_in_failed_searches: Number of marked nodes visited during
 *              failed searches.
 *
 * unmarked_nodes_visited_in_failed_searches: Number of unmarked nodes visited
 *              during failed searches.
 *
 *
 * searches:    Number of value spaps.
 *
 * successful_val_swaps: Number of successful value swaps.
 *
 * failed_val_swaps: Number of failed value swaps.
 *
 * marked_nodes_visited_in_succ_val_swaps:  Number of marked nodes visited
 *              during successful value swaps.
 *
 * unmarked_nodes_visited_in_succ_val_swaps:  Number of unmarked nodes visited
 *              during successful value swaps.
 *
 * marked_nodes_visited_in_failed_val_swaps: Number of marked nodes visited during
 *              failed val swaps.
 *
 * unmarked_nodes_visited_in_failed_val_swaps: Number of unmarked nodes visited
 *              during failed val swaps.
 *
 *
 * value_searches: Searches for entries by value instead of id.
 *
 * successful_val_searches: Successful searches for entries by value.
 *
 * failed_val_searches: Failed searches for entries by value.
 *
 * marked_nodes_visited_in_val_searches; Marked nodes visited during searches by
 *              value.
 *
 * unmarked_nodes_visited_in_val_searches: Unmarked nodes other than sentinels
 *              visited during searches by value.
 *
 * sentinels_traversed_in_val_searches: Sentinel nodes traversed during searches
 *              by value.
 *
 * itter_inits: Number of times that an itteration through the entries in the
 *              hash table has been initiated via a call to lfht_get_next().
 *
 * itter_nexts: Number of times the next element in an itteration through the
 *              entries in the hash table has been returned.
 *
 * itter_ends:  Number of times that an itterations through the entries in
 *              the hash table has been followed to its end.
 *
 * marked_nodes_visited_in_itters: Marked nodes visited during itterations
 *               trhough the entries in the hash table.
 *
 * unmarked_nodes_visited_in_itters: Un-marked nodes that are not sentinels
 *               visited during itterations through the entries in the hash table.
 *
 * sentinels_traversed_in_itters: Number of sentinel nodes traversed during
 *               itterations through the entries in the hash table.
 *
 ***********************************************************************************/

#define LFHT_VALID                     0x628
#define LFHT_INVALID                   0xDEADBEEF
#define LFHT__MAX_DESIRED_FL_LEN       256
#define LFHT__BASE_IDX_LEN             1024

typedef struct lfht_t
{
   unsigned int tag;


   /* LFSLL: */

   struct lfht_node_t * _Atomic lfsll_root;
   _Atomic long unsigned long int lfsll_log_len;
   _Atomic long unsigned long int lfsll_phys_len;


   /* Free List: */

   _Atomic struct lfht_flsptr_t fl_shead;
   _Atomic struct lfht_flsptr_t fl_stail;
   _Atomic long long int fl_len;
   int max_desired_fl_len;
   _Atomic unsigned long long int next_sn;


   /* hash bucket index */

   _Atomic int index_bits;
   int max_index_bits;
   unsigned long long int index_masks[LFHT__NUM_HASH_BITS + 1];
   _Atomic unsigned long long int buckets_defined;
   _Atomic unsigned long long int buckets_initialized;
   _Atomic (struct lfht_node_t *) bucket_idx[LFHT__BASE_IDX_LEN];


   /* statistics: */
   _Atomic unsigned long long int max_lfsll_log_len;
   _Atomic unsigned long long int max_lfsll_phys_len;

   _Atomic long long int max_fl_len;
   _Atomic long long int num_nodes_allocated;
   _Atomic long long int num_nodes_freed;
   _Atomic long long int num_node_free_candidate_selection_restarts;
   _Atomic long long int num_nodes_added_to_fl;
   _Atomic long long int num_nodes_drawn_from_fl;
   _Atomic long long int num_fl_head_update_cols;
   _Atomic long long int num_fl_tail_update_cols;
   _Atomic long long int num_fl_append_cols;
   _Atomic long long int num_fl_req_denied_due_to_empty;
   _Atomic long long int num_fl_req_denied_due_to_ref_count;
   _Atomic long long int num_fl_node_ref_cnt_incs;
   _Atomic long long int num_fl_node_ref_cnt_inc_retrys;
   _Atomic long long int num_fl_node_ref_cnt_decs;
   _Atomic long long int num_fl_frees_skiped_due_to_empty;
   _Atomic long long int num_fl_frees_skiped_due_to_ref_count;

   _Atomic long long int index_bits_incr_cols;
   _Atomic long long int buckets_defined_update_cols;
   _Atomic long long int buckets_defined_update_retries;
   _Atomic long long int bucket_init_cols;
   _Atomic long long int bucket_init_col_sleeps;
   _Atomic long long int recursive_bucket_inits;
   _Atomic long long int sentinels_traversed;

   _Atomic long long int insertions;
   _Atomic long long int insertion_failures;
   _Atomic long long int ins_restarts_due_to_ins_col;
   _Atomic long long int ins_restarts_due_to_del_col;
   _Atomic long long int ins_deletion_completions;
   _Atomic long long int nodes_visited_during_ins;

   _Atomic long long int deletion_attempts;
   _Atomic long long int deletion_starts;
   _Atomic long long int deletion_start_cols;
   _Atomic long long int deletion_failures;
   _Atomic long long int del_restarts_due_to_del_col;
   _Atomic long long int del_retries;
   _Atomic long long int del_deletion_completions;
   _Atomic long long int nodes_visited_during_dels;

   _Atomic long long int searches;
   _Atomic long long int successful_searches;
   _Atomic long long int failed_searches;
   _Atomic long long int marked_nodes_visited_in_succ_searches;
   _Atomic long long int unmarked_nodes_visited_in_succ_searches;
   _Atomic long long int marked_nodes_visited_in_failed_searches;
   _Atomic long long int unmarked_nodes_visited_in_failed_searches;

   _Atomic long long int value_swaps;
   _Atomic long long int successful_val_swaps;
   _Atomic long long int failed_val_swaps;
   _Atomic long long int marked_nodes_visited_in_succ_val_swaps;
   _Atomic long long int unmarked_nodes_visited_in_succ_val_swaps;
   _Atomic long long int marked_nodes_visited_in_failed_val_swaps;
   _Atomic long long int unmarked_nodes_visited_in_failed_val_swaps;

   _Atomic long long int value_searches;
   _Atomic long long int successful_val_searches;
   _Atomic long long int failed_val_searches;
   _Atomic long long int marked_nodes_visited_in_val_searches;
   _Atomic long long int unmarked_nodes_visited_in_val_searches;
   _Atomic long long int sentinels_traversed_in_val_searches;

   _Atomic long long int itter_inits;
   _Atomic long long int itter_nexts;
   _Atomic long long int itter_ends;
   _Atomic long long int marked_nodes_visited_in_itters;
   _Atomic long long int unmarked_nodes_visited_in_itters;
   _Atomic long long int sentinels_traversed_in_itters;

} lfht_t;

