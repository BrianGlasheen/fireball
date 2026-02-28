#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "stb_image.h"

#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
