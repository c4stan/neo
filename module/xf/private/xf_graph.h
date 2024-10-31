#include <xf.h>

#include <std_hash.h>

/*
    Full rebuild every frame vs rebuild on user request/fraph change only?
        Every frame probably makes the most sense
        Compute workloads in particular can vary significantly between frames
        Makes it easier to schedule occasional setup/one-frame work
        One alternative could be allowing to turn on-off sub-graph or sections of the graph and rebuild onlu what's affected by that
            Likely to get quite complex, and not sure if the gain would be significant (or any)
        Build on every frame first, once that's working evaluate alternatives later

    Nodes as semi stand-alone things that one can reuse and treat like prefabs, vs simple graph-centric nodes management
        Would be cool if I could get a "SSAO node" "prefab" declaration from somewhere and just plug that into my graph
        In practice that means providing the all the enums for slotting the resources in the node params, the user callback and the user args declaration.
        Doesn't really require any special treatment, comes by default.

    Separete create_node and add_node_to_graph calls or all in one?
        In theory having a separate add_node_to_graph means that a node can be part of multiple graphs, meaning that its dependencies can come from multiple
        graphs, meaning that I can't look only inside the graph when determining dependencies but instead must take into consideration all other existing nodes
        Currently the api only has a create_node call, best to keep it that way for now, perhaps expand later.
*/

/*
    Resources
        https://logins.github.io/graphics/2021/05/31/RenderGraphs.html
        https://levelup.gitconnected.com/organizing-gpu-work-with-directed-acyclic-graphs-f3fd5f2c2af3
        https://docs.unrealengine.com/4.26/en-US/ProgrammingAndScripting/Rendering/RenderDependencyGraph
        https://www.gdcvault.com/play/1024656/Advanced-Graphics-Tech-Moving-to
        https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive
        https://apoorvaj.io/render-graphs-1

        https://www.jeremyong.com/rendering/2019/06/28/render-graph-optimization-scribbles/

    Node Dependencies
        In order to linearize the graph it's necessary to go from implicit node dependencies (e.g. resource usage) to explicit (e.g. node A depends on node B).
        One way is to start from a 'root' node, and from there recursively define explicit dependencies based on resource usage, where reads depend on previous writes.
        This works as long as there's only one writer per resource. This is rather limiting, and simple situations of write -> read -> write on same resource are
        no longer allowed. Same for multiple nodes using the same render target.
        -One solution to this limitations can be in the form of adding explicit dependencies between nodes, not based on resources, that help solve the case of
        multiple writes, assigning an arbitrary ordering.
        -Another solution is to use multiple user-level resources to refer to the same physical resource. This can work for a write -> read -> write on same resource,
        by simply doing the second write on a new resource and trusting the backend to reuse the same resource. Cases where some of the previous content must be
        preserved need to be explicitly declared however, for example a write -> write on the same resource, where each write acts on a subregion.
        -Another solution is to use the order in which the nodes are added to the graph as a baseline. In this scenario the order of a w->r->w or even a w->w is simply
        determined by the order in which their respective nodes were added to the graph. Partial reordering is still possible to maximize distance between
        dependencies.
        Solution: follow nodes declaration order

    Node execute order vs gpu command submission order
        Nodes get sorted based on their dependencies, but that is only needed in order to determine the sort keys passed to the node callbacks.
        Once the sort has happened, the execute step can iterate over all nodes and spawn a task for each execute, accumulating each node key space on the sort key
        that gets passed as param. Node callbacks can then record commands in parallel using keys in the sort key space they declared, with the param key as base, and
        the xg backend can do the sort later of the commands based on keys.
        For node callbacks to be executed in parallel, it must be required that the callbacks don't modify any state that's shared between the callbacks. This includes
        xg resources state. It follows that it might be best for xg cmd buffer to have an API that's separate from the xg one, and only provide the cmd buffer api
        to the callback.
*/

typedef struct xf_node_t {
    xf_node_params_t params;
    xf_node_h edges[xf_node_max_node_edges_m]; // outgoing dependency edges
    uint32_t edge_count;
    void* user_alloc;
    bool enabled;
    //tk_workload_h cpu_workload;
    xg_renderpass_h renderpass;
    struct {
        xg_render_textures_layout_t render_textures;
        uint32_t resolution_x;
        uint32_t resolution_y;
    } renderpass_params;
} xf_node_t;

// TODO move out
#define xf_graph_max_swapchain_multi_textures_per_graph_m 4

typedef struct {
    xg_device_h device;
    //xf_node_t* nodes[xf_graph_max_nodes_m];
    xf_node_h nodes[xf_graph_max_nodes_m];
    size_t nodes_execution_order[xf_graph_max_nodes_m]; // indexes nodes, sorted by execution order
    size_t nodes_count;
    
    xf_texture_h multi_textures_array[xf_graph_max_multi_textures_per_graph_m];
    uint64_t multi_textures_count;
    uint64_t multi_textures_hashes[xf_graph_max_multi_textures_per_graph_m];
    std_hash_set_t multi_textures_hash_set;
} xf_graph_t;

typedef struct {
    xf_graph_t* graphs_array;
    xf_graph_t* graphs_freelist;

    xf_node_t* nodes_array;
    xf_node_t* nodes_freelist;
} xf_graph_state_t;

void xf_graph_load ( xf_graph_state_t* state );
void xf_graph_reload ( xf_graph_state_t* state );
void xf_graph_unload ( void );

xf_graph_h xf_graph_create ( xg_device_h device );
xf_node_h xf_graph_add_node ( xf_graph_h graph, const xf_node_params_t* params );
void xf_graph_clear ( xf_graph_h graph );
void xf_graph_build2 ( xf_graph_h graph, xg_workload_h xg_workload );
uint64_t xf_graph_execute ( xf_graph_h graph, xg_workload_h xg_workload, uint64_t base_key );
void xf_graph_advance_multi_textures ( xf_graph_h graph );
void xf_graph_destroy ( xf_graph_h graph );

void xf_graph_get_info ( xf_graph_info_t* info, xf_graph_h graph );
void xf_graph_get_node_info ( xf_node_info_t* info, xf_node_h node );

void xf_graph_debug_print ( xf_graph_h graph );

void xf_graph_node_set_enabled ( xf_node_h node, bool enabled );
void xf_graph_node_enable ( xf_node_h node );
void xf_graph_node_disable ( xf_node_h node );
