#pragma once

#include <std_byte.h>
#include <std_compiler.h>

/* =========================================================================== *
*
*
    About modules and memory allocation
        Pass some kind of allocator somehow to modules ?
        Have a way to query each module for memory allocation stats (e.g. memory allocated, memory used, ...)

        Basically atm there's no design for how modules should manage memory
        and report on that. There is no defined interface on how a module
        gets a way to allocate memory and on how it can report on that
        to the main user.

        To provide a standard way for modules to allocate, one can either
        pass one or more allocators to every module on creation, or allow
        modules to fetch one or more global allocators.
        The issue with passing allocators is that currently it is not clear
        on which module load call actually triggers the initialization/load
        of the module. The actual module init is hidden from the caller. So
        it is not possible to explicitly trigger a module init and pass a
        specific allocator to the module. So the only option is to let modules
        pick an allocator from one or more available allocators. (actually,
        the alternative could be something like having a module specific for
        assigning allcoators to modules e.g. by name, and then modules could
        fetch their allocator from this module).
        The best result would be to be able to support both modules that
        alloc from the generic os heap as much as they want and restricted
        pre-allocated memory footprints for all specified modules. Additionally,
        for some modules it might make sense to allow them to explicitly require
        a portion of memory... although you can do that with an os heap, too, by
        simply doing a single heap allocation. Finally, should std provice a way
        to allocate virtual pages? should that be the main allocation method
        of modules?
            On the virtual memory note, I think it works more like, some
            allocators need to preallocate pools/blocks of memory to store
            smaller client allocations, and at that point you might as well
            fit your pools to system memory pages. So only pool-backed
            allocators should allocate virtual memory pages, others can simply
            use the system heap (or some other generic heap)
            On the generic heap note, perhaps malloc should never be used, and
            instead another general purpose heap allocator should replace it.
        One simple solution is having a configuration file that works as
        module name -> footprint table. when the module requests its allocator,
        if it's on the table, it gets a limited size allocator instead of
        the os heap allocator.
        One important thing about tracking is that std allocations are special,
        in the sense that they are not identified by their memory address but
        rather by their ID returned by the alloc call in the alloc_t struct.
        So tracking can be handle based rather than address based, which makes
        it quite easier to track individual allocations. Something like a table
        handle (~>array idx) -> info becomes easy to do and efficient.
        Offline memory tracking is also possible. Perhaps a good in-between is
        to track per system total allocation (can be used to cap systems?) and
        then also 'log' in a linear buffer all allocations? Even if one buffer
        is shared for all allocations logs all that is necessary is an atomic
        bump on it.
        Should memtrack be a module or a part of std? I'm feeling more like a
        module, that also (separately, in another module) provides a way to
        read and analyze the logs.
        So, finally, every module is free to grab a std allocator and allocate
        from it. The module must track its allocation from this allocator. Once
        memory has been allocated this way, the module is free to use it in
        whatever way. Tracking sub-allocations to that memory is encouraged,
        but not obligatory.

    About using more complex allocators (E.g. tagged frame-aware allocator)
        std doesn't really support the idea of resizable memory, or cyclic allocations (e.g. every frame) of unknown size.
        the only kinds of allocations supported are static allocations that for some reason need to use heap memory,
        and temporary allocations that basically might aswell live on the stack, if it wasn't for their size.
        Resizable memory (e.g. vector-like containers) should use an allocator interface to grow.
        Systems that support cyclic allocations should either depend explicitly on a specific allocator that fits that
        allocation pattern, or leave their allocations call configurable from outside.
        Sub-systems that build on top of those low level systems should go back to more standard allocation methods. The
        inner subsystems don't need to be aware of the fact that the system is using a cyclic allocator. They should be
        provided with just the allocated memory, if their footprint is fixed, or with the memory and a way to require more,
        if their footprint is dynamic, but the actual interfacing to the custom allocator should be left to the lower system.
        This allows to easily swap the custom allocator to test or switch to another one, without affecting the higher level
        subsystems.

    About tracking std allocations

    About being able to validate pointers
        it could be in theory possible to establish that, for example, a pointer to a std_thread_t struct in order to be
        valid has to point o a specific range of memory, because all threads are allocated there. This does not necessarily
        mean that those ranges must be static (although they might as well), but at the very least at some point at runtime
        it has to be decided that each subsystem has a unique partition of memory addresses assigned to him and it has to
        be possible to programmatically retrieve that partition and test a pointer against its bounds.
        since std's state is fully self contained this is pretty easy to achieve inside std code.
        outer code could either use static allocations, meaning that they are the only ones that can track/test against it,
        or dynamic allocations.

    About malloc
        malloc should be completely replaced by a custom heap allocator. The purpose of such allocator is to basically share
        unused virtual memory pages with other systems as opposed to taking a whole page and using only a part of it.
        Thanks to our allocation model the heap doesn't need to take any tagging or headers on user allocations.
        Allocating a size that's an exact multiple of the virtual page size should cause the heap to act as a simple proxy
        for the virtual allocator. This can easily be checked both on alloc and on free. In other cases the heap takes a page
        from the virtual allocator and takes ownership of it. Once a page's content has been fully freed the heap loses
        ownership of the page and asks the virtual allocator to free it.
        At every time the heap owns a number of sets of pages, where each set is a bunch of contiguous pages. These sets can
        be stored in a virtual linear array where only the part that's actually used gets committed. The heap does 2 operations
        on the array, insertion (append at the end) and removal (swap with last). Some additional metadata stored in this array
        alongside the pointer to the actual memory page could possibly speed up allocations. For example storing the max. free
        segment size would allow to skip all sets where no allocation is possible. On the other side, however, doing so would
        require having to traverse the entire set freelist every time an allocation happens in the set, instead of traversing
        it until a segment good enough is found. On the upper side, though, this means that we can pick the optimal segment to
        do the allocation relative to the page set, instead of the first.
        To minimize space waste it could be a good idea to require allocations to be tagged as temporary or permanent. This
        is to avoid degenerate cases where eg. we allocate 1.5 pages, then we allocate 8 bytes right after it, then we free
        the 1.5 pages allocation, and repeat. This would cause the heap to have a number of pages where only 8 bytes are used,
        and possibly run out of memory if this degenerate allocation pattern goes on long enough. Temp/perm pags would solve
        this, having the 1.5 pages tagged as temp and the 8 bytes as perm, the heap can allocate them in 2 separate sets.


*
*
* =========================================================================== */

/*
    Program stack
*/
#if defined(std_platform_win32_m)
    #define std_program_stack_alloc_m( size ) _alloca ( size )
#elif defined(std_platform_linux_m)
    #define std_program_stack_alloc_m( size ) alloca ( size )
#endif
#define std_program_stack_alloc_array_m( type, count ) ( type* ) std_program_stack_alloc_m ( sizeof ( type ) * (count) );

/*
    Virtual memory page allocator
*/
size_t std_virtual_page_size ( void );
size_t std_virtual_page_align ( size_t size );

void* std_virtual_alloc ( size_t size );
void* std_virtual_reserve ( size_t size );
bool std_virtual_map ( void* begin, void* end ); // begin included, end excluded
bool std_virtual_unmap ( void* begin, void* end );
bool std_virtual_free ( void* begin, void* end ); // on Win32 this range must match the entire reserved range

/*
    Virtual heap allocator
    TODO rename to global_heap? heap? ???
*/
void*   std_virtual_heap_alloc ( size_t size, size_t align );
bool    std_virtual_heap_free ( void* ptr );
#define std_virtual_heap_alloc_m( type ) std_virtual_heap_alloc_array_m ( type, 1 )
#define std_virtual_heap_alloc_array_m( type, count ) ( type* ) ( std_virtual_heap_alloc ( sizeof ( type ) * (count), std_alignof_m ( type ) ) )

/*
    Buffer utilities
*/
// Just a utility struct. Can be used as return value, or to store a memory segment without having to split it into 2 separate fields
typedef struct {
    void* base;
    size_t size;
} std_buffer_t;

std_buffer_t std_buffer ( void* base, size_t size );
#define std_buffer_m( item ) std_buffer ( item, sizeof ( *item ) )
#define std_null_buffer_m ( std_buffer_t ) { NULL, 0 }

std_buffer_t std_virtual_heap_read_file ( const char* path );

// TODO split the stacks out to new file std_stack?
// Linear fixed size allocator. Can be manually resized by cloning to a bigger separate stack and freeing the old one. 
typedef struct {
    void* begin;
    void* top;
    void* end;
} std_stack_t;

std_stack_t std_stack ( void* base, size_t size );
void*       std_stack_alloc ( std_stack_t* buffer, size_t size );
void*       std_stack_write ( std_stack_t* buffer, const void* data, size_t size );
bool        std_stack_align ( std_stack_t* buffer, size_t align );
bool        std_stack_align_zero ( std_stack_t* buffer, size_t align );
void*       std_stack_alloc_align ( std_stack_t* buffer, size_t size, size_t align );
void*       std_stack_write_align ( std_stack_t* buffer, const void* data, size_t size, size_t align );
void        std_stack_clear ( std_stack_t* buffer );
char*       std_stack_string_copy ( std_stack_t* buffer, const char* std );
char*       std_stack_string_append ( std_stack_t* buffer, const char* std );
char*       std_stack_string_append_char ( std_stack_t* stack, char c );
void        std_stack_string_pop ( std_stack_t* stack );
void        std_stack_free ( std_stack_t* buffer, size_t size );
void        std_stack_clone ( std_stack_t* from, std_stack_t* to );

#define std_static_stack_m( array ) std_stack ( array, sizeof ( array ) )
#define std_stack_alloc_array_m( stack, type, count ) ( type* ) std_stack_alloc_align ( stack, sizeof ( type ) * (count), std_alignof_m ( type ) )
#define std_stack_alloc_m( stack, type ) std_stack_alloc_array_m ( stack, type, 1 )
#define std_stack_write_noalign_m( stack, data ) std_stack_write ( stack, data, sizeof ( *data ) )
#define std_stack_array_count_m( stack, type ) ( ( (stack)->top - (stack)->begin ) / sizeof ( type ) )

// Linear allocator based on virtual memory. The mapped segment will grow until the whore reserved range is full. It will not grow further.
typedef struct {
    union {
        std_stack_t mapped;
        struct {
            void* begin;
            void* top;
            void* end;
        };
    };
    void* virtual_end;
} std_virtual_stack_t;

std_virtual_stack_t std_virtual_stack ( void* base, size_t mapped_size, size_t virtual_size );
std_virtual_stack_t std_virtual_stack_create ( size_t virtual_size );
void                std_virtual_stack_destroy ( std_virtual_stack_t* stack );
void*       std_virtual_stack_alloc ( std_virtual_stack_t* buffer, size_t size );
void*       std_virtual_stack_write ( std_virtual_stack_t* buffer, const void* data, size_t size );
bool        std_virtual_stack_align ( std_virtual_stack_t* buffer, size_t align );
bool        std_virtual_stack_align_zero ( std_virtual_stack_t* buffer, size_t align );
void*       std_virtual_stack_alloc_align ( std_virtual_stack_t* buffer, size_t size, size_t align );
void*       std_virtual_stack_write_align ( std_virtual_stack_t* buffer, const void* data, size_t size, size_t align );
void        std_virtual_stack_clear ( std_virtual_stack_t* buffer );
char*       std_virtual_stack_string_copy ( std_virtual_stack_t* buffer, const char* std );
char*       std_virtual_stack_string_append ( std_virtual_stack_t* buffer, const char* std );
void        std_virtual_stack_free ( std_virtual_stack_t* buffer, size_t size );

#define std_virtual_stack_alloc_array_m( stack, type, count ) ( type* ) std_virtual_stack_alloc_align ( stack, sizeof ( type ) * (count), std_alignof_m ( type ) )
#define std_virtual_stack_alloc_m( stack, type ) std_virtual_stack_alloc_array_m ( stack, type, 1 )

/*
    Linear stack allocator
*/
#if 0
typedef enum {
    std_arena_allocator_heap_m,
    std_arena_allocator_virtual_m,
    std_arena_allocator_none_m,
} std_arena_allocator_e;

typedef struct {
    std_buffer_t buffer;
    void* virtual_end;
    std_arena_allocator_e allocator;
} std_arena_t;

std_arena_t std_arena_create ( std_arena_allocator_e allocator, size_t size );
std_arena_t std_arena ( void* base, size_t size, size_t virtual_size, std_arena_allocator_e allocator );
void        std_arena_destroy ( std_arena_t* arena );
void*       std_arena_alloc ( std_arena_t* arena, size_t size );
bool        std_arena_write ( std_arena_t* arena, const void* data, size_t size );
bool        std_arena_align ( std_arena_t* arena, size_t align );
bool        std_arena_align_zero ( std_arena_t* arena, size_t align );
void*       std_arena_alloc_align ( std_arena_t* arena, size_t size, size_t align );
bool        std_arena_write_align ( std_arena_t* arena, const void* data, size_t size, size_t align );
void        std_arena_free ( std_arena_t* arena, size_t size );
void        std_arena_clear ( std_arena_t* arena );
size_t      std_arena_used_size ( std_arena_t* arena );

char*       std_arena_string_copy ( std_arena_t* arena, const char* str );
char*       std_arena_string_append ( std_arena_t* arena, const char* str );

#define     std_static_arena_m( array ) std_arena ( array, sizeof ( array ), sizeof ( array ), std_arena_allocator_none_m );
#define     std_fixed_arena_m( base, size ) std_arena ( base, size, 0, std_arena_allocator_none_m );
#define     std_heap_arena_m( base, size ) std_arena ( base, size, 0, std_arena_allocator_heap_m );
#define     std_virtual_arena_m( buffer ) std_arena ( buffer.base, buffer.mapped_size, buffer.reserved_size, std_arena_allocator_virtual_m );

#define     std_arena_write_noalign_m( arena, data ) std_arena_write ( arena, data, sizeof ( *data ) )
#define     std_arena_alloc_m( arena, type ) ( type* ) std_arena_alloc_align ( arena, sizeof ( type ) , std_alignof_m ( type ) )
#define     std_arena_alloc_array_m( arena, type, count ) ( type* ) std_arena_alloc_align ( arena, sizeof ( type ) * (count), std_alignof_m ( type ) )
#endif

/*
    Tagged allocator
    // TODO remove?
*/
#if 0
size_t       std_tagged_page_size ( void );
std_buffer_t std_tagged_alloc ( size_t size, uint64_t tag );
void         std_tagged_free ( uint64_t tag );
#endif


