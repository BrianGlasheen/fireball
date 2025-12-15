#include "texture_manager.h"

#include <vulkan/vk_enum_string_helper.h>

#include <cmath>

#define VK_CHECK(x)																  \
    do {																		  \
        VkResult err = x;														  \
        if (err) {																  \
            fprintf(stderr, "Detected Vulkan error: %s\n", string_VkResult(err)); \
            abort();															  \
        }																		  \
    } while (0)

namespace Texture_Manager {
    struct Texture {
        std::string path;
    };

    void init() {

    }

    void cleanup() {
    
    }

    //bool loaded_already(const std::string& new_path, Texture_Handle& handle) {
    //}

    //Texture_Handle load(const std::string& file_path) {
    //}

    //Texture_Handle load_dds(const std::string& file_path) {
    //}

    //Texture_Handle load_png(const std::string& file_path) {
    //}

    //size_t get_num_textures() {
    //}

    //std::string get_name(Texture_Handle handle) {
    //}
}
