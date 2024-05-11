#include <std_list.h>

#include <std_compiler.h>
#include <std_platform.h>
#include <std_log.h>

// =============================================================================

typedef struct std_list_item_i {
    struct std_list_item_i* next;
} std_list_item_i;

void std_list_push ( void* _list, void* _item ) {
    std_list_insert ( _list, _item );
}

void* std_list_pop ( void* _list ) {
    std_list_item_i* list = ( std_list_item_i* ) _list;
    std_assert_m ( list != NULL );
    std_list_item_i* head = ( std_list_item_i* ) list->next;

    if ( head == NULL ) {
        return NULL;
    }

    list->next = head->next;
    return head;
}

/*
void std_list_clear ( std_list_t* list ) {
    std_assert_m ( list != NULL );
    void* item;

    while ( ( item = std_list_pop ( list ) ) != NULL ) {
        std_mem_set ( item, sizeof ( item ), 0 );
    }
}
*/

void* std_list_next ( void* _item ) {
    std_list_item_i* item = ( std_list_item_i* ) _item;
    std_assert_m ( item != NULL );
    std_assert_m ( item != item->next );
    return item->next;
}

void std_list_insert ( void* _parent, void* _item ) {
    std_list_item_i* parent = ( std_list_item_i* ) _parent;
    std_list_item_i* item = ( std_list_item_i* ) _item;
    std_assert_m ( parent != NULL );
    std_assert_m ( item != NULL );
    std_assert_m ( item != parent );
    std_assert_m ( parent->next != item ); // loop check
    item->next = parent->next;
    parent->next = item;
}

void std_list_remove ( void* _parent, void* _item ) {
    std_list_item_i* parent = ( std_list_item_i* ) _parent;
    std_list_item_i* item = ( std_list_item_i* ) _item;
    std_assert_m ( parent != NULL );
    std_assert_m ( item != NULL );
    std_assert_m ( parent->next == item );
    parent->next = item->next;
}

/*
void* std_list_head ( std_list_t* list ) {
    std_assert_m ( list != NULL );
    return list->head;
}

void* std_list_self ( std_list_t* list ) {
    return list;
}
*/

// =============================================================================

typedef struct std_dlist_item_i {
    struct std_dlist_item_i* next;
    struct std_dlist_item_i* prev;
} std_dlist_item_i;

void std_dlist_push ( void* _list, void* _item ) {
    std_dlist_item_i* list = ( std_dlist_item_i* ) _list;
    std_dlist_item_i* item = ( std_dlist_item_i* ) _item;
    std_assert_m ( list != NULL );
    std_assert_m ( item != NULL );
    //std_assert_m ( list->prev == NULL );
    item->next = list->next;

    if ( list->next ) {
        list->next->prev = item;
    }

    list->next = item;
    item->prev = list;
}

void* std_dlist_pop ( void* _list ) {
    std_dlist_item_i* list = ( std_dlist_item_i* ) _list;
    std_assert_m ( list != NULL );
    //std_assert_m ( list->prev == NULL );
    std_dlist_item_i* item = ( std_dlist_item_i* ) list->next;

    if ( item == NULL ) {
        return NULL;
    }

    list->next = item->next;

    if ( item->next ) {
        item->next->prev = list;
    }

    item->next = NULL;
    item->prev = NULL;

    return item;
}

//void* std_dlist_next ( void* _item ) {
//    std_dlist_item_i* item = ( std_dlist_item_i* ) _item;
//    std_assert_m ( item != NULL );
//    return item->next;
//}
//
//void* std_dlist_prev ( void* _item ) {
//    std_dlist_item_i* item = ( std_dlist_item_i* ) _item;
//    std_assert_m ( item != NULL );
//    return item->prev;
//}

void std_dlist_remove ( void* _item ) {
    std_dlist_item_i* item = ( std_dlist_item_i* ) _item;
    std_assert_m ( item != NULL );
    std_dlist_item_i* next = item->next;
    std_dlist_item_i* prev = item->prev;

    if ( next ) {
        next->prev = prev;

    }

    if ( prev ) {
        prev->next = next;
    }

    item->next = NULL;
    item->prev = NULL;
}

void std_dlist_push_offset ( void* _list, void* _item, uint64_t offset ) {
    std_auto_m list = ( std_dlist_item_i* ) ( ( char* ) _list + offset );
    std_auto_m item = ( std_dlist_item_i* ) ( ( char* ) _item + offset );
    std_assert_m ( list != NULL );
    std_assert_m ( item != NULL );
    //std_assert_m ( list->prev == NULL );
    item->next = list->next;

    if ( list->next ) {
        std_auto_m list_next = ( std_dlist_item_i* ) ( ( char* ) list->next + offset );
        list_next->prev = item;
    }

    list->next = item;
    item->prev = list;
}

void* std_dlist_pop_offset ( void* _list, uint64_t offset ) {
    std_auto_m list = ( std_dlist_item_i* ) ( ( char*) _list + offset );
    std_assert_m ( list != NULL );
    //std_assert_m ( list->prev == NULL );
    std_auto_m item = ( std_dlist_item_i* ) ( ( char* ) list->next + offset );

    if ( item == NULL ) {
        return NULL;
    }

    list->next = item->next;

    if ( item->next ) {
        std_auto_m item_next = ( std_dlist_item_i* ) ( ( char* ) item->next + offset );
        item_next->prev = list;
    }

    item->next = NULL;
    item->prev = NULL;

    return item;
}

void std_dlist_remove_offset ( void* _item, uint64_t offset ) {
    std_auto_m item = ( std_dlist_item_i* ) ( ( char*) _item + offset );
    std_assert_m ( item != NULL );
    std_auto_m next = ( std_dlist_item_i* ) ( ( char*) item->next + offset );
    std_auto_m prev = ( std_dlist_item_i* ) ( ( char*) item->prev + offset );

    if ( next ) {
        next->prev = prev;

    }

    if ( prev ) {
        prev->next = next;
    }

    item->next = NULL;
    item->prev = NULL;
}

// =============================================================================

void* std_freelist ( void* base, size_t stride, size_t capacity ) {
    std_assert_m ( base != NULL );
    std_assert_m ( stride >= std_pointer_size_m );
    std_assert_m ( std_align_test_ptr ( base, std_pointer_size_m ) );
    std_assert_m ( std_align_test ( stride, std_pointer_size_m ) );
    std_assert_m ( capacity > 0 );

    void* p;
    void* end = base + stride * ( capacity - 1 ); 
    for ( p = base; p < end; p += stride ) {
        void* next = p + stride;
        std_mem_copy ( p, &next, sizeof ( next ) );
    }
    void* next = NULL;
    std_mem_copy ( p, &next, sizeof ( next ) );

    return base;
}

// =============================================================================

#if 0
std_pool_t std_pool ( std_buffer_t buffer, size_t stride ) {
    std_pool_t pool;
    pool.buffer = buffer;
    pool.freelist = std_freelist ( buffer, stride );
    pool.cap = buffer.size / stride;
    pool.pop = pool.cap;
    pool.stride = stride;
    return pool;
}

void* std_pool_pop ( std_pool_t* pool ) {
    std_assert_m ( pool->pop > 0 );

    --pool->pop;
    return std_list_pop ( &pool->freelist );
}

void std_pool_push ( std_pool_t* pool, void* item ) {
    std_assert_m ( pool->pop < pool->cap );
    std_assert_m ( ( char* ) item >= pool->buffer.base && ( char* ) item < pool->buffer.base + pool->buffer.size );

    ++pool->pop;
    std_list_push ( &pool->freelist, item );
}

void std_pool_push_idx ( std_pool_t* pool, size_t idx ) {
    std_assert_m ( pool->pop < pool->cap );

    ++pool->pop;
    std_list_push ( &pool->freelist, pool->buffer.base + idx * pool->stride );
}

void* std_pool_at ( const std_pool_t* pool, size_t idx ) {
    return pool->buffer.base + idx * pool->stride;
}

size_t std_pool_index ( const std_pool_t* pool, void* item ) {
    return ( size_t ) ( ( char* ) item - pool->buffer.base ) / pool->stride;
}
#endif

// =============================================================================
#if 0
typedef struct {
    uint64_t size; // When the top bit is set to 1, it means the segment coming after is free, and the remaining 63 bits hold that segment size (size includes start and end)
    void* prev;
    void* next;
} std_segment_pool_segment_start_t;

typedef struct {
    uint64_t size; // When the top bit is set to 1, it means the segment coming before is free, and the remaining 63 bits hold that segment size (size includes start and end)
} std_segment_pool_segment_end_t;

static int std_segment_pool_bin_size_cmp ( const void* a, const void* b ) {
    const uint64_t* u1 = ( const uint64_t* ) a;
    const uint64_t* u2 = ( const uint64_t* ) b;

    if ( *u1 < *u2 ) {
        return -1;
    } else if ( *u1 > *u2 ) {
        return 1;
    } else {
        return 0;
    }
}

static void std_segment_pool_bin_segment ( std_segment_pool_t* pool, void* base, size_t size ) {
    std_assert_m ( size >= 32 );
    std_assert_m ( size % pool->stride == 0 );

    for ( size_t i = 0; i < std_segment_pool_max_bins_m; ++i ) {
        if ( pool->bins[i].size <= size ) {
            //pool->bins[i].list = std_list ( pool.buffer.base );
            std_segment_pool_segment_start_t* start = ( std_segment_pool_segment_start_t* ) base;
            start->prev = &pool->bins[i].list;
            start->next = NULL;
            start->size = size;
            std_segment_pool_segment_end_t* end = ( std_segment_pool_segment_end_t* ) ( pool.buffer.base + size - sizeof ( std_segment_pool_segment_end_t ) );
            end->size = size;
            break;
        }
    }
}

std_segment_pool_t std_segment_pool ( std_buffer_t pool_buffer, size_t stride, const size_t* bins, size_t bin_count ) {
    std_assert_m ( stride >= 32 );
    std_assert_m ( std_align_test ( stride, 8 ) );
    std_assert_m ( std_align_test ( pool_buffer.base, 8 ) );

    std_segment_pool_t pool;
    pool.buffer = pool_buffer;
    pool.cap = pool.buffer.size / stride;
    pool.pop = pool.cap;
    pool.stride = stride;

    std_assert_m ( bin_count <= std_segment_pool_max_bins_m );
    size_t sorted_bins[std_segment_pool_max_bins_m];
    std_sort_insertion_copy ( sorted_bins, bins, sizeof ( uint64_t ), bin_count, std_segment_pool_bin_size_cmp );

    {
        uint size_t bin_it = 0;

        for ( ; bin_it < bin_count; ++bin_it ) {
            pool.bins[i].size = sorted_bins[i];
            pool.bins[i].list = std_list ( NULL );
        }

        if ( bin_it < std_segment_pool_max_bins_m ) {
            ++bin_it;
        }

        pool.bins[bin_it].size = UINT64_MAX;
        pool.bins[bin_it].list = std_list ( NULL );
    }

    size_t segment_size = pool.cap * stride;

    for ( size_t i = 0; i < std_segment_pool_max_bins_m; ++i ) {
        if ( pool.bins[i].size <= segment_size ) {
            pool.bins[i].list = std_list ( pool.buffer.base );
            std_segment_pool_segment_start_t* start = ( std_segment_pool_segment_start_t* ) pool.buffer.base;
            start->prev = &pool.bins[i].list;
            start->next = NULL;
            start->size = segment_size;
            std_segment_pool_segment_end_t* end = ( std_segment_pool_segment_end_t* ) ( pool.buffer.base + segment_size - sizeof ( std_segment_pool_segment_end_t ) );
            end->size = segment_size;
            break;
        }
    }

    return pool;
}

void* std_segment_pool_pop ( std_pool_t* pool, size_t count ) {

}

void std_segment_pool_push ( std_pool_t* pool, void* base, size_t count ) {

}

void* std_segment_pool_at ( const std_pool_t* pool, size_t idx ) {

}

size_t std_segment_pool_index ( const std_pool_t* pool, void* item ) {

}
#endif
