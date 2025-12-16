#pragma once

//#include "renderer/vk_util.h"
#include "renderer/vk_backend.h"

#include <string>
#include <vector>
#include <cstdint>
#include <map>

struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct Texture {
    AllocatedImage allocated_image;
    uint32_t bindless_id;
};

// hm
struct Texture_Handle {
    //uint32_t index;
    //std::map<std::string, Texture>::iterator iter;
};

namespace Texture_Manager {
    void init(Vk_Backend* _renderer);
    void cleanup();

    uint32_t load(const std::string& file_path);

	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
    uint32_t add_bindless_texture(const AllocatedImage& image);

    void free_texture(const std::string& str);
    //void free_texture(const Texture_Handle& handle);
    void destroy_image(const AllocatedImage& img);

    const Texture& get_by_name(const std::string& str);

    uint32_t max_bindless();
    void set_bindless_descriptor_layout(VkDescriptorSetLayout layout);
    void set_bindless_descriptor_set(VkDescriptorSet set);
    VkDescriptorSetLayout get_bindless_descriptor_layout();
    VkDescriptorSet get_bindless_descriptor_set();
 //   Texture_Handle load_dds(const std::string& file_path);
 //   Texture_Handle load_png(const std::string& file_path);

	////AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	////AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

 //   size_t get_num_textures();
 //   std::string get_name(Texture_Handle handle);
}
