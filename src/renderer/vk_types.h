#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

#include <deque>
#include <functional>
#include <span>
#include <cstdio>

#define VK_CHECK(x)																  \
    do {																		  \
        VkResult err = x;														  \
        if (err) {																  \
            fprintf(stderr, "Detected Vulkan error: %s\n", string_VkResult(err)); \
            abort();															  \
        }																		  \
    } while (0)

// texture_manager
struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

struct GPUMeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

struct DeletionQueue {
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};

struct DescriptorAllocatorGrowable {
public:
	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio;
	};

	void init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios) {
		ratios.clear();

		for (auto r : poolRatios) {
			ratios.push_back(r);
		}

		VkDescriptorPool newPool = create_pool(device, initialSets, poolRatios);

		setsPerPool = initialSets * 1.5; // grow it next allocation

		readyPools.push_back(newPool);
	}

	void clear_pools(VkDevice device) {
		for (auto p : readyPools) {
			vkResetDescriptorPool(device, p, 0);
		}
		for (auto p : fullPools) {
			vkResetDescriptorPool(device, p, 0);
			readyPools.push_back(p);
		}
		fullPools.clear();
	}

	void destroy_pools(VkDevice device) {
		for (auto p : readyPools) {
			vkDestroyDescriptorPool(device, p, nullptr);
		}
		readyPools.clear();
		for (auto p : fullPools) {
			vkDestroyDescriptorPool(device, p, nullptr);
		}
		fullPools.clear();
	}

	VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void* pNext = nullptr) {
		//get or create a pool to allocate from
		VkDescriptorPool poolToUse = get_pool(device);

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.pNext = pNext;
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = poolToUse;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSet ds;
		VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &ds);

		//allocation failed. Try again
		if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {

			fullPools.push_back(poolToUse);

			poolToUse = get_pool(device);
			allocInfo.descriptorPool = poolToUse;

			VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
		}

		readyPools.push_back(poolToUse);
		return ds;
	}

private:
	VkDescriptorPool get_pool(VkDevice device) {
		VkDescriptorPool newPool;
		if (readyPools.size() != 0) {
			newPool = readyPools.back();
			readyPools.pop_back();
		}
		else {
			//need to create a new pool
			newPool = create_pool(device, setsPerPool, ratios);

			setsPerPool = setsPerPool * 1.5;
			if (setsPerPool > 4092) {
				setsPerPool = 4092;
			}
		}

		return newPool;
	}
	VkDescriptorPool create_pool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios) {
		std::vector<VkDescriptorPoolSize> poolSizes;
		for (PoolSizeRatio ratio : poolRatios) {
			poolSizes.push_back(VkDescriptorPoolSize{
				.type = ratio.type,
				.descriptorCount = uint32_t(ratio.ratio * setCount)
				});
		}

		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = 0;
		pool_info.maxSets = setCount;
		pool_info.poolSizeCount = (uint32_t)poolSizes.size();
		pool_info.pPoolSizes = poolSizes.data();

		VkDescriptorPool newPool;
		vkCreateDescriptorPool(device, &pool_info, nullptr, &newPool);
		return newPool;
	}

	std::vector<PoolSizeRatio> ratios;
	std::vector<VkDescriptorPool> fullPools;
	std::vector<VkDescriptorPool> readyPools;
	uint32_t setsPerPool;
};

struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
	VkSemaphore _swapchainSemaphore, _renderSemaphore;
	VkFence _renderFence;
	DeletionQueue _deletionQueue;
	DescriptorAllocatorGrowable _frameDescriptors;
};

struct DescriptorLayoutBuilder {
	std::vector<VkDescriptorSetLayoutBinding> bindings;

	void add_binding(uint32_t binding, VkDescriptorType type) {
		VkDescriptorSetLayoutBinding newbind{};
		newbind.binding = binding;
		newbind.descriptorCount = 1;
		newbind.descriptorType = type;

		bindings.push_back(newbind);
	}

	void clear() {
		bindings.clear();
	}

	VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0) {
		for (auto& b : bindings) {
			b.stageFlags |= shaderStages;
		}

		VkDescriptorSetLayoutCreateInfo info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
		info.pNext = pNext;

		info.pBindings = bindings.data();
		info.bindingCount = (uint32_t)bindings.size();
		info.flags = flags;

		VkDescriptorSetLayout set;
		VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

		return set;
	}
};

struct DescriptorAllocator {
	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio;
	};

	VkDescriptorPool pool;

	void init_pool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
		std::vector<VkDescriptorPoolSize> poolSizes;
		for (PoolSizeRatio ratio : poolRatios) {
			poolSizes.push_back(VkDescriptorPoolSize{
				.type = ratio.type,
				.descriptorCount = uint32_t(ratio.ratio * maxSets)
				});
		}

		VkDescriptorPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
		pool_info.flags = 0;
		pool_info.maxSets = maxSets;
		pool_info.poolSizeCount = (uint32_t)poolSizes.size();
		pool_info.pPoolSizes = poolSizes.data();

		vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
	}

	void clear_descriptors(VkDevice device) {
		vkResetDescriptorPool(device, pool, 0);
	}

	void destroy_pool(VkDevice device) {
		vkDestroyDescriptorPool(device, pool, nullptr);
	}

	VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout) {
		VkDescriptorSetAllocateInfo allocInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = pool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSet ds;
		VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));

		return ds;
	}
};

struct DescriptorWriter {
	std::deque<VkDescriptorImageInfo> imageInfos;
	std::deque<VkDescriptorBufferInfo> bufferInfos;
	std::vector<VkWriteDescriptorSet> writes;

	void write_image(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
		VkDescriptorImageInfo& info = imageInfos.emplace_back(VkDescriptorImageInfo{
			.sampler = sampler,
			.imageView = image,
			.imageLayout = layout
			});

		VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

		write.dstBinding = binding;
		write.dstSet = VK_NULL_HANDLE; //left empty for now until we need to write it
		write.descriptorCount = 1;
		write.descriptorType = type;
		write.pImageInfo = &info;

		writes.push_back(write);
	}

	void write_buffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type) {

		VkDescriptorBufferInfo& info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
			.buffer = buffer,
			.offset = offset,
			.range = size
			});

		VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

		write.dstBinding = binding;
		write.dstSet = VK_NULL_HANDLE; //left empty for now until we need to write it
		write.descriptorCount = 1;
		write.descriptorType = type;
		write.pBufferInfo = &info;

		writes.push_back(write);
	}

	void clear() {
		imageInfos.clear();
		writes.clear();
		bufferInfos.clear();
	}

	void update_set(VkDevice device, VkDescriptorSet set) {
		for (VkWriteDescriptorSet& write : writes) {
			write.dstSet = set;
		}

		vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
	}
};
