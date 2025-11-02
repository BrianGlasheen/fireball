cmake_minimum_required(VERSION 3.15)

project(fireball)

# set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Vulkan REQUIRED)

add_subdirectory(external/glfw)
add_subdirectory(external/glm)
add_subdirectory(external/vk-bootstrap)

include_directories(
    PRIVATE
        src
)

file(GLOB_RECURSE SOURCES
    "src/*.cpp"
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} 
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm
        vk-bootstrap::vk-bootstrap
)
