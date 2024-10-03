#version 460
#extension GL_NV_ray_tracing : require

layout ( location = 1 ) rayPayloadInNV bool is_shadowed;

void main ( void )
{
    is_shadowed = false;
}
