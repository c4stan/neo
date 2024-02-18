#pragma once

#include <xg.h>

#include "xg_vk_workload.h"

#include "xg_vk.h"

#include <std_mutex.h>

// Cmd buffer event

typedef struct {
    xg_device_h device;
    VkEvent vk_event;
} xg_vk_gpu_event_t;

xg_gpu_event_h xg_gpu_event_create ( xg_device_h device );
void xg_gpu_event_destroy ( xg_gpu_event_h handle );
const xg_vk_gpu_event_t* xg_vk_gpu_event_get ( xg_gpu_event_h handle );

// Queue events (semaphores and fences)
// Not visible from outside of xg_vk
// TODO prefix these with xg_vk ?

/*
    swapchain texture acquire:  vkAcquireNextImageKHR -> vkQueueSubmit
    gpu execution complete:     vkQueueSubmit -> vkQueuePresentKHR
        seems like Vulkan doesn't offer a way to wait on the Present call to "finish", while requiring
        the gpu execution complete semaphore to remain alive until then. See https://github.com/KhronosGroup/Vulkan-Docs/issues/152
        current solution to this is to never kill the semaphore used that way, instead have one per
        swapchain texture and reuse them when the same texture gets acquired again by the acquire call
*/
typedef struct {
    xg_device_h device;
    VkSemaphore vk_semaphore;
} xg_vk_gpu_queue_event_t;

typedef uint64_t xg_gpu_queue_event_h;

xg_gpu_queue_event_h xg_gpu_queue_event_create ( xg_device_h device );
void xg_gpu_queue_event_destroy ( xg_gpu_queue_event_h handle );
const xg_vk_gpu_queue_event_t* xg_vk_gpu_queue_event_get ( xg_gpu_queue_event_h handle );
void xg_gpu_queue_event_log_wait ( xg_gpu_queue_event_h handle );
void xg_gpu_queue_event_log_signal ( xg_gpu_queue_event_h handle );

/*
    gpu execution complete:     vkQueueSubmit -> vkWaitForFences
*/
typedef struct {
    xg_device_h device;
    VkFence vk_fence;
} xg_vk_cpu_queue_event_t;

typedef uint64_t xg_cpu_queue_event_h;

xg_cpu_queue_event_h xg_cpu_queue_event_create ( xg_device_h device );
void xg_cpu_queue_event_destroy ( xg_cpu_queue_event_h handle );
const xg_vk_cpu_queue_event_t* xg_vk_cpu_queue_event_get ( xg_cpu_queue_event_h handle );

/*
    acquire image from swapchain
        signal acquire sema
    submit work to queue
        wait on acquire sema
        signal on execution sema
        signal on execution fence
    present queue
        wait on execution sema

    recycle submission context
        check execution fence
        delete semaphores
*/

typedef struct {
    std_memory_h gpu_events_memory_handle;
    xg_vk_gpu_event_t* gpu_events_array;
    xg_vk_gpu_event_t* gpu_events_freelist;
    std_mutex_t gpu_events_mutex;

    std_memory_h gpu_queue_events_memory_handle;
    xg_vk_gpu_queue_event_t* gpu_queue_events_array;
    xg_vk_gpu_queue_event_t* gpu_queue_events_freelist;
    std_mutex_t gpu_queue_events_mutex;

    std_memory_h cpu_queue_events_memory_handle;
    xg_vk_cpu_queue_event_t* cpu_queue_events_array;
    xg_vk_cpu_queue_event_t* cpu_queue_events_freelist;
    std_mutex_t cpu_queue_events_mutex;
} xg_vk_event_state_t;

void xg_vk_event_load ( xg_vk_event_state_t* state );
void xg_vk_event_reload ( xg_vk_event_state_t* state );
void xg_vk_event_unload ( void );
