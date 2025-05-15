struct light_cluster_t {
    vec4 min;
    vec4 max;
};

struct light_grid_t {
    uint offset;
    uint count;
};

struct light_t {
    vec3 pos;
    float radius;
    vec3 color;
    float emissive;
    mat4 proj_from_view;
    mat4 view_from_world;
    vec3 shadow_tile; // x y size
    uint type; // unused
};
