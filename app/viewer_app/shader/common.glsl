#include <xs.glsl>

#define PI 3.1415f
#define REVERSE_Z 0 // TODO

#define print debugPrintfEXT

// ======================================================================================= //
//                                     U N I F O R M S
// ======================================================================================= //

layout ( binding = 0, set = xs_shader_binding_set_workload_m ) uniform frame_uniforms_t {
    vec2 resolution_f32;
    uvec2 resolution_u32;
    uint frame_id; // TODO enable GL_ARB_gpu_shader_int64 and use uint64_t
    float time_ms;
    uvec2 _pad0;

    mat4 view_from_world;
    mat4 proj_from_view;
    mat4 jittered_proj_from_view;
    mat4 view_from_proj;
    mat4 world_from_view;
    mat4 prev_view_from_world;
    mat4 prev_proj_from_view;
    float z_near;
    float z_far;

    uint is_reload;
} frame_uniforms;

// ======================================================================================= //
//                                     R A Y T R A C E
// ======================================================================================= //

struct ray_payload_t {
    vec3 color;
    float distance;
    vec3 normal;
    vec3 emissive;
};

struct shadow_ray_payload_t {
    float distance;
};

// ======================================================================================= //
//                                         M A T H
// ======================================================================================= //

vec3 vec3_from_spherical ( float theta, float phi ) {
    float x = sin ( theta ) * cos ( phi );
    float y = cos ( theta );
    float z = sin ( theta ) * sin ( phi );
    return vec3 ( x, y, z );
}

vec3 tangent_from_normal ( vec3 normal ) {
    vec3 tangent;

    if ( abs ( normal.x ) > abs ( normal.y ) ) {
        tangent = vec3 ( normal.z, 0.f, -normal.x );
        tangent *= sqrt ( normal.x * normal.x + normal.z * normal.z );
    } else {
        tangent = vec3 ( 0.f, -normal.z, normal.y );
        tangent *= sqrt ( normal.y * normal.y + normal.z * normal.z );
    }

    return tangent;
}

mat3 tnb_from_normal ( vec3 n ) {
    vec3 t = tangent_from_normal ( n );
    vec3 b = cross ( n, t );
    return mat3 ( t, n, b );
}

mat3 tbn_from_normal ( vec3 n ) {
    vec3 t = tangent_from_normal ( n );
    vec3 b = cross ( n, t );
    return mat3 ( t, b, n );
}

bool proj_depth_cmp_ge ( float a, float b ) {
#if reverse_depth_m
    return a <= b;
#else
    return a >= b;
#endif
}

bool proj_depth_cmp_gt ( float a, float b ) {
#if reverse_depth_m
    return a < b;
#else
    return a > b;
#endif
}

float proj_depth_diff ( float a, float b ) {
#if reverse_depth_m
    return a - b;
#else
    return b - a;
#endif
}

// ======================================================================================= //
//                             T R A N S F O R M   S P A C E S
// ======================================================================================= //

/*
    model:      vbuffer pos.
    world:      world_matrix * model. transform applied pos.
    view:       view_matrix * world. camera is at origin.
    proj/clip:  proj_matrix * view. pre w divide.
    ndc:        post w divide proj pos.
    screen/uv:  ndc remapped from [-1,1] to [0,1].
    viewport:   screen remapped to pixel coordinates via viewport transform.
*/

// https://stackoverflow.com/questions/51108596/linearize-depth
float linearize_depth ( float d ) {
    float z_near = frame_uniforms.z_near;
    float z_far = frame_uniforms.z_far;
    return z_near * z_far / ( z_far + d * ( z_near - z_far ) );
}

// https://mynameismjp.wordpress.com/2009/03/10/reconstructing-position-from-depth/
vec3 view_from_depth ( vec2 screen_uv, float depth ) {
    float proj_x = screen_uv.x * 2 - 1;
    float proj_y = ( 1 - screen_uv.y ) * 2 - 1;
    vec3 proj_pos = vec3 ( proj_x, proj_y, depth );
    vec4 unnormalized_view_pos = frame_uniforms.view_from_proj * vec4 ( proj_pos, 1 );
    vec3 view_pos = unnormalized_view_pos.xyz / unnormalized_view_pos.w;
    return view_pos;
}

vec3 screen_from_view ( mat4 proj_from_view, vec3 view ) {
    vec4 proj = proj_from_view * vec4 ( view, 1 );
    proj /= proj.w;
    vec3 screen = vec3 ( proj.xy * vec2 ( 0.5, -0.5 ) + 0.5, proj.z );
    return screen;
}

vec3 view_from_screen ( vec3 screen ) {
    return view_from_depth ( screen.xy, screen.z );
}

// https://mynameismjp.wordpress.com/the-museum/samples-tutorials-tools/motion-blur-sample/
vec3 prev_screen_from_world ( vec3 world ) {
    vec3 prev_view = ( frame_uniforms.prev_view_from_world * vec4 ( world, 1 ) ).xyz;
    vec4 prev_proj = ( frame_uniforms.prev_proj_from_view * vec4 ( prev_view, 1 ) );
    prev_proj /= prev_proj.w;
    vec3 screen = vec3 ( prev_proj.xy * vec2 ( 0.5, -0.5 ) + 0.5, prev_proj.z );
    return screen;
}

// takes in post-divide by w coords, before moving from NDC to screen
vec2 dejitter_ndc ( vec2 ndc ) {
    vec2 jitter = vec2 ( frame_uniforms.jittered_proj_from_view[2][0], frame_uniforms.jittered_proj_from_view[2][1] );
    return ndc - jitter;
}

vec2 dejitter_uv ( vec2 screen_uv ) {
#if 1
    float proj_x = screen_uv.x * 2 - 1;
    float proj_y = ( 1 - screen_uv.y ) * 2 - 1;
    vec2 ndc = vec2 ( proj_x, proj_y );
    vec2 dejittered_ndc = dejitter_ndc ( ndc );
    vec2 dejittered_uv = dejittered_ndc * vec2 ( 0.5, -0.5 ) + 0.5;
    return dejittered_uv;
#else
    vec2 jitter = vec2 ( frame_uniforms.jittered_proj_from_view[2][0], frame_uniforms.jittered_proj_from_view[2][1] );
    vec2 jitter_uv = jitter * vec2 ( 0.5f, -0.5f );
    return screen_uv - jitter_uv;
#endif
}

//vec2 dejittered_screen_uv() { 
//    vec2 screen_uv = vec2 ( gl_FragCoord.xy / frame_uniforms.resolution_f32 );
//    screen_uv = dejitter_uv ( screen_uv );
//    return screen_uv;
//}

// ======================================================================================= //
//                                 C O L O R   S P A C E S
// ======================================================================================= //

vec3 linear_to_srgb ( vec3 color ) {
    return pow ( color, vec3 ( 1.0 / 2.2 ) );
}

float rgb_to_luma ( vec3 color ) {
    return dot ( color, vec3 ( 0.2126f, 0.7152f, 0.0722f ) );
}

// ======================================================================================= //
//                              T E X T U R E   F I L T E R S
// ======================================================================================= //
// https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
vec4 sample_catmull_rom ( texture2D tex2d, sampler sampler_linear, vec2 uv, vec2 tex_size )
{
    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    vec2 uv_tex = uv * tex_size;
    vec2 tex1 = floor ( uv_tex - 0.5f ) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    vec2 f = uv_tex - tex1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    vec2 w0 = f * ( -0.5f + f * ( 1.0f - 0.5f * f ) );
    vec2 w1 = 1.0f + f * f * ( -2.5f + 1.5f * f );
    vec2 w2 = f * ( 0.5f + f * ( 2.0f - 1.5f * f ) );
    vec2 w3 = f * f * ( -0.5f + 0.5f * f );

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    vec2 w12 = w1 + w2;
    vec2 offset12 = w2 / ( w1 + w2 );

    // Compute the final UV coordinates we'll use for sampling the texture
    vec2 tex0 = tex1 - 1;
    vec2 tex3 = tex1 + 2;
    vec2 tex12 = tex1 + offset12;

    tex0 /= tex_size;
    tex3 /= tex_size;
    tex12 /= tex_size;

    vec4 result = vec4 ( 0.0f );
    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex0.x, tex0.y ) ) * w0.x * w0.y;
    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex12.x, tex0.y ) ) * w12.x * w0.y;
    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex3.x, tex0.y ) ) * w3.x * w0.y;

    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex0.x, tex12.y ) ) * w0.x * w12.y;
    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex12.x, tex12.y ) ) * w12.x * w12.y;
    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex3.x, tex12.y ) ) * w3.x * w12.y;

    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex0.x, tex3.y ) ) * w0.x * w3.y;
    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex12.x, tex3.y ) ) * w12.x * w3.y;
    result += texture ( sampler2D ( tex2d, sampler_linear ), vec2 ( tex3.x, tex3.y ) ) * w3.x * w3.y;

    return result;
}

// ======================================================================================= //
//                              R A N D O M   S A M P L I N G
// ======================================================================================= //

// https://blog.demofox.org/2020/05/
struct rng_wang_state_t {
    uint v;
};

float wang_hash ( inout uint seed ) {
    seed = uint ( seed ^ uint ( 61 ) ) ^ uint ( seed >> uint ( 16 ) );
    seed *= uint ( 9 );
    seed = seed ^ ( seed >> 4 );
    seed *= uint ( 0x27d4eb2d );
    seed = seed ^ ( seed >> 15 );
    return seed;
}

float rng_wang ( inout rng_wang_state_t state ) {
    return float ( wang_hash ( state.v ) ) / 4294967296.0;
}

rng_wang_state_t rng_wang_init ( vec2 tex_coord ) {
    uint v = uint ( uint ( tex_coord.x ) * uint ( 1973 ) + uint ( tex_coord.y ) * uint ( 9277 ) + uint ( frame_uniforms.frame_id ) * uint ( 26699 ) ) | uint ( 1 );
    rng_wang_state_t state;
    state.v = v;
    return state;
}

// https://mathworld.wolfram.com/SpherePointPicking.html
vec3 random_unit_vector ( vec2 e ) {
    float theta = e.y * 3.1415f * 2.f;
    // phi = cos^-1 ( e.x * 2 - 1 )
    // u = cos ( phi )
    float u = e.x * 2.f - 1.f;
    float r = sqrt ( 1.f - u * u );
    float x = r * cos ( theta );
    float y = u;
    float z = r * sin ( theta );
    return vec3 ( x, y, z );
}

// https://alexanderameye.github.io/notes/sampling-the-hemisphere/
vec3 sample_cosine_weighted_hemisphere_normal ( vec2 e, vec3 normal ) {
#if 0
    normal = normalize ( normal );
    vec3 tangent = generate_tangent ( normal );
    vec3 bitangent = normalize ( cross ( normal, tangent ) );

    float r = sqrt ( e.x );
    float phi = 2.f * 3.1415f * e.y;
    float x = r * cos ( phi );
    float y = sqrt ( 1.f - e.x );
    float z = r * sin ( phi );

    vec3 wi = vec3 ( x, y, z );
    mat3 m = mat3 ( tangent, normal, bitangent );
    wi = m * wi;
    return normalize ( wi );
#else
    return normalize ( normal + random_unit_vector ( e )  );
#endif
}

// ======================================================================================= //
//                                          G G X
// ======================================================================================= //

float ggx_d ( float NoH, float roughness ) {
    float den = NoH * NoH * ( roughness * roughness - 1 ) + 1;
    return ( roughness * roughness ) / ( PI * den * den );
}

// takes in wo (view vector V)
// returns wi (light vector L)

float ggx_pdf ( vec3 wi, vec3 wo, vec3 normal, float roughness ) {
    vec3 wh = normalize ( wi + wo );
    float NoH = max ( 0, dot ( normal, wh ) );
    float d = ggx_d ( NoH, roughness );
    float HoV = max ( 0, dot (wh, wo ) );
    return ( d * NoH ) / ( 4.f * HoV );
}

// ======================================================================================= //
//                     S C R E E N   S P A C E   R A Y   T R A C I N G
// ======================================================================================= //
//
// GPU Pro 5 p.149 - Hi-Z Screen-Space Cone-Traced Reflections
// https://sugulee.wordpress.com/2021/01/16/performance-optimizations-for-screen-space-reflections-technique-part-1-linear-tracing-method/
// https://sugulee.wordpress.com/2021/01/19/screen-space-reflections-implementation-and-optimization-part-2-hi-z-tracing-method/
// https://bitsquid.blogspot.com/2017/08/notes-on-screen-space-hiz-tracing.html
//
// TODO wrap return values in a struct?
//

#if 1
// steps forward the ray sample until it exits the hi-z mip cell it is currently in and goes a bit further (controlled by cross_offset)
// returns the ray path t value for the new sample position
float intersect_cell_boundary ( vec3 screen_ray_start, vec3 screen_ray_path, vec3 screen_ray_sample, uint hiz_mip, vec2 cross_step, vec2 cross_offset ) {
    vec2 hiz_resolution = vec2 ( frame_uniforms.resolution_u32 >> hiz_mip );
    // index of the next cell in ray dir if dir is positive, or the current cell if dir is negative
    vec2 cell_index = floor ( screen_ray_sample.xy * hiz_resolution ) + cross_step;
    // get screen coords for the vertical and horizontal planes defining the cell boundary we want to cross
    vec2 screen_cell_boundary = cell_index / hiz_resolution;
    // compute intersections and cross the boundaries using cross_offset, return closest one
    vec2 solutions = ( screen_cell_boundary + cross_offset - screen_ray_start.xy ) / screen_ray_path.xy;
    float t = min ( solutions.x, solutions.y );
    return t;
}

bool trace_screen_space_ray ( out vec3 out_screen_pos, out float out_depth, vec3 view_pos, vec3 hemisphere_normal, texture2D tex_hiz, uint hiz_mip_count, sampler sampler_point, uint max_sample_count ) {
    //
    // :: go into screen space ::
    //
    vec3 view_ray_start = view_pos;
    vec3 screen_ray_start = screen_from_view ( frame_uniforms.jittered_proj_from_view, view_ray_start );
    //screen_ray_start.y += 1.f / frame_uniforms.resolution_f32.y;
    vec3 screen_ray_hemisphere_sample = screen_from_view ( frame_uniforms.jittered_proj_from_view, view_ray_start + hemisphere_normal );
    vec3 screen_ray_dir = normalize ( screen_ray_hemisphere_sample - screen_ray_start );

    //
    // :: compute ray screen edges ::
    //
    float screen_max_trace_x = ( ( screen_ray_dir.x < 0.f ? 0.f : 1.f ) - screen_ray_start.x ) / screen_ray_dir.x;
    float screen_max_trace_y = ( ( screen_ray_dir.y < 0.f ? 0.f : 1.f ) - screen_ray_start.y ) / screen_ray_dir.y;
    float screen_max_trace_z = ( ( screen_ray_dir.z < 0.f ? 0.f : 1.f ) - screen_ray_start.z ) / screen_ray_dir.z;
    float screen_max_trace_distance = min ( min ( screen_max_trace_x, screen_max_trace_y ), screen_max_trace_z );

    //
    // :: precompute hi-z cell crossing values ::
    //
    uint hiz_max_mip = hiz_mip_count - 1;
    vec2 cross_dir = vec2 ( screen_ray_dir.x > 0.f ? 1.f : -1.f, screen_ray_dir.y > 0.f ? 1.f : -1.f );
    vec2 cross_offset = cross_dir * ( 1.f / float ( frame_uniforms.resolution_u32 << hiz_max_mip ) );// * 0.5f );
    vec2 cross_step = clamp ( cross_dir, 0, 1 );

    //
    // :: ray setup ::
    //
    // compute screen ray path and step forward once
    // t goes from 0 to 1, from ray origin to ray intersection with screen space limits
    uint hiz_mip = 0;
    vec3 screen_ray_path = screen_ray_dir * screen_max_trace_distance;
    float t = intersect_cell_boundary ( screen_ray_start, screen_ray_path, screen_ray_start, hiz_mip, cross_step, cross_offset );
    vec3 screen_ray_sample = screen_ray_start + screen_ray_path * t;

    //
    // :: ray trace ::
    //
    // trace the ray
    bool screen_ray_is_backward = screen_ray_dir.z < 0;

    uint sample_it = 0;
    float depth_threshold = 0.0001;

    while ( t > 0.f && t < 1.f && sample_it < max_sample_count ) {
        //
        // :: prepare ::
        //
        // sample hiz at current mip and ray screen coord
        float sample_depth = textureLod ( sampler2D ( tex_hiz, sampler_point ), screen_ray_sample.xy, hiz_mip ).x;

        // compute ray cell index for current hiz mip
        vec2 hiz_resolution = vec2 ( frame_uniforms.resolution_u32 >> hiz_mip );
        vec2 cell_index = floor ( screen_ray_sample.xy * hiz_resolution );

        //
        // :: depth step ::
        //
        // use the sampled depth to move along the ray path
        vec3 screen_ray_sample_depth_step = screen_ray_sample;
        float depth_delta = ( sample_depth - screen_ray_sample.z ); // delta from the ray z to the sampled depth. positive if the sample is in front (not occluded) by the depth.

        if ( depth_delta > 0 && !screen_ray_is_backward ) {
            float depth_t = ( sample_depth - screen_ray_start.z ) / ( screen_ray_path.z );
            screen_ray_sample_depth_step = screen_ray_start + screen_ray_path * depth_t;
        }

        vec2 depth_step_cell_idx = floor ( screen_ray_sample_depth_step.xy * hiz_resolution );

        //
        // :: resolve ::
        //
        if ( 
               ( cell_index != depth_step_cell_idx )                        // check if we crossed the current hiz cell by depth stepping
            || ( screen_ray_is_backward && depth_delta > 0 )                // if we're going backward, check if there's nothing in the current cell that we can possibly collide with
            //|| ( hiz_mip == 0 && abs ( depth_delta ) > depth_threshold )  // TODO why the abs?
            || ( hiz_mip == 0 && depth_delta < -depth_threshold )           // check if we're behind a surface and it's depth threshold (ignore the collision in that case)
        ) {
            // continue tracing
            // depth step failed, do a cell boundary step
            // move the ray up to the current cell boundary and go up a mip
            t = intersect_cell_boundary ( screen_ray_start, screen_ray_path, screen_ray_sample, hiz_mip, cross_step, cross_offset );
            screen_ray_sample = screen_ray_start + screen_ray_path * t;
            hiz_mip = min ( hiz_mip + 2, hiz_max_mip );
        } else {
            // depth stepping hit a surface
            // if we're not at mip 0 go down a mip and trace again
            // if we are at mip 0 end the trace
            screen_ray_sample = screen_ray_sample_depth_step;

            if ( hiz_mip > 0 ) {
                hiz_mip -= 1;
            } else {
                out_screen_pos = screen_ray_sample;
                out_depth = sample_depth;
                return true;
            }
        }

        ++sample_it;
    }

    return false;
}
#else

#define HIZ_START_LEVEL 0
#define HIZ_STOP_LEVEL 0
#define HIZ_MAX_LEVEL 8
#define MAX_ITERATIONS max_sample_count

vec2 cell ( vec2 ray, vec2 cell_count, uint camera ) {
    return floor ( ray.xy * cell_count );
}

vec2 cell_count ( float level ) {
    return frame_uniforms.resolution_f32 / ( level == 0.0 ? 1.0 : exp2 ( level ) );
}

vec3 intersect_cell_boundary ( vec3 pos, vec3 dir, vec2 cell_id, vec2 cell_count, vec2 cross_step, vec2 cross_offset, uint camera ) {
    vec2 cell_size = 1.0 / cell_count;
    vec2 planes = cell_id / cell_count + cell_size * cross_step;

    vec2 solutions = ( planes - pos.xy ) / dir.xy;
    vec3 intersection_pos = pos + dir * min ( solutions.x, solutions.y );

    intersection_pos.xy += ( solutions.x < solutions.y ) ? vec2 ( cross_offset.x, 0.0 ) : vec2 ( 0.0, cross_offset.y );

    return intersection_pos;
}

bool crossed_cell_boundary ( vec2 cell_id_one, vec2 cell_id_two ) {
    return int ( cell_id_one.x ) != int ( cell_id_two.x ) || int ( cell_id_one.y ) != int ( cell_id_two.y );
}

float minimum_depth_plane ( vec2 ray, float level, vec2 cell_count, texture2D tex_hiz, sampler sampler_point ) {
    return textureLod ( sampler2D ( tex_hiz, sampler_point ), ray.xy, level ).x;
}

//vec3 hi_z_trace ( vec3 p, vec3 v, in uint camera, out uint iterations ) {
bool trace_screen_space_ray ( out vec3 out_screen_pos, out float out_depth, vec3 view_pos, vec3 hemisphere_normal, texture2D tex_hiz, uint hiz_mip_count, sampler sampler_point, uint max_sample_count ) {

    vec3 p = screen_from_view ( view_pos );
    vec3 v = normalize ( screen_from_view ( view_pos + hemisphere_normal ) - p );

    float level = HIZ_START_LEVEL;
    vec3 v_z = v / v.z;
    vec2 hi_z_size = cell_count ( level );
    vec3 ray = p;

    vec2 cross_step = vec2 ( v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0 );
    vec2 cross_offset = cross_step * 0.00001;
    cross_step = clamp ( cross_step, 0.0, 1.0 );

    uint camera = 0;
    vec2 ray_cell = cell ( ray.xy, hi_z_size.xy, camera );
    ray = intersect_cell_boundary ( ray, v, ray_cell, hi_z_size, cross_step, cross_offset, camera );

    uint iterations = 0;

    float min_z = 0;

    while ( level >= HIZ_STOP_LEVEL && iterations < MAX_ITERATIONS ) {
        // get the cell number of the current ray
        vec2 current_cell_count = cell_count ( level );
        vec2 old_cell_id = cell ( ray.xy, current_cell_count, camera );

        // get the minimum depth plane in which the current ray resides
        min_z = minimum_depth_plane ( ray.xy, level, current_cell_count, tex_hiz, sampler_point );

        // intersect only if ray depth is below the minimum depth plane
        vec3 tmp_ray = ray;

        if ( v.z > 0 ) {
            float min_minus_ray = min_z - ray.z;
            tmp_ray = min_minus_ray > 0 ? ray + v_z * min_minus_ray : tmp_ray;
            vec2 new_cell_id = cell ( tmp_ray.xy, current_cell_count, camera );

            if ( crossed_cell_boundary ( old_cell_id, new_cell_id ) ) {
                tmp_ray = intersect_cell_boundary ( ray, v, old_cell_id, current_cell_count, cross_step, cross_offset, camera );
                level = min ( HIZ_MAX_LEVEL, level + 2.0f );
            } else {
                if ( level == 1 && abs ( min_minus_ray ) > 0.0001 ) {
                    tmp_ray = intersect_cell_boundary ( ray, v, old_cell_id, current_cell_count, cross_step, cross_offset, camera );
                    level = 2;
                }
            }
        } else if ( ray.z < min_z ) {
            tmp_ray = intersect_cell_boundary ( ray, v, old_cell_id, current_cell_count, cross_step, cross_offset, camera );
            level = min ( HIZ_MAX_LEVEL, level + 2.0f );
        }

        ray.xyz = tmp_ray.xyz;
        --level;

        ++iterations;
    }

    out_screen_pos = ray;
    out_depth = min_z;
    return true;
}
#endif

bool trace_screen_space_ray_linear ( out vec3 out_screen_pos, out float out_depth, vec3 view_pos, vec3 hemisphere_normal, texture2D tex_depth, sampler sampler_point, uint max_sample_count, uint step_size ) {
    // go into screen space
    vec3 view_ray_start = view_pos;
    vec3 screen_ray_start = screen_from_view ( frame_uniforms.jittered_proj_from_view, view_ray_start );
    vec3 screen_ray_hemisphere_sample = screen_from_view ( frame_uniforms.jittered_proj_from_view, view_ray_start + hemisphere_normal );
    vec3 screen_ray_dir = normalize ( screen_ray_hemisphere_sample - screen_ray_start );

    // find screen space ray end and length
    float screen_max_trace_x = ( ( screen_ray_dir.x < 0.f ? 0.f : 1.f ) - screen_ray_start.x ) / screen_ray_dir.x;
    float screen_max_trace_y = ( ( screen_ray_dir.y < 0.f ? 0.f : 1.f ) - screen_ray_start.y ) / screen_ray_dir.y;
    float screen_max_trace_z = ( ( screen_ray_dir.z < 0.f ? 0.f : 1.f ) - screen_ray_start.z ) / screen_ray_dir.z;
    float screen_max_trace_distance = min ( min ( screen_max_trace_x, screen_max_trace_y ), screen_max_trace_z );
    vec3 screen_ray_end = screen_ray_start + screen_ray_dir * screen_max_trace_distance;
    vec3 screen_ray_length = screen_ray_end - screen_ray_start;

    // find screen space step size
    ivec2 tex_ray_start = ivec2 ( screen_ray_start.xy * frame_uniforms.resolution_u32 );
    ivec2 tex_ray_end = ivec2 ( screen_ray_end.xy * frame_uniforms.resolution_u32 );
    ivec2 tex_ray_length = tex_ray_end - tex_ray_start;
    uint tex_ray_max_dist = max ( abs ( tex_ray_length.x ), abs ( tex_ray_length.y ) );
    vec3 screen_ray_step = screen_ray_length / tex_ray_max_dist;
    uint max_tex_samples = tex_ray_max_dist / step_size;

    // trace the ray
    uint sample_steps = step_size;
    vec3 screen_ray_sample = screen_ray_start + screen_ray_step * sample_steps;
    float depth_threshold = 0.0001 * sample_steps;

    for ( uint sample_it = 0; sample_it < max_tex_samples && sample_it < max_sample_count; ++sample_it ) {
        float sample_depth = texture ( sampler2D ( tex_depth, sampler_point ), screen_ray_sample.xy ).x;

        float depth_delta = sample_depth - screen_ray_sample.z;

        if ( depth_delta < 0 ) {
            if ( depth_delta > -depth_threshold ) {
                out_screen_pos = screen_ray_sample;
                out_depth = sample_depth;
                return true;
            } else {
                return false;
            }
        }

        screen_ray_sample += screen_ray_step * sample_steps;
    }

    return false;
}
