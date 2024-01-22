#pragma once

#include <std_platform.h>

// TODO use macros for these?
void                            std_compiler_fence ( void );
void                            std_memory_fence   ( void );

// Returns whether the CAS was successful. The actual read is written into the expected read param.
// TODO is the _ptr api necessary?
bool                            std_compare_and_swap_i32 ( int32_t* atomic, int32_t* expected_read, int32_t conditional_write );
bool                            std_compare_and_swap_i64 ( int64_t* atomic, int64_t* expected_read, int64_t conditional_write );
bool                            std_compare_and_swap_u32 ( uint32_t* atomic, uint32_t* expected_read, uint32_t conditional_write );
bool                            std_compare_and_swap_u64 ( uint64_t* atomic, uint64_t* expected_read, uint64_t conditional_write );

// Returns the old value
int32_t                         std_atomic_exchange_i32 ( int32_t* atomic, int32_t write );
int64_t                         std_atomic_exchange_i64 ( int64_t* atomic, int64_t write );
uint32_t                        std_atomic_exchange_u32 ( uint32_t* atomic, uint32_t write );
uint64_t                        std_atomic_exchange_u64 ( uint64_t* atomic, uint64_t write );

// Returns the atomic value post-increment
int32_t                         std_atomic_increment_i32 ( int32_t* atomic );
int64_t                         std_atomic_increment_i64 ( int64_t* atomic );
uint32_t                        std_atomic_increment_u32 ( uint32_t* atomic );
uint64_t                        std_atomic_increment_u64 ( uint64_t* atomic );
int32_t                         std_atomic_decrement_i32 ( int32_t* atomic );
int64_t                         std_atomic_decrement_i64 ( int64_t* atomic );
uint32_t                        std_atomic_decrement_u32 ( uint32_t* atomic );
uint64_t                        std_atomic_decrement_u64 ( uint64_t* atomic );

// Returns the atomic value pre-add
int32_t                         std_atomic_fetch_add_i32 ( int32_t* atomic, int32_t value_to_add );
int64_t                         std_atomic_fetch_add_i64 ( int64_t* atomic, int64_t value_to_add );
uint32_t                        std_atomic_fetch_add_u32 ( uint32_t* atomic, uint32_t value_to_add );
uint64_t                        std_atomic_fetch_add_u64 ( uint64_t* atomic, uint64_t value_to_add );
