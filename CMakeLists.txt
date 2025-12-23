cmake_minimum_required(VERSION 3.15)

project(fireball)

option(SHADER_DEBUG "Embed debug information into SPIV shaders" OFF)

file(
    GLOB_RECURSE
    GLSL_SOURCE_FILES
    "src/shaders/*.comp"
    "src/shaders/*.vert"
    "src/shaders/*.frag"
)

file(
    GLOB_RECURSE
    GLSL_HEADER_FILES
    "src/shaders/*.h"
)

file(
    GLOB_RECURSE
    CPP_SOURCE_FILES
    "src/*.h"
    "src/*.cpp"
)

set(
    IMGUI_SOURCES
    "external/imgui/imgui.cpp"
    "external/imgui/imgui_demo.cpp"
    "external/imgui/imgui_draw.cpp"
    "external/imgui/imgui_tables.cpp"
    "external/imgui/imgui_widgets.cpp"
    "external/imgui/backends/imgui_impl_glfw.cpp"
    "external/imgui/backends/imgui_impl_vulkan.cpp"
)

# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_executable(
    ${PROJECT_NAME}
    ${CPP_SOURCE_FILES}
    ${GLSL_SOURCE_FILES}
    ${GLSL_HEADER_FILES}
    ${IMGUI_SOURCES}
)

# visual studio stuff
source_group("Shaders" FILES ${GLSL_SOURCE_FILES} ${GLSL_HEADER_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CPP_SOURCE_FILES})

target_compile_definitions(
    ${PROJECT_NAME}
    PRIVATE 
    GLFW_INCLUDE_NONE
    GLM_FORCE_XYZW_ONLY
    GLM_FORCE_QUAT_DATA_XYZW
    GLM_FORCE_QUAT_CTOR_XYZW
)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
    "external/stb"
    "src"
)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL_PDB OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

find_package(Vulkan REQUIRED)

find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    message("glfw3 not found, building from source")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/glfw)
endif()

add_subdirectory(external/glm)
add_subdirectory(external/vk-bootstrap)
add_subdirectory(external/vma)
add_subdirectory(external/assimp)
add_subdirectory(external/meshoptimizer)
add_subdirectory(external/flecs)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "external/imgui"
        "external/imgui/backends"
)

if(UNIX AND NOT APPLE)
    target_link_libraries(
        ${PROJECT_NAME}
        PRIVATE
            X11 Xcursor Xrandr Xi Xinerama Xxf86vm pthread dl
    )
endif()

target_link_libraries(
    ${PROJECT_NAME} 
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
        assimp
        meshoptimizer
        flecs::flecs_static
)

find_program(GLSL_VALIDATOR glslangValidator HINTS /usr/bin /usr/local/bin $ENV{VULKAN_SDK}/Bin/ $ENV{VULKAN_SDK}/Bin32/)

set(GLSL_FLAGS --target-env vulkan1.4)

if (SHADER_DEBUG)
  list(APPEND GLSL_FLAGS -gVS)
endif()

# Thanks to: https://gist.github.com/evilactually/a0d191701cb48f157b05be7f74d79396
set(SPIRV_OUTPUT_DIR "${PROJECT_BINARY_DIR}/spirv/")
foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(STEM ${GLSL} NAME)
  set(SPIRV "${SPIRV_OUTPUT_DIR}${STEM}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SPIRV_OUTPUT_DIR}"
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL_FLAGS} ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL} ${GLSL_HEADER_FILES}
  )
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(${PROJECT_NAME} compile_shaders)
