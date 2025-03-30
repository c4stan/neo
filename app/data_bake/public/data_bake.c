#include <std_main.h>
#include <std_log.h>
#include <std_file.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#define encode_u32_m( c1, c2, c3, c4 ) ( \
    ( ( unsigned char ) ( c1 ) << 24 ) | \
    ( ( unsigned char ) ( c2 ) << 16 ) | \
    ( ( unsigned char ) ( c3 ) <<  8 ) | \
    ( ( unsigned char ) ( c4 ) ) \
)

#define bsf_magic_m encode_u32_m ( 'B', 'S', 'F', '1' )
#define bsf_version_m 0x1

#define bsf_chunk_mesh_m        0x0001
#define bsf_chunk_material_m    0x0002
#define bsf_chunk_hierarchy_m   0x0004

typedef struct {
    uint32_t id;
    uint32_t type;
    uint64_t offset;
} bsf_table_entry_t;

void std_main ( void ) {
    std_log_info_m ( std_binding_assimp_models_m );
    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );
    const char* input_path = process_info.args[0];
    const char* output_path = process_info.args[1];

    std_path_info_t path_info;
    bool result = std_path_info ( &path_info, input_path );
    
    if ( !result ) {
        std_log_error_m ( "Input path not found" );
        return;
    }

    if ( path_info.flags & std_path_is_directory_m ) {
        std_log_error_m ( "Folder paths are not currently supported" );
        return;
    }

    unsigned int flags = 0;
    flags |= aiProcess_JoinIdenticalVertices;
    flags |= aiProcess_MakeLeftHanded;
    flags |= aiProcess_Triangulate;
    flags |= aiProcess_ValidateDataStructure;
    flags |= aiProcess_GenSmoothNormals;
    flags |= aiProcess_FindInvalidData;
    flags |= aiProcess_GenUVCoords;
    flags |= aiProcess_GenBoundingBoxes;

    std_log_info_m ( "Importing input scene " std_fmt_str_m, input_path );
    const struct aiScene* scene = aiImportFile ( input_path, flags );

    if ( scene == NULL ) {
        std_log_error_m ( "Error importing file" );
        return;
    }

    uint64_t max_chunk_size = 16ull * 1024 * 1024 * 1024;
    std_virtual_stack_t stack = std_virtual_stack_create ( max_chunk_size );

    uint32_t chunk_count = scene->mNumMeshes + scene->mNumMaterials + 1;
    bsf_table_entry_t* table = std_virtual_heap_alloc_array_m ( bsf_table_entry_t, chunk_count );
    uint32_t global_id = 0;

    // alloc header
    uint32_t header_size = 20 + 12 * chunk_count;
    std_virtual_stack_alloc ( &stack, header_size ); // header + table

    for ( uint32_t mesh_it = 0; mesh_it < scene->mNumMeshes; ++mesh_it ) {
        table[global_id].id = global_id;
        table[global_id].type = bsf_chunk_mesh_m;
        table[global_id].offset = std_virtual_stack_used_size ( &stack );
        ++global_id;

        const struct aiMesh* mesh = scene->mMeshes[mesh_it];
        uint32_t mesh_id = mesh_it;
        uint32_t vertex_count = mesh->mNumVertices;
        uint32_t index_count = mesh->mNumFaces * 3;
        //uint32_t chunk_size = 12 + vertex_count * ( 12 + 12 + 8 ) + index_count * 4; // 12 = id + vertex_count + index_count

        // mesh id, vertex count, idx count
        std_virtual_stack_write_m ( &stack, &mesh_id );
        std_virtual_stack_write_m ( &stack, &vertex_count );
        std_virtual_stack_write_m ( &stack, &index_count );

        // pos, nor, uv
        float* f32_data = std_virtual_heap_alloc_array_m ( float, vertex_count * 3 );
        
        for ( uint32_t i = 0; i < vertex_count; i += 3 ) {
            f32_data[i + 0] = mesh->mVertices[i].x;
            f32_data[i + 1] = mesh->mVertices[i].y;
            f32_data[i + 2] = mesh->mVertices[i].z;
        }
        std_virtual_stack_write_array_m ( &stack, f32_data, vertex_count * 3 );
        
        for ( uint32_t i = 0; i < vertex_count; i += 3 ) {
            f32_data[i + 0] = mesh->mNormals[i].x;
            f32_data[i + 1] = mesh->mNormals[i].y;
            f32_data[i + 2] = mesh->mNormals[i].z;
        }
        std_virtual_stack_write_array_m ( &stack, f32_data, vertex_count * 3 );

        if ( mesh->mTextureCoords[0] ) {
            for ( uint32_t i = 0; i < vertex_count; i += 2 ) {
                f32_data[i + 0] = mesh->mTextureCoords[0][i].x;
                f32_data[i + 1] = mesh->mTextureCoords[0][i].y;
            }
        } else {
            for ( uint32_t i = 0; i < vertex_count; i += 2 ) {
                f32_data[i + 0] = 0;
                f32_data[i + 1] = 0;
            }
        }
        std_virtual_stack_write_array_m ( &stack, f32_data, vertex_count * 2 );
        
        std_virtual_heap_free ( f32_data );
    
        // idx
        uint32_t* u32_data = std_virtual_heap_alloc_array_m ( uint32_t, index_count );
        
        for ( uint32_t i = 0; i < mesh->mNumFaces; i += 3 ) {
            struct aiFace face = mesh->mFaces[i];
            u32_data[i + 0] = face.mIndices[0];
            u32_data[i + 1] = face.mIndices[1];
            u32_data[i + 2] = face.mIndices[2];
        }
        std_virtual_stack_write_array_m ( &stack, u32_data, index_count );
        
        std_virtual_heap_free ( u32_data );
    }

    // fill header
    uint32_t magic = bsf_magic_m;
    uint32_t version = bsf_version_m;
    uint64_t total_size = std_virtual_stack_used_size ( &stack );
    std_stack_t header_stack = std_stack ( stack.mapped.begin, header_size );
    std_stack_write_m ( &header_stack, &magic );
    std_stack_write_m ( &header_stack, &version );
    std_stack_write_m ( &header_stack, &chunk_count );
    std_stack_write_m ( &header_stack, &total_size );
    std_stack_write_array_m ( &header_stack, table, chunk_count );

    std_log_info_m ( "Writing out scene to " std_fmt_str_m, output_path );
    std_file_h file = std_file_create ( output_path, std_file_write_m, std_path_already_existing_overwrite_m );
    std_file_write ( file, stack.mapped.begin, total_size );
    std_file_close ( file );
}
