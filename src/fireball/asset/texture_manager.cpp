#include "texture_manager.h"

#include "fireball/renderer/vk_types.h"
#include "fireball/renderer/vk_util.h"
#include "fireball/util/math.h"

#include <stb_image.h>
#include <vulkan/vk_enum_string_helper.h>

#include <array>
#include <cmath>
#include <map>
#include <mutex>
#include <thread>

using std::mutex;
using std::thread;

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

	// this should NOT own this... lol
	static VkDescriptorSetLayout _bindlessDescriptorLayout;
	static VkDescriptorSet _bindlessDescriptorSet;
	// this should NOT own this... lol

	static uint32_t _nextBindlessTextureIndex = 0;

	// put in array or something
	static VkSampler _defaultSamplerLinear;
	static VkSampler _defaultSamplerNearest;

	static mutex texture_mutex;
	static mutex deque_mutex;
	static mutex vma_mutex;
	static mutex immediate_mutex;
	static mutex device_mutex;
	static vector<thread> loading_threads;

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
		uint32_t normal = glm::packUnorm4x8(vec4(0, 0, 1, 0));

		Texture& black_texture = textures["black"];
		black_texture.allocated_image = create_image((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
		black_texture.bindless_id = add_bindless_texture(black_texture.allocated_image);
		black_texture.loading_state = Texture_Loading_State::Loaded;

		Texture& normal_texture = textures["normal"];
		normal_texture.allocated_image = create_image((void*)&normal, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
		normal_texture.bindless_id = add_bindless_texture(normal_texture.allocated_image);
		normal_texture.loading_state = Texture_Loading_State::Loaded;

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
		error_texture.loading_state = Texture_Loading_State::Loaded;

		deq.push_function([=]() {
			vkDestroySampler(device, _defaultSamplerNearest, nullptr);
			vkDestroySampler(device, _defaultSamplerLinear, nullptr);

			free_texture("error");
			free_texture("black");
			free_texture("normal");
		});
    }

    void cleanup() {
        deq.flush();
    }

	//bool loaded_already(const std::string& new_path, Texture_Handle& handle);
	
	// TODO probably change so this returns handle to texture instead of just
	// the bindless id, at some point
	uint32_t load(const std::string& file_path) {
		texture_mutex.lock();
			auto it = textures.find(file_path);
			if (it != textures.end()) {
				// printf("[TEXTURE] Already loaded: %s\n", file_path.c_str());
				texture_mutex.unlock();

				return it->second.bindless_id;
			}
			
			auto [insertIt, inserted] = textures.try_emplace(file_path);
			insertIt->second.loading_state = Texture_Loading_State::Loading;
		texture_mutex.unlock();

		// atomic load, maybe not?
		if (_nextBindlessTextureIndex > MAX_BINDLESS_TEXTURES) {
			fprintf(stderr, "Bindless texture limit reached!\n");
			assert(false);
		}

		uint32_t bindless_id = _nextBindlessTextureIndex++; // TODO atomic add? maybe not!

		// TODO should really get a handle
		// add resource, set state to loading
		// start load for resources handle points at
		// return handle

		loading_threads.emplace_back(load_async, file_path, bindless_id);

		return bindless_id;
	}

	void load_async(const std::string& file_path, uint32_t bindless_id) {
		int width, height, nrComponents;
		unsigned char* data = stbi_load(file_path.c_str(), &width, &height, &nrComponents, 4); // TODO don't Force 4 channels? idk gonna have bcX anyways

		if (!data) {
			fprintf(stderr, "[TEXTURE] Failed to load: %s\n", file_path.c_str());
			assert(false);
		}

		VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
		if (nrComponents == 1) {
			format = VK_FORMAT_R8_UNORM;
		}

		AllocatedImage image = create_image(data, VkExtent3D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 }, format, VK_IMAGE_USAGE_SAMPLED_BIT, true);

		stbi_image_free(data);

		add_bindless_texture(image, bindless_id);

		texture_mutex.lock();
			Texture& texture = textures[file_path];
			texture.allocated_image = image;
			texture.bindless_id = bindless_id;
			texture.loading_state = Texture_Loading_State::Loaded;
			printf("[TEXTURE] Loaded texture: %s (%dx%d, bindless_id=%u)\n", file_path.c_str(), width, height, bindless_id);
		texture_mutex.unlock();

		deque_mutex.lock();
			deq.push_function([=]() {
				free_texture(file_path);
			});
		deque_mutex.unlock();
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
		vma_mutex.lock();
			VK_CHECK(vmaCreateImage(allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));
		vma_mutex.unlock();

		// if the format is a depth format, we will need to have it use the correct
		// aspect flag
		VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
		if (format == VK_FORMAT_D32_SFLOAT) {
			aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
		}

		// build a image-view for the image
		VkImageViewCreateInfo view_info = imageview_create_info(format, newImage.image, aspectFlag);
		view_info.subresourceRange.levelCount = img_info.mipLevels;

		device_mutex.lock();
			VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &newImage.imageView));
		device_mutex.unlock();

		return newImage;
	}

	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
		size_t data_size = size.depth * size.width * size.height * 4;

		vma_mutex.lock(); // TODO maybe probably move mutex to renderer
			Allocated_Buffer uploadbuffer = renderer->create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
		vma_mutex.unlock();
			
		memcpy(uploadbuffer.info.pMappedData, data, data_size);

		AllocatedImage new_image = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

		immediate_mutex.lock();
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
				vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

				transition_image(cmd, new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			});
		immediate_mutex.unlock();

		vma_mutex.lock();
			renderer->destroy_buffer(uploadbuffer);
		vma_mutex.unlock();

		return new_image;
	}

	uint32_t add_bindless_texture(const AllocatedImage& image) {
		if (_nextBindlessTextureIndex > MAX_BINDLESS_TEXTURES) {
			fprintf(stderr, "Bindless texture limit reached!\n");
			assert(false);
		}

		uint32_t texture_index = _nextBindlessTextureIndex++;

		// Update the descriptor set
		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = _defaultSamplerLinear;
		imageInfo.imageView = image.imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = _bindlessDescriptorSet;
		write.dstBinding = 0;
		write.dstArrayElement = texture_index;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		
		return texture_index;
	}

	void add_bindless_texture(const AllocatedImage& image, uint32_t texture_index) {
		// Update the descriptor set
		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = _defaultSamplerLinear;
		imageInfo.imageView = image.imageView;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = _bindlessDescriptorSet;
		write.dstBinding = 0;
		write.dstArrayElement = texture_index;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.pImageInfo = &imageInfo;

		device_mutex.lock();
			vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
		device_mutex.unlock();
	}

	void free_texture(const std::string& str) {
		printf("deleting %s\n", str.c_str());

		auto it = textures.find(str);
		if (it != textures.end()) {
			// remove_bindless_texture(handle.iter->second.bindless_id);
			destroy_image(it->second.allocated_image);
			textures.erase(it);
		}
		else {
			assert(false);
		}
	}

	void destroy_image(const AllocatedImage& img) {
		vkDestroyImageView(device, img.imageView, nullptr);
		vmaDestroyImage(allocator, img.image, img.allocation);
	}

	void wait_for_all_loads() {
		for (thread& load : loading_threads)
			load.join();

		loading_threads.clear();

		for (const auto& [path, texture] : textures)
			assert(texture.loading_state == Texture_Loading_State::Loaded);
			// TODO count MB
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
