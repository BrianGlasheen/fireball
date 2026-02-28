#pragma once

#include "pipeline.h"
#include "vk_util.h"

#include "fireball/util/math.h"

#include <vulkan/vulkan.h>

#include <cassert>
#include <vector>

using namespace glm;

struct Debug_Vertex {
    vec3 position;
    vec4 color;
};

enum class Debug_Draw_Mode {
    Lines,
    Points
    //Triangles
};

struct Debug_Draw_Batch {
    std::vector<Debug_Vertex> vertices;
    Debug_Draw_Mode mode;
    bool depth_test;
    float line_width = 1.0f;
};

class Vk_Debug_Backend {
public:
    void init(VkDevice dev, VkPhysicalDevice physical_dev, VkFormat color_format, VkFormat depth_format) {
        device = dev;
        physical_device = physical_dev;

        VkShaderModule vertex;
        if (!load_shader_module("spirv/debug.vert.spv", device, &vertex)) {
            fprintf(stderr, "Error when building the vertex shader module");
            assert(false);
        }

        VkShaderModule fragment;
        if (!load_shader_module("spirv/debug.frag.spv", device, &fragment)) {
            fprintf(stderr, "Error when building the fragment shader module");
            assert(false);
        }

        VkPushConstantRange push_constant = {};
        push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant.offset = 0;
        push_constant.size = sizeof(Push_Constants);

        VkPipelineLayoutCreateInfo layout_info = {};
        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_constant;

        vkCreatePipelineLayout(device, &layout_info, nullptr, &draw_pipeline_layout);

        Pipeline_Builder builder;

        builder.set_shaders(vertex, fragment);
        builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        builder.set_multisampling_none();
        builder.enable_blending_alphablend();
        builder.set_color_attachment_format(color_format);
        builder.set_depth_format(depth_format);
        //builder.enable_depthtest(false, VK_COMPARE_OP_LESS_OR_EQUAL);
        builder.disable_depthtest();

        builder._pipelineLayout = draw_pipeline_layout;

        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = sizeof(Debug_Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributes[2] = {};
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Debug_Vertex, position);

        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[1].offset = offsetof(Debug_Vertex, color);

        VkPipelineVertexInputStateCreateInfo vertex_input = {};
        vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 2;
        vertex_input.pVertexAttributeDescriptions = attributes;

        VkPipelineViewportStateCreateInfo viewport_state = {};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineColorBlendStateCreateInfo color_blend = {};
        color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend.attachmentCount = 1;
        color_blend.pAttachments = &builder._colorBlendAttachment;

        VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
            VK_DYNAMIC_STATE_LINE_WIDTH
        };

        VkPipelineDynamicStateCreateInfo dynamic_state = {};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = 5;
        dynamic_state.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo pipeline_info = {};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.pNext = &builder._renderInfo;
        pipeline_info.stageCount = (uint32_t)builder._shaderStages.size();
        pipeline_info.pStages = builder._shaderStages.data();
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &builder._inputAssembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &builder._rasterizer;
        pipeline_info.pMultisampleState = &builder._multisampling;
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.pDepthStencilState = &builder._depthStencil;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = builder._pipelineLayout;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &draw_pipeline) != VK_SUCCESS) {
            fprintf(stderr, "Failed to create debug pipeline\n");
            assert(false);
        }

        vkDestroyShaderModule(device, vertex, nullptr);
        vkDestroyShaderModule(device, fragment, nullptr);

        vertex_buffer_size = 1024 * 1024;
        ensure_vertex_buffer_size(vertex_buffer_size);
    }

    void cleanup() {
        if (vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertex_buffer, nullptr);
        }
        if (vertex_buffer_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, vertex_buffer_memory, nullptr);
        }
        if (draw_pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, draw_pipeline, nullptr);
        }
        if (draw_pipeline_layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, draw_pipeline_layout, nullptr);
        }
    }

    void add_line(vec3 start, vec3 end, vec4 color = vec4(1.0f), bool depth_test = false) {
        Debug_Draw_Batch* batch = nullptr;
        for (auto& b : batches) {
            if (b.mode == Debug_Draw_Mode::Lines && b.depth_test == depth_test) {
                batch = &b;
                break;
            }
        }

        if (!batch) {
            batches.push_back({});
            batch = &batches.back();
            batch->mode = Debug_Draw_Mode::Lines;
            batch->depth_test = depth_test;
        }

        batch->vertices.push_back({ start, color });
        batch->vertices.push_back({ end, color });
    }
    //void add_box(vec3 min, vec3 max, vec4 color = vec4(1.0f), bool depth_test = true);
    //void add_sphere(vec3 center, float radius, vec4 color = vec4(1.0f), int segments = 16, bool depth_test = true);
    //void add_grid(vec3 center, float size, int divisions, vec4 color = vec4(0.5f, 0.5f, 0.5f, 1), bool depth_test = true);
    void add_point(vec3 position, vec4 color = vec4(1), bool depth_test = false) {
        Debug_Draw_Batch* batch = nullptr;
        for (auto& b : batches) {
            if (b.mode == Debug_Draw_Mode::Points && b.depth_test == depth_test) {
                batch = &b;
                break;
            }
        }

        if (!batch) {
            batches.push_back({});
            batch = &batches.back();
            batch->mode = Debug_Draw_Mode::Points;
            batch->depth_test = depth_test;
        }

        batch->vertices.push_back({ position, color });
    }

    void render(VkCommandBuffer cmd, mat4 view_proj) {
        if (batches.empty()) return;

        size_t total_vertices = 0;
        for (auto& batch : batches) {
            total_vertices += batch.vertices.size();
        }

        ensure_vertex_buffer_size(total_vertices * sizeof(Debug_Vertex));

        void* data;
        vkMapMemory(device, vertex_buffer_memory, 0, total_vertices * sizeof(Debug_Vertex), 0, &data);
        size_t offset = 0;
        for (auto& batch : batches) {
            memcpy((char*)data + offset, batch.vertices.data(),
                batch.vertices.size() * sizeof(Debug_Vertex));
            offset += batch.vertices.size() * sizeof(Debug_Vertex);
        }
        vkUnmapMemory(device, vertex_buffer_memory);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw_pipeline);
        VkDeviceSize buffer_offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &buffer_offset);

        Push_Constants pc = { view_proj };
        vkCmdPushConstants(cmd, draw_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
            0, sizeof(Push_Constants), &pc);

        uint32_t vertex_offset = 0;
        for (auto& batch : batches) {
            VkPrimitiveTopology topology = batch.mode == Debug_Draw_Mode::Lines
                ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                : VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            vkCmdSetPrimitiveTopology(cmd, topology);

            vkCmdSetDepthTestEnable(cmd, batch.depth_test ? VK_TRUE : VK_FALSE);
            vkCmdSetLineWidth(cmd, batch.line_width);

            uint32_t vertex_count = (uint32_t)batch.vertices.size();
            vkCmdDraw(cmd, vertex_count, 1, vertex_offset, 0);

            vertex_offset += vertex_count;
        }
    }

    void clear() {
        batches.clear();
    }

private:
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkPipeline draw_pipeline;
    VkPipelineLayout draw_pipeline_layout;
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_buffer_memory = VK_NULL_HANDLE;
    size_t vertex_buffer_size = 0;

    struct Push_Constants {
        mat4 view_proj;
    };

    std::vector<Debug_Draw_Batch> batches;

    void ensure_vertex_buffer_size(size_t required_size) {
        if (required_size <= vertex_buffer_size && vertex_buffer != VK_NULL_HANDLE) {
            return;
        }

        if (vertex_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, vertex_buffer, nullptr);
            vkFreeMemory(device, vertex_buffer_memory, nullptr);
        }

        vertex_buffer_size = required_size * 2;

        VkBufferCreateInfo buffer_info = {};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = vertex_buffer_size;
        buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        vkCreateBuffer(device, &buffer_info, nullptr, &vertex_buffer);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(device, vertex_buffer, &mem_reqs);

        VkMemoryAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_reqs.size;

        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
        alloc_info.memoryTypeIndex = find_memory_type(mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        vkAllocateMemory(device, &alloc_info, nullptr, &vertex_buffer_memory);
        vkBindBufferMemory(device, vertex_buffer, vertex_buffer_memory, 0);
    }

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties mem_props;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if ((type_filter & (1 << i)) &&
                (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        assert(false && "Failed to find suitable memory type");
    }
};
