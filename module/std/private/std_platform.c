#include <std_platform.h>

#include "std_state.h"

//==============================================================================

static std_platform_state_t* std_platform_state;

//==============================================================================

#if defined(std_platform_linux_m)
    #include <sys/types.h>
    #include <dirent.h>
#endif

void std_platform_init ( std_platform_state_t* state ) {
#if defined(std_platform_win32_m)
    std_mem_zero_m ( state );
    //state->logical_cores_mask = 0;
    //state->caches_count = 0;
    //state->physical_cores_count = 0;
    //state->logical_cores_count = 0;
    size_t processors_count = 0;
    // System logical cores information
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION processor_info[128];
    DWORD processor_info_size = sizeof ( processor_info );
    GetLogicalProcessorInformation ( processor_info, &processor_info_size );

    for ( size_t i = 0, o = 0; o < processor_info_size; ++i, o += sizeof ( SYSTEM_LOGICAL_PROCESSOR_INFORMATION ) ) {
        SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info = &processor_info[i];

        switch ( info->Relationship ) {
            case RelationProcessorCore: {
                size_t physical_core_idx = state->physical_cores_count;
                std_platform_physical_core_info_t* physical_core = &state->physical_cores_info[physical_core_idx];
                physical_core->logical_cores_mask = info->ProcessorMask;
                uint64_t mask = info->ProcessorMask;

                while ( mask ) {
                    uint32_t logical_core_idx = std_bit_scan_64 ( mask );
                    std_bit_clear_64 ( &mask, logical_core_idx );

                    if ( !std_bit_test_64 ( state->logical_cores_mask, logical_core_idx ) ) {
                        std_bit_set_64 ( &state->logical_cores_mask, logical_core_idx );
                        ++state->logical_cores_count;
                    }

                    std_platform_logical_core_info_t* logical_core = &state->logical_cores_info[logical_core_idx];
                    logical_core->physical_core_idx = physical_core_idx;
                }

                ++state->physical_cores_count;
                break;
            }

            case RelationCache:
                size_t cache_idx = state->caches_count;
                std_platform_cache_info_t* cache = &state->caches_info[cache_idx];
                cache->logical_cores_mask = info->ProcessorMask;
                std_platform_cache_type_e cache_type;

                switch ( info->Cache.Type ) {
                    case CacheUnified:
                        cache_type = std_platform_cache_unified_m;
                        break;

                    case CacheInstruction:
                        cache_type = std_platform_cache_instruction_m;
                        break;

                    case CacheData:
                        cache_type = std_platform_cache_data_m;
                        break;

                    case CacheTrace:
                        cache_type = std_platform_cache_type_unknown_m;
                        break;
                }

                cache->type = cache_type;
                std_platform_cache_level_e cache_level = 0;

                switch ( info->Cache.Level ) {
                    case 1:
                        cache_level = std_platform_cache_l1_m;
                        break;

                    case 2:
                        cache_level = std_platform_cache_l2_m;
                        break;

                    case 3:
                        cache_level = std_platform_cache_l3_m;
                        break;
                }

                cache->level = cache_level;
                cache->size = info->Cache.Size;
                cache->line_size = info->Cache.LineSize;
                cache->associativity = info->Cache.Associativity;

                if ( cache->associativity == CACHE_FULLY_ASSOCIATIVE ) {
                    cache->associativity = std_platform_cache_fully_associative_m;
                }

                ++state->caches_count;
                break;

            case RelationProcessorPackage:
                ++processors_count;
                break;

            case RelationNumaNode:
                break;

            default:
                break;
        }
    }

    std_assert_warn_m ( processors_count == 1, "Found more than one processors. This is not (yet) supported. Something might break!" );

    // Memory information
    SYSTEM_INFO os_info;
    GetSystemInfo ( &os_info );
    ULONGLONG total_memory_kb;
    GetPhysicallyInstalledSystemMemory ( &total_memory_kb );
    state->memory_info.total_ram_size = total_memory_kb * 1024;
    state->memory_info.virtual_page_size = os_info.dwPageSize;
    state->memory_info.total_swap_size = 0; // TODO
    //state->memory_info.lowest_user_memory_address = 0;
    //state->memory_info.highest_user_memory_address = 0;
#elif defined(std_platform_linux_m)
    // parse /sys/devices/system/cpu
    const char* base = "/sys/devices/system/cpu/";

    for ( size_t cpu_it = 0; cpu_it < 64; ++cpu_it ) {
        char cpu_path[128];
        std_str_format ( cpu_path, 128, std_fmt_str_m"cpu"std_fmt_size_m, base, cpu_it );
        DIR* cpu_dir = opendir ( cpu_path );

        if ( !cpu_dir ) {
            break;
        }

        char file_path[128];
        char buffer[128];

        std_platform_physical_core_info_t* physical_core;
        size_t physical_core_idx;
        bool new_physical_core = false;
        {
            std_str_format ( file_path, 128, std_fmt_str_m"/topology/core_id", cpu_path );
            int file = open ( file_path, O_RDONLY );
            std_assert_m ( file != -1 );
            ssize_t result = read ( file, buffer, 128 );
            std_assert_m ( result != -1 );

            physical_core_idx = std_str_to_u32 ( buffer );
            physical_core = &state->physical_cores_info[physical_core_idx];

            if ( physical_core->logical_cores_mask == 0 ) {
                ++state->physical_cores_count;
                new_physical_core = true;
            }
        }

        if ( new_physical_core ) {
            std_str_format ( file_path, 128, std_fmt_str_m"/topology/core_cpus_list", cpu_path );
            int file = open ( file_path, O_RDONLY );
            std_assert_m ( file != -1 );
            ssize_t result = read ( file, buffer, 128 );
            std_assert_m ( result != -1 );

            size_t first = std_str_to_u32 ( buffer );
            size_t last;
            size_t separator = std_str_find ( buffer, "-" );

            if ( separator != std_str_find_null_m ) {
                last = std_str_to_u32 ( buffer + separator + 1 );
            } else {
                last = first;
            }

            uint64_t mask = 0;

            for ( size_t i = first; i <= last; ++i ) {
                std_bit_set_64 ( &mask, i );
            }

            physical_core->logical_cores_mask = mask;
            uint32_t logical_core_idx;
            bool another;

            while ( ( another = std_bit_scan_64 ( mask, &logical_core_idx ) ) ) {
                std_bit_clear_64 ( &mask, logical_core_idx );

                if ( !std_bit_test_64 ( state->logical_cores_mask, logical_core_idx ) ) {
                    std_bit_set_64 ( &state->logical_cores_mask, logical_core_idx );
                    ++state->logical_cores_count;
                }

                std_platform_logical_core_info_t* logical_core = &state->logical_cores_info[logical_core_idx];
                logical_core->physical_core_idx = physical_core_idx;
            }
        }

        for ( size_t j = 0; j < 128; ++j ) {
            char cache_path[128];
            std_str_format ( cache_path, 128, std_fmt_str_m"/cache/index"std_fmt_size_m, cpu_path, j );
            DIR* cache_dir = opendir ( cache_path );

            if ( !cache_dir ) {
                break;
            }

            // https://stackoverflow.com/questions/61454437/programmatically-get-accurate-cpu-cache-hierarchy-information-on-linux
            // TODO

            closedir ( cache_dir );
        }

        closedir ( cpu_dir );
    }

    // TODO!!! fill the rest! parse it from popen(lscpu)?
    struct sysinfo si;
    sysinfo ( &si );
    state->memory_info.virtual_page_size = getpagesize();
    state->memory_info.total_ram_size = ( size_t ) si.totalram;
    state->memory_info.total_swap_size = ( size_t ) si.totalswap;
    std_assert_m ( state->logical_cores_count == sysconf ( _SC_NPROCESSORS_ONLN ) );
#endif
}

void std_platform_attach ( std_platform_state_t* state ) {
    std_platform_state = state;
}

//==============================================================================

size_t std_platform_caches_info ( std_platform_cache_info_t* info, size_t cap ) {
    size_t count = std_platform_state->caches_count;

    if ( info == NULL ) {
        return count;
    }

    size_t min_cap = std_min ( cap, count );

    for ( size_t i = 0; i < min_cap; ++i ) {
        info[i] = std_platform_state->caches_info[i];
    }

    return count;
}

size_t std_platform_physical_cores_info ( std_platform_physical_core_info_t* info, size_t cap ) {
    size_t count = std_platform_state->physical_cores_count;

    if ( info == NULL ) {
        return count;;
    }

    size_t min_cap = std_min ( cap, count );

    for ( size_t i = 0; i < min_cap; ++i ) {
        info[i] = std_platform_state->physical_cores_info[i];
    }

    return count;
}

size_t std_platform_logical_cores_info ( std_platform_logical_core_info_t* info, size_t cap ) {
    size_t count = std_platform_state->logical_cores_count;

    if ( info == NULL ) {
        return count;
    }

    size_t min_cap = std_min ( cap, count );

    for ( size_t i = 0; i < min_cap; ++i ) {
        info[i] = std_platform_state->logical_cores_info[i];
    }

    return count;
}

std_platform_memory_info_t std_platform_memory_info ( void ) {
    return std_platform_state->memory_info;
}

//==============================================================================

std_platform_core_relationship_e std_platform_core_relationship ( uint64_t core, uint64_t other_core ) {
    if ( core == other_core ) {
        return std_platform_core_relationship_error_m;
    }

    uint64_t physical_core = std_platform_state->logical_cores_info[core].physical_core_idx;
    uint64_t other_physical_core = std_platform_state->logical_cores_info[other_core].physical_core_idx;

    if ( physical_core == other_physical_core ) {
        return std_platform_core_relationship_same_physical_core_m;
    } else {
        return std_platform_core_relationship_same_processor_m;
    }
}

//==============================================================================
