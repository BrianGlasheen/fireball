#pragma once

//#include "renderer/vk_util.h"

#include <string>
#include <vector>
#include <cstdint>

struct Texture_Handle {
    uint32_t index;
};

namespace Texture_Manager {
    void init();
    void cleanup();

 //   bool loaded_already(const std::string& new_path, Texture_Handle& handle);

 //   Texture_Handle load(const std::string& file_path);
 //   Texture_Handle load_dds(const std::string& file_path);
 //   Texture_Handle load_png(const std::string& file_path);

	////AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	////AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

 //   size_t get_num_textures();
 //   std::string get_name(Texture_Handle handle);
}
