#include <std_main.h>
#include <std_log.h>

#include <assimp/cimport.h>
#include <assimp/postprocess.h>

#include <fs.h>

void std_main ( void ) {
    std_log_info_m ( std_binding_assimp_models_m );
    const char* input_path;
    std_process_info_t process_info;
    std_process_info ( &process_info, std_process_this() );
    input_path = process_info.args[0];

    fs_i* fs = std_module_get_m ( fs_module_name_m );
    fs_path_info_t path_info;
    bool result = fs->get_path_info ( &path_info, input_path );
    
    if ( !result ) {
        std_log_error_m ( "Input path not found" );
        return;
    }

    if ( path_info.flags & fs_path_is_dir_m ) { 
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

    const struct aiScene* ai_scene = aiImportFile ( input_path, flags );

    if ( ai_scene == NULL ) {
        std_log_error_m ( "Error importing file" );
        return;
    }

    std_log_info_m ( "File import succeeded" );

    
}
