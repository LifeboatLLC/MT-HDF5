/* Lock free hash table code */

#include <stdatomic.h>

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

#endif
#define LFHT__MAX_INDEX_BITS    10
#define LFHT__USE_SPTR          1


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
 *              no next entry.  Note that due to the alignement guarantees of 
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
 *              nodes in the LFSLL appear in strictly increasing order.
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
 * The 128 bit package is necessary to allow lock free atomic operations on at 
 * least some platforms.
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
    unsigned long long int sn;

} lfht_flsptr_t;


/***********************************************************************************
 * struct lfht_fl_node_t
 *
 * Node in the free list for the lock free singly linked list on which entries 
 * in the lock free hash table are stored.  The fields of the node are discussed 
 * individually below:
 *
 * lfht_node:   Instance lfht_node_t which is initialized and returned to the lfht
 *              code by lfht_create_node.  
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
#if LFHT__USE_SPTR
 * snext;       Atomic instance of struct lfht_flsptr_t, which contains a 
 *              pointer (ptr) to the next node on the free list for the lock 
 *              free singly linked list, and a serial number (sn) which must be 
 *              incremented each time a new value is assigned to snext.
 *
 *              The objective here is to prevent ABA bugs, which would 
 *              otherwise occasionally allow leakage of a node.
#else
 * next:        Atomic pointer to the next entry in the free list, or NULL if 
 *              there is no next entry.  
#endif
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
#if LFHT__USE_SPTR
   _Atomic struct lfht_flsptr_t snext;
#else /* LFHT__USE_SPTR */
    struct lfht_fl_node_t * _Atomic next;
#endif /* LFHT__USE_SPTR */

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
 * above).  On entry to the LFHT package, each thread must increment the reference
 * count on the last node on the free list, and then decrement it on exit.  
 *
 * If we further require that nodes on the free list are only removed from
 * the head of the list (either for re-use or discard), and then only when their 
 * reference counts are zero, we have that nodes are only released to the 
 * heap or re-used if all threads that were active in LFHT package at the point
 * at which the node was place on the free list have since exited the LFHT 
 * package.  
 *
 * Between them, these two adaptions solve the problem of avoiding accesse to 
 * nodes that have been returned to the heap.
 *
 * Finally, observe that the LFSLL code is simplified if it always contains two
 * sentinel nodes with (effectively) values negative and positive infinity -- 
 * thus avoiding operations that touch the ends of the list.
 *
 * In this context, the index sentinel node with hash value zero is created 
 * at initialization time and serves as the  node with value negative infinity.  
 * However, since a sentinel node with hash positive infinity is not created 
 * by the index, we add a sentinel node with hash LLONG_MAX at initialization
 * to serve this purpose.
 *
 * Note that the LFSLL used in the implementation of the LFHT is a modified 
 * version of the lock free singly linked  list discussed in chapter 9 of 
 * "The Art of Multiprocessor Programming" by Herlihy, Luchangco, Shavit, 
 * and Spear. 
 *
 * The fields of lfht_t are discussed individually below.
 *
 *
 * tag:		Unsigned integer set to LFHT_VALID when the instance of lfht_t 
 *              is initialized, and to LFHT_INVALID just before the memory for 
 *              the instance of struct lfHT_t is discarded.
 *
 *
 * Lock Free Singly Linked List related fields:
 *
 * lfsll_root:	Atomic Pointer to the head of the SLL.  Other than during setup, 
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
#if LFHT__USE_SPTR
 *
 *  fl_shead:   Atomic instance of struct lfht_flsptr_t, which contains a 
 *              pointer (ptr) to the head of the free list for the lock free 
 *              singly linked list, and a serial number (sn) which must be 
 *              incremented each time a new value is assigned to fl_shead.
 *
 *              The objective here is to prevent ABA bugs, which would 
 *              otherwise occasionally allow allocation of free list 
 *              nodes with positive ref counts.
 *
 *  fl_stail:   Atomic instance of struct lfht_flsptr_t, which contains a 
 *              pointer (ptr) to the tail of the free list for the lock free 
 *              singly linked list, and a serial number (sn) which must be 
 *              incremented each time a new value is assigned to fl_stail.
 *
 *              The objective here is to prevent ABA bugs, which would 
 *              otherwise occasionally allow the tail of the free list to 
 *              get ahead of the head -- resulting in the increment of the 
 *              ref count on nodes that are no longer in the free list.
 *
#else
 *
 * fl_head:     Pointer to the head of the free list for the lock free singly 
 *              linked list. Once initialized, the free list will always contain
 *              at least one entry, and is logically empty if it contains only
 *              one entry (i.e. fl_head == fl_tail != NULL).
 *
 * fl_tail:     Pointer to the tail of the free list for the lock free singly
 *              linked list.  Once initialized, the free list will always contain
 *              at least one entry, and is logically empty if it contains only
 *              one entry (i.e. fl_head == fl_tail != NULL).
 *
 #endif
 *
 * fl_len:      Atomic integer used to maintain a count of the number of nodes
 *              on the free list.  Note that due to the delay between free list
 *              insertions and deletins, and the update of this field, this 
 *              count may be off for brief periods of time.
 *
 *              Further, since the free list must always contain at least one 
 *              entry.  When correct, fl_len will be one greater than the number
 *              of nodess available on the free list.
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
 *              a lock free singly lined list, as only one hash bucket is 
 *              permitted.
 *
 * index_masks: Array of unsiged long long containing the bit masks used to 
 *              compute the index into the hash bucket array from a hash code.
 *
 * buckets_defined: Convenience field.  This is simply 2 ** index_bits.  
 *              Needless to say, buckets_initialized must always be less than
 *              or equal to buckets_initialized.
 *
 * buckets_initialized: Number of hash buckets that have been initialized -- 
 *              that is, their sentinel nodes have been created, and inserted
 *              into the LFSLL, and a pointer to the sendinel node has been 
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

#if LFHT__USE_SPTR
   _Atomic struct lfht_flsptr_t fl_shead;
   _Atomic struct lfht_flsptr_t fl_stail;
#else /* LFHT__USE_SPTR */
   struct lfht_fl_node_t * _Atomic fl_head;
   struct lfht_fl_node_t * _Atomic fl_tail;
#endif /* LFHT__USE_SPTR */
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


bool lfht_add(struct lfht_t * lfht_ptr, unsigned long long int id, void * value);
bool lfht_add_internal(struct lfht_t * lfht_ptr, struct lfht_node_t * bucket_head_ptr,
                       unsigned long long int id, unsigned long long int hash, bool sentinel, void * value,
                       struct lfht_node_t ** new_node_ptr_ptr);
void lfht_clear(struct lfht_t * lfht_ptr);
void lfht_clear_stats(struct lfht_t * lfht_ptr);
void lfht_create_hash_bucket(struct lfht_t * lfht_ptr, unsigned long long int hash, int index_bits);
struct lfht_node_t * lfht_create_node(struct lfht_t * lfht_ptr, unsigned long long int id,
                                      unsigned long long int hash, bool sentinel, void * value);
void lfht_discard_node(struct lfht_t * lfht_ptr, struct lfht_node_t * node_ptr, unsigned int expected_ref_count);
bool lfht_delete(struct lfht_t * lfht_ptr, unsigned long long int id);
void lfht_dump_list(struct lfht_t * lfht_ptr, FILE * file_ptr);
void lfht_dump_stats(struct lfht_t * lfht_ptr, FILE * file_ptr);
struct lfht_fl_node_t * lfht_enter(struct lfht_t * lfht_ptr);
void lfht_exit(struct lfht_t * lfht_ptr, struct lfht_fl_node_t * fl_node_ptr);
bool lfht_find(struct lfht_t * lfht_ptr, unsigned long long int id, void ** value_ptr);
bool lfht_find_id_by_value(struct lfht_t * lfht_ptr, unsigned long long int *id_ptr, void * value);
struct lfht_node_t * lfht_find_internal(struct lfht_t * lfht_ptr, unsigned long long int hash,
                                        long long int * marked_nodes_visited_ptr,
                                        long long int * unmarked_nodes_visited_ptr,
                                        long long int * sentinels_traversed_ptr);
void lfht_find_mod_point(struct lfht_t * lfht_ptr, struct lfht_node_t * bucket_head_ptr,
                         struct lfht_node_t ** first_ptr_ptr, struct lfht_node_t ** second_ptr_ptr,
                         int * cols_ptr, int * dels_ptr, int * nodes_visited_ptr,
                         unsigned long long int hash);
bool lfht_get_first(struct lfht_t * lfht_ptr, unsigned long long int * id_ptr, void ** value_ptr);
struct lfht_node_t * lfht_get_hash_bucket_sentinel(struct lfht_t * lfht_ptr, unsigned long long int hash);
bool lfht_get_next(struct lfht_t * lfht_ptr, unsigned long long int old_id, unsigned long long int * id_ptr,
                   void ** value_ptr);
unsigned long long lfht_hash_to_idx(unsigned long long hash, int index_bits);
unsigned long long lfht_id_to_hash(unsigned long long id, bool sentinel_hash);
void lfht_init(struct lfht_t * lfht_ptr);
bool lfht_swap_value(struct lfht_t * lfht_ptr, unsigned long long int id, void * new_value,
                     void ** old_value_ptr);


