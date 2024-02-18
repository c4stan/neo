#include <std_array.h>

#include <std_log.h>

std_array_t std_array ( uint64_t capacity ) {
    std_array_t array;
    array.count = 0;
    array.capacity = capacity;
    return array;
}

void std_array_push ( std_array_t* array, uint64_t count ) {
    std_assert_m ( array->capacity >= array->count + count );
    array->count += count;
}

void std_array_pop ( std_array_t* array, uint64_t count ) {
    std_assert_m ( array->count >= count );
    array->count -= count;
}
