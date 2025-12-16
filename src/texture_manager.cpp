#include "texture_manager.h"

#include "renderer/vk_types.h"
#include "renderer/vk_util.h"
#include "math.h"

#include <stb_image.h>
#include <vulkan/vk_enum_string_helper.h>

#include <array>
#include <cmath>
#include <map>

#define VK_CHECK(x)																  \
    do {																		  \
        VkResult err = x;														  \
        if (err) {																  \
            fprintf(stderr, "Detected Vulkan error: %s\n", string_VkResult(err)); \
            abort();															  \
        }																		  \
    } while (0)

#define MAX_BINDLESS_TEXTURES 65535

namespace Texture_Manager {

	static Vk_Backend* renderer;
	static VmaAllocator allocator;
	static VkDevice device;

	static DeletionQueue deq;

	static std::map<std::string, Texture> textures;

	// hm
	static VkDescriptorSetLayout _bindlessDescriptorLayout;
	static VkDescriptorSet _bindlessDescriptorSet;
	static std::vector<AllocatedImage> _bindlessTextures;
	static uint32_t _nextBindlessTextureIndex = 0;

	// put in array or something
	static VkSampler _defaultSamplerLinear;
	static VkSampler _defaultSamplerNearest;

    void init(Vk_Backend* _renderer) {
		renderer = _renderer;
		allocator = _renderer->_allocator;
		device = _renderer->_device;

		//uint32_t white = glm::packUnorm4x8(vec4(1, 1, 1, 1));
		//AllocatedImage _whiteImage = Texture_Manager::create_image((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		//	VK_IMAGE_USAGE_SAMPLED_BIT);

		//uint32_t grey = glm::packUnorm4x8(vec4(0.66f, 0.66f, 0.66f, 1));
		//AllocatedImage _greyImage = Texture_Manager::create_image((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
		//	VK_IMAGE_USAGE_SAMPLED_BIT);

		VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

		sampl.magFilter = VK_FILTER_NEAREST;
		sampl.minFilter = VK_FILTER_NEAREST;

		vkCreateSampler(device, &sampl, nullptr, &_defaultSamplerNearest);

		sampl.magFilter = VK_FILTER_LINEAR;
		sampl.minFilter = VK_FILTER_LINEAR;
		vkCreateSampler(device, &sampl, nullptr, &_defaultSamplerLinear);


		uint32_t black = glm::packUnorm4x8(vec4(0, 0, 0, 0));
		uint32_t magenta = glm::packUnorm4x8(vec4(1, 0, 1, 1));

		Texture& black_texture = textures["black"];
		black_texture.allocated_image = create_image((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
		black_texture.bindless_id = add_bindless_texture(black_texture.allocated_image);
		
		//checkerboard image
		std::array<uint32_t, 16 * 16 > pixels; //for 16x16 checkerboard texture
		for (int x = 0; x < 16; x++) {
			for (int y = 0; y < 16; y++) {
				pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
			}
		}

		Texture& error_texture = textures["error"];
		error_texture.allocated_image = create_image(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
		error_texture.bindless_id = add_bindless_texture(error_texture.allocated_image);

		deq.push_function([=]() {
			vkDestroySampler(device, _defaultSamplerNearest, nullptr);
			vkDestroySampler(device, _defaultSamplerLinear, nullptr);

			free_texture("error");
			free_texture("black");
		});
    }

    void cleanup() {
        deq.flush();
    }

	//bool loaded_already(const std::string& new_path, Texture_Handle& handle);
	
	uint32_t load(const std::string& file_path) {
		auto it = textures.find(file_path);
		if (it != textures.end()) {
			printf("[TEXTURE] Already loaded: %s\n", file_path.c_str());
			return it->second.bindless_id;
		}

		int width, height, nrComponents;
		unsigned char* data = stbi_load(file_path.c_str(), &width, &height, &nrComponents, 4); // Force 4 channels

		if (!data) {
			fprintf(stderr, "[TEXTURE] Failed to load: %s\n", file_path.c_str());
			assert(false);
		}

		VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
		if (nrComponents == 1) {
			format = VK_FORMAT_R8_UNORM;
		}

		AllocatedImage image = create_image(
			data,
			VkExtent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 },
			format,
			VK_IMAGE_USAGE_SAMPLED_BIT,
			true
		);

		stbi_image_free(data);

		uint32_t bindless_id = add_bindless_texture(image);

		auto [insertIt, inserted] = textures.try_emplace(file_path);
		insertIt->second.allocated_image = image;
		insertIt->second.bindless_id = bindless_id;

		printf("[TEXTURE] Loaded texture: %s (%dx%d, bindless_id=%u)\n",
			file_path.c_str(), width, height, bindless_id);

		deq.push_function([=]() {
			free_texture(file_path);
		});

		return bindless_id;
	}

	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
		AllocatedImage newImage;
		newImage.imageFormat = format;
		newImage.imageExtent = size;

		VkImageCreateInfo img_info = image_create_info(format, usage, size);
		if (mipmapped)
			img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;

		// always allocate images on dedicated GPU memory
		VmaAllocationCreateInfo allocinfo = {};
		allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		// allocate and create the image
		VK_CHECK(vmaCreateImage(allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

		// if the format is a depth format, we will need to have it use the correct
		// aspect flag
		VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
		if (format == VK_FORMAT_D32_SFLOAT) {
			aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
		}

		// build a image-view for the image
		VkImageViewCreateInfo view_info = imageview_create_info(format, newImage.image, aspectFlag);
		view_info.subresourceRange.levelCount = img_info.mipLevels;

		VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &newImage.imageView));

		return newImage;
	}

	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
		size_t data_size = size.depth * size.width * size.height * 4;
		AllocatedBuffer uploadbuffer = renderer->create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		memcpy(uploadbuffer.info.pMappedData, data, data_size);

		AllocatedImage new_image = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

		renderer->immediate_submit([&](VkCommandBuffer cmd) {
			transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkBufferImageCopy copyRegion = {};
			copyRegion.bufferOffset = 0;
			copyRegion.bufferRowLength = 0;
			copyRegion.bufferImageHeight = 0;

			copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copyRegion.imageSubresource.mipLevel = 0;
			copyRegion.imageSubresource.baseArrayLayer = 0;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageExtent = size;

			// copy the buffer into the image
			vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
				&copyRegion);

			transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

		renderer->destroy_buffer(uploadbuffer);

		return new_image;
	}

	uint32_t add_bindless_texture(const AllocatedImage& image) {
		if (_nextBindlessTextureIndex > MAX_BINDLESS_TEXTURES) {
			fprintf(stderr, "Bindless texture limit reached!\n");
			return 0;
		}

		uint32_t textureIndex = _nextBindlessTextureIndex++;

		// Update the descriptor set
		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = _defaultSamplerLinear;
		imageInfo.imageView = image.imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = _bindlessDescriptorSet;
		write.dstBinding = 0;
		write.dstArrayElement = textureIndex;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

		return textureIndex;
	}

	void free_texture(const std::string& str) {
		printf("deleting %s\n", str.c_str());

		auto it = textures.find(str);
		if (it != textures.end()) {
			// remove_bindless_texture(handle.iter->second.bindless_id);
			destroy_image(it->second.allocated_image);
			textures.erase(it);
		}
		else
			assert(false);
	}

	void destroy_image(const AllocatedImage& img) {
		vkDestroyImageView(device, img.imageView, nullptr);
		vmaDestroyImage(allocator, img.image, img.allocation);
	}

	const Texture& get_by_name(const std::string& str) {
		auto it = textures.find(str);
		if (it != textures.end())
			return it->second;

		assert(false);
		//return textures.at("error");
	}

	uint32_t max_bindless() {
		return MAX_BINDLESS_TEXTURES;
	}

	void set_bindless_descriptor_layout(VkDescriptorSetLayout layout) {
		_bindlessDescriptorLayout = layout;
	}

	void set_bindless_descriptor_set(VkDescriptorSet set) {
		_bindlessDescriptorSet = set;
	}

	VkDescriptorSetLayout get_bindless_descriptor_layout() {
		return _bindlessDescriptorLayout;
	}

	VkDescriptorSet get_bindless_descriptor_set() {
		return _bindlessDescriptorSet;
	}

    //bool loaded_already(const std::string& new_path, Texture_Handle& handle) {
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
