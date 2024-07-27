#pragma once

#include "xg_vk.h"

#include "xg_vk_instance.h"
#include "xg_vk_device.h"
#include "xg_vk_event.h"
#include "xg_vk_pipeline.h"
#include "xg_vk_allocator.h"
#include "xg_vk_buffer.h"
#include "xg_vk_texture.h"
#include "xg_vk_sampler.h"
#include "xg_vk_swapchain.h"
#include "xg_vk_workload.h"

typedef struct {
    xg_vk_instance_state_t instance;
    xg_vk_device_state_t device;
    xg_vk_event_state_t event;
    xg_vk_pipeline_state_t pipeline;
    xg_vk_allocator_state_t allocator;
    xg_vk_buffer_state_t buffer;
    xg_vk_texture_state_t texture;
    xg_vk_sampler_state_t sampler;
    xg_vk_swapchain_state_t swapchain;
    xg_vk_workload_state_t workload;
} xg_vk_state_t;
