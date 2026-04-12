//
// https://mrl.cs.nyu.edu/~perlin/noise/
//
float perlin_fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
float perlin_lerp(float t, float a, float b) { return mix(a, b, t); /*a + t * (b - a);*/ }
float perlin_grad(int hash, float x, float y, float z) {
  int h = hash & 15;                      // CONVERT LO 4 BITS OF HASH CODE
  float u = h<8 ? x : y,                  // INTO 12 GRADIENT DIRECTIONS.
        v = h<4 ? y : h==12||h==14 ? x : z;
  return ((h&1) == 0 ? u : -u) + ((h&2) == 0 ? v : -v);
}
float perlin_noise(float x, float y, float z) {
    int p[] = { 151,160,137,91,90,15,
        131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
        190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
        88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
        77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
        102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
        135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
        5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
        223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
        129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
        251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
        49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
        138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
        151,160,137,91,90,15,
        131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,21,10,23,
        190, 6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,
        88,237,149,56,87,174,20,125,136,171,168, 68,175,74,165,71,134,139,48,27,166,
        77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,
        102,143,54, 65,25,63,161, 1,216,80,73,209,76,132,187,208, 89,18,169,200,196,
        135,130,116,188,159,86,164,100,109,198,173,186, 3,64,52,217,226,250,124,123,
        5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,28,42,
        223,183,170,213,119,248,152, 2,44,154,163, 70,221,153,101,155,167, 43,172,9,
        129,22,39,253, 19,98,108,110,79,113,224,232,178,185, 112,104,218,246,97,228,
        251,34,242,193,238,210,144,12,191,179,162,241, 81,51,145,235,249,14,239,107,
        49,192,214, 31,181,199,106,157,184, 84,204,176,115,121,50,45,127, 4,150,254,
        138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    int X = int(floor(x)) & 255,                     // FIND UNIT CUBE THAT
        Y = int(floor(y)) & 255,                     // CONTAINS POINT.
        Z = int(floor(z)) & 255;
  x -= floor(x);                                     // FIND RELATIVE X,Y,Z
  y -= floor(y);                                     // OF POINT IN CUBE.
  z -= floor(z);
  float u = perlin_fade(x),                          // COMPUTE FADE CURVES
        v = perlin_fade(y),                          // FOR EACH OF X,Y,Z.
        w = perlin_fade(z);
  int A = p[X  ]+Y, AA = p[A]+Z, AB = p[A+1]+Z,      // HASH COORDINATES OF
      B = p[X+1]+Y, BA = p[B]+Z, BB = p[B+1]+Z;      // THE 8 CUBE CORNERS,

  return perlin_lerp(w, perlin_lerp(v, perlin_lerp(u, perlin_grad(p[AA  ], x  , y  , z   ),  // AND ADD
                                                      perlin_grad(p[BA  ], x-1, y  , z   )), // BLENDED
                                       perlin_lerp(u, perlin_grad(p[AB  ], x  , y-1, z   ),  // RESULTS
                                                      perlin_grad(p[BB  ], x-1, y-1, z   ))),// FROM  8
                        perlin_lerp(v, perlin_lerp(u, perlin_grad(p[AA+1], x  , y  , z-1 ),  // CORNERS
                                                      perlin_grad(p[BA+1], x-1, y  , z-1 )), // OF CUBE
                                       perlin_lerp(u, perlin_grad(p[AB+1], x  , y-1, z-1 ),
                                                      perlin_grad(p[BB+1], x-1, y-1, z-1 ))));
}
float field_height ( float x, float z ) {
    float param_scale = 0.13f;
    float height_scale = 10.f;
    return perlin_noise ( x * param_scale, 0, z * param_scale ) * height_scale;
}

//
// GPU Zen 2 - Adaptive GPU Tessellation with Compute Shaders
//
//  Main changes from the original:
//      - bit_to_xform winding order
//          the original author bit_to_xform implementation seems wrong? it flips the winding order every other lod level.
//          current implementation uses re-derived barycentric-space xform matrix values that avoid doing that. 
//      - update_subdivision logic
//          the original subdivision logic causes split-merge cycles (flickering) and merge disagreeing siblings (missing triangles/flickering).
//          current implementation computes and uses parent_target_lod to stabilize the update and avoid those issues, while also not introducing t-junction issues.
//

/*
    Barycentric space:
      0,1                                
        |\                   |\          
        | \                  | \ <- 0.5, 0.5         
        |__\ 1,0             |/_\    
      0,0                             

    Barycentric space mapping for zero-key child (S0):
    | a  b  0.5 |   | 0 |   | 0.5 |
    | c  d  0.5 | * | 0 | = | 0.5 |
    | 0  0   1  |   | 1 |   |  1  |

    | a  b  0.5 |   | 1 |   |  0  |
    | c  d  0.5 | * | 0 | = |  1  |
    | 0  0   1  |   | 1 |   |  1  |

    | a  b  0.5 |   | 0 |   |  0  |
    | c  d  0.5 | * | 1 | = |  0  |
    | 0  0   1  |   | 1 |   |  1  |

    Barycentric space mapping for one-key child (S1):
    | a  b  0.5 |   | 0 |   | 0.5 |
    | c  d  0.5 | * | 0 | = | 0.5 |
    | 0  0   1  |   | 1 |   |  1  |

    | a  b  0.5 |   | 1 |   |  0  |
    | c  d  0.5 | * | 0 | = |  0  |
    | 0  0   1  |   | 1 |   |  1  |

    | a  b  0.5 |   | 0 |   |  1  |
    | c  d  0.5 | * | 1 | = |  0  |
    | 0  0   1  |   | 1 |   |  1  |

    Solve for a,b,c,d and obrain the following xform matrix.
*/
mat3 bit_to_xform ( uint bit ) {
#if 1
    float s = float ( bit );
    vec3 c1 = vec3 ( -0.5, -0.5 + s, 0 ); 
    vec3 c2 = vec3 ( 0.5 - s, -0.5, 0 ); 
    vec3 c3 = vec3 (  0.5,  0.5, 1 );
    return mat3 ( c1, c2, c3 );
#else
    float s = float ( bit ) - 0.5;
    vec3 c1 = vec3 (    s, -0.5, 0 ); 
    vec3 c2 = vec3 ( -0.5,   -s, 0 ); 
    vec3 c3 = vec3 (  0.5,  0.5, 1 );
    return mat3 ( c1, c2, c3 );
#endif
}

mat3 key_to_xform ( uint key ) {
    mat3 xform = mat3 ( 1.0 );
    
    while ( key > 1 ) {
        xform = bit_to_xform ( key & 1 ) * xform;
        key = key >> 1;
    }

    return xform;
}

vec3 berp ( vec3 v[3], vec2 u ) {
    return v[0] + u.x * ( v[1] - v[0] ) + u.y * ( v[2] - v[0] );
}

void build_key_verts ( uint key, vec3 prim_verts[3], out vec3 out_verts[3] ) {
    mat3 xform = key_to_xform ( key );

    // transform barycentric-space vertices
    vec2 u1 = ( xform * vec3 ( 0, 0, 1 ) ).xy;  // bottom left    |\ 
    vec2 u2 = ( xform * vec3 ( 1, 0, 1 ) ).xy;  // bottom right   | \  
    vec2 u3 = ( xform * vec3 ( 0, 1, 1 ) ).xy;  // top left       |__\ 

    // Baricentric interpolate the primitive vertices at the new barycentric space coordinates for the transformed vertices
    out_verts[0] = berp ( prim_verts, u1 );
    out_verts[1] = berp ( prim_verts, u2 );
    out_verts[2] = berp ( prim_verts, u3 );
}
