#pragma once

#include <xg.h>

#include <std_list.h>
#include <std_mutex.h>
#include <std_log.h>
#include <std_hash.h>
#include <std_allocator.h>

#if defined(std_platform_win32_m)
    #define VK_USE_PLATFORM_WIN32_KHR
#elif defined(std_platform_linux_m)
    #define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <vulkan/vulkan.h>

// TODO remove these
#define xg_vk_safecall_m( f, x ) if (f != VK_SUCCESS) { std_log_error_m("Vulakn API returned an error code!"); return x; }
#define xg_vk_safecall_return_m( f, x ) if (f != VK_SUCCESS) { std_log_error_m("Vulakn API returned an error code!"); return x; }
#define xg_vk_safecall_goto_m( f, x ) if (f != VK_SUCCESS) { std_log_error_m("Vulakn API returned an error code!"); goto x; }
// TODO rename this to xg_vk_verify_m
#define xg_vk_assert_m( result ) std_verify_m ( ( result ) == VK_SUCCESS )

/*
-------------------------------------------------------------------------------

Vulkan
    Generic sources:
        http://media.steampowered.com/apps/valve/2016/Dan_Ginsburg_Source2_Vulkan_Perf_Lessons.pdf
        https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/
        https://vkguide.dev/

    Command buffers
        VkCommandBuffers are usually generated in parallel, on multiple threads. Usually one thread goes over a number of cmd buffers, one at a time,
        and owns a VkCommandPool that gets reused for all buffers processed for this frame by this thread.

        Two types of cmd buffers exist, primary and secondary. Primary buffers can be submitted to a queue and can call vkCmdBeginRenderPass/vkCmdEndRenderPass.
        Secondary buffers cannot be submitted to a queue, but they can be "attached" to a primary buffer by adding a command to the primary buffer, and that command
        will basically execute the secondary buffer. They cannot call vkCmdBeginRenderPass/vkCmdEndRenderPass and they inherit the render pass info from the primary buffer.
        A common use is to use one single primary buffer per render pass and then attach a number of secondary buffers to it.

    Descriptors
        A descriptor can be seen as a handle to an actual resource.

        Descriptors are grouped in descriptor sets which get allocated from descriptor pools. Usually each thread owns one (or more) descriptor pool per frame in flight.
        Once the frame has been fully rendered, the descriptor pool can be reused.

        When creating a pipeline, it's necessary to define a number of descriptor set layouts. Each layout is composed of multiple bindings,
        and each binding is made of a resource type (texture, buffer, ...) and the shader stages and register (glsl layout binding) that the resource will be accessible from.
        Shader registers must be unique across pipeline states, they cannot be reused per stage.

        A descriptor set is also created against a descriptor set layout. After allocation, VkWriteDescriptorSet sets which resources the descriptors bindings actually
        point to, along with additional info depending on the resource type (image view if image, offset and size if buffer, ...).

        Finally, descriptor sets can be "activated" by calling vkCmdBindDescriptorSets on a cmd buffer before registering the draw commands.

        In practice a good way to organize descriptors is to have one layout per update frequency, e.g. one layout for per-view bindings, one for per-draw bindings, ...
        This way, assuming that draw calls get sorted by update frequency, the number of descriptor sets changes can be minimized. This is because when switching
        between two pipelines, starting from set 0 if the first N sets are shared between the two pipelines they are kept bound and it's not needed to bind them again.
        This way for example all objects for a given view can be drawn in sequence and their per-view layout only needs to be bound once.
        Source: https://developer.nvidia.com/vulkan-shader-resource-binding
                https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#descriptorsets-compatibility

    Memory
        Memory is organized in memory heaps. Each heap contains uniform memory, but different heaps can contain different kinds of memory. The main basic peoperties of a memory heap are residency, visibility, and caching. Memory can either be resident on GPU or CPU memory. GPU resident memory is best performance for GPU operations, but it's not accessible from outside. The exception is CPU visible GPU memory, where the CPU can memory map the GPU memory and access it. In this case, access to GPU memory from CPU might go through a caching step, or it might not. If caching occurs, reads from CPU are generally faster. Finally, manual cache management might be required, where it's the host that invalidates and flushes its cache to update its content and publish to GPU respectively, although in practice all common current PC GPUs don't require this. Each heap is described by a combination of these properties and they determine which heap is optimal for each type of workload.
        GPU intensive work (rendering, compute, etc) typically uses plain GPU resident memory, since it's optimal and comes in great quantity. When the CPU needs to upload data to the GPU, it can either use GPU resident memory mapped memory, which is tipically fast to access but limited in size, or CPU resident memory that can later be copied to GPU resident memory by the GPU before using it for any computation. The first case is common for e.g. uplaoding the current frame's constant data, whereas
        the second is commonly used for uploading textures to the GPU. Finally, when it's the GPU that needs to send back data to the CPU, it is common to use CPU resident memory that is also cached by the CPU.
        Source: https://gpuopen.com/vulkan-device-memory/           for in-depth AMD description.
                https://asawicki.info/news_1740_vulkan_memory_types_on_pc_and_how_to_use_them
                https://therealmjp.github.io/posts/gpu-memory-pool

        Memory allocations are explicit and allocated memory can later be used for storing textures or buffers. It is strongly adviced to manually manage memory and sub-allocate. A base block size of 256MB can be a good value.
        Source: https://developer.nvidia.com/vulkan-memory-management

        There are three levels of resource management: traditional resources, sparse binding, and sparse residency. Traditional resources are those that are created in one single call and after that they can't be moved to a different memory block without having to destroy the resource and creating another one, and their memory is contiguous. Sparse binding resources are resources that are organized in "pages" that can be remapped dynamically to non contiguous blocks of memory. Before this
        resource can be used by the gpu, however, it must be fully resident, meaning that all of its pages must have a physical mapping. Sparse residency resources, on the other hand, do not have this restriction.
        Source: https://www.asawicki.info/news_1698_vulkan_sparse_binding_-_a_quick_overview.html.

    Barriers
        Barriers on GPUs are required for 3 reasons. One is an execution dependency, to make sure that a command is fully completed before starting another one.
        A second reason is cache coherency. GPUs have multiple caches that can get out of sync (e.g. the caches for sampling and for writing color buffer are separate, so before
        sampling a color buffer, a color buffer cache flush is required). Finally, a third reason is because the GPU under the hood will probably apply some sort of compression
        to things like a render target (to minimize required bandwidth when writing to it) or depth buffer and so when going from writing to it to sampling it a decompression is
        required. Source: https://mynameismjp.wordpress.com/2018/03/06/breaking-down-barriers-part-1-whats-a-barrier/.

        Vulkan pipeline barriers can do all of the above. They are always execution barriers, and on top of that can be memory barriers, or more specifically image memory barriers,
        which can also change the memory layout of the image.

        The way execution barriers work is by specifying a "source" and a "destination" stages. When the barrier gets picked up from the command buffer, it will cause all the
        commands that come after, to wait before doing work in the "destination" stage (e.g. compute) until all the commands that come before have completed their "source" stage
        (e.g. vertex shader).

        Memory barriers can be added to execution barriers and they specify another set of "source" and "destination" stages. This time "source" specifies the types of memory
        that shall be flushed from local caches to main memory (e.g. copy write from previous dispatch) and "destination" specifies the types of memory that shall invalidate and throw away their local caches, thus making main memory visible to them (e.g. shader read in next dispatch)

        Finally, layout barriers are those that can request decompression/compression of data and they can be added to image memory barriers. They take a pair of old and new image
        layouts and the transition will be performed by the GPU after the flush and before any read.
        Source: http://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/

        TODO render pass barriers
        TODO aliasing

    Events, Semaphores, Fences
        Events are an alternative way to have execution dependencies. Similarly to barriers, a set event command acts as the "source" and a wait event command acts as the
        "destination". The main difference from execution barriers is that, since events are split in 2 commands, there can be other commands in between those and those commands
        are not affected by any synchronization, so in a way it's more fine grained. Finally, events can also be set directly from CPU, rather than scheduling a GPU command for it.
        Source: http://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/

        Semaphores are also a way to handle execution dependencies but in vulkan they are passed along when submitting command buffers to a command queue. It is possible to specify
        a list of semaphores to wait for before running any of the command buffer submitted on a specific stage, and a list of semaphores to signal after completing all command
        buffers. Note that semaphores handle GPU -> GPU synchronization, it is not possible to read a semaphore state from the CPU.
        Source: https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkSubmitInfo.html

        Fences are the counterpart to semaphores, in the sense that they handle GPU -> CPU synchronization. When submitting a set of command buffers to a command queue, it is
        possible to also pass a fence, which will get signaled when all command buffers have finished executing on the GPU. The state of this fence can be read from CPU.
        Note that fences are not memory barriers. If a command list result has to be read back from the CPU, for example, it still required a memory barrier to make that memory
        visible to the CPU.
        Source: https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/vkQueueSubmit.html
                http://themaister.net/blog/2019/08/14/yet-another-blog-explaining-vulkan-synchronization/

    Pipelines and Render Passes
        Pipeline objects embed all pipeline state. They can be either compute, graphics or raytrace pipelines. Graphics pipeline state is made of input layout, primitive assembly,
        tessellation, viewport, rasterizer, msaa, depth, color blend, and also descriptors layouts and render pass (these two are just handles to externally created objects,
        not actual state).

        A render pass is made of subpasses and attachments. Attachments are resources that have the additional constain of only allowing sampling and writing at the fixed
        pixel coordinate instead of using an arbitrary UV. For example, render targets are an attachment that only writes to the current pixel coordinate. Other examples
        of attachments are depth buffers, input attachments (arbitrary input images) and msaa resolve attachments (images that specifically contain msaa related info - TODO).
        Attachments can specify in what state they can be expected to be at the beginning of the render pass and to which layout they should be transitioned to at the end of it.
        Subpasses are ordered steps that further define the render pass. Each subpass references a number of attachments specifying their use. Finally, the render pass
        lists the attachment dependencies between subpasses, implicitly specifying memory barriers.
        TODO: https://www.reddit.com/r/vulkan/comments/ags970/question_regarding_vksubpassdependency_in_sascha/

        When creating a subpass, a list of attachments is provided. When creating a framebuffer, that same list of attachments (this time in actual views, instead of definitions)
        must be provided. When beginning a render pass in a command buffer, a framebuffer having that layout must be provided.
        When extension VK_KHR_imageless_framebuffer is enabled, image views are not necessary when creating the framebuffer. Instead, they are bound dynamically at render pass
        begin time via a pointer to VkRenderPassAttachmentBeginInfoKHR in pNext.
        https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_imageless_framebuffer.html

        When creating a framebuffer or a graphics pipeline a render pass is provided. That framebuffer and that pipeline can only be bound when inside that render pass, or
        when inside one compatible with it. Two render passes are compatible when all their attachments have matching format and sample count. Image layouts and load/store ops
        can differ.
        When binding a compute or raytrace pipeline on the other hand, no render pass must be active. 
        https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#renderpass-compatibility

    Queries
        https://nikitablack.github.io/post/how_to_use_vulkan_timestamp_queries/
        Queries are instanced from a VkQueryPool object, similar to other Vulkan objects. One pool only contains queries of one type. The basic usage pattern is also similar
        to other vulkan resource pools, where you instance all queries of a given type for the current frame in one pool (or more than one, if one is not enough), reference
        that pool from the workload, and reset the pool once the workload is complete. At that point the pool can be reused.
        Once a pool is ready it's possible to write timestamp values to it by calling vkCmdWriteTimestamp. Once the workload has been completed, it is possible to readback the
        written values by calling vkGetQueryPoolResults.

    Dynamic state
        Some pipeline state can be flagged as dynamnic, meaning that the values in the pipeline state params will be *ignored*. The user is instead expected to set them
        manually before any relevant drawing command. So dynamic does not mean overridable, it means completely ignored in the provided state and left to the user at runtime.

Vulkan Raytrace

    Acceleration Structures
        To be able to HW raytrace, vulkan requires to declare and build all geometry upfront into acceleration structure vulkan resources. There are 2 types of those, top (TLAS)
        and bottom (BLAS) level. A TLAS represents a scene, or a portion of it, and a BLAS represents one or more 3D geometries. A TLAS contains a number of BLAS instances and
        a transform matrix for each of those. BLASes owns the actual geometry data, and once the BLAS is built, the geometry used as parameter for the build could be free'd.
        TODO how to handle dynamic updates

    Shader Binding Table
        A raytrace pipeline needs access to all shaders that could possibly be triggered by a ray hitting any object in the TLAS. Multiple hit or miss shaders can be provided
        at pipeline construction. The mapping from TLAS instance to shader to execute is contained in the Shader Binding Table. Specifically, it can contain three kind of items:
            Ray generation record
                Single reference to a Ray gen shader. When tracing rays, a single ray gen record has to be selected to run the ray generation for the dispatch. 
            Hit group record
                One reference to a Closest hit shader, and optionally one Any hit and one Intersection shaders. The hit group record shaders get called on ray geo intersection and 
                is dependent on the geo and can likely vary from one geo to another. The hit group record index to call is computed as follows: 
                                                        trace_offset + sbt_stride * ( instance_offset + trace_stride * geo_id )
                - trace_offset and trace_stride are specified as parameters when calling into traceRayEXT from inside a shader.
                - instance_offset is the offset specific to the BLAS instance intersected by the ray, specified at TLAS build time.
                - geo_id is the index of the intersected geometry inside the geometry array that was used at BLAS creation to build the specific BLAS that was intersected.
                - sbt_stride is the stride of the handles in the sbt, meaning that the remaining offsets are indexes and not byte values.
                Following the indexing rules, one way to lay down the hit group records is to group them in a flat 2D array by instance first, by geo second, and by ray type last.
                For example, all records belonging to an instance will be laid down together as a single segment. Then, inside that segment, first will come the records belonging
                to the first geometry that makes up the BLAS instance, then the second (if there is any) and so on. Finally, for each geo, the records will be sorted by ray type,
                for example the hit group dedicated to primary rays first, the one dedicated to occlusion rays second, and so on.
                In this case, trace_offset would effectively specify the ray type enum value that is to be dispatched and trace_stride the number of total ray types to support.
            Miss record
                Single reference to a Ray miss shader. The shader gets called when a ray fails to intersect a geometry. When calling into traceRayEXT an offset for the miss record
                to pick can be passed as parameter. In the example before, this offset too would indicate the ray type.
        In addition to shader references, each SBT record can contain additional parameters. They can be accessed from the shader by declaring a layout(shaderRecordNV) buffer. 
        Only static parameters should be stored here. Parameters taht change every frame or more should go through regular shader parameter bindings.
        Source: https://www.willusher.io/graphics/2019/11/20/the-sbt-three-ways/
                https://www.realtimerendering.com/raytracinggems/rtg2/index.html - THE SHADER BINDING TABLE DEMYSTIFIED

*/

// ----------------------------
