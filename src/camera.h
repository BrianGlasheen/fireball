#pragma once

#define NEAR_PLANE 0.1f

#include "util/math.h"

#include <GLFW/glfw3.h>

const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SENSITIVITY = 0.05f;
const float ZOOM = 45.0f;
const float PAN_SENSITIVITY = 0.005f;
const float ZOOM_SENSITIVITY = 2.0f;

class Camera {
public:
    vec3 position;
    vec3 front;
    vec3 up;
    vec3 right;
    float yaw;
    float pitch;
    
    float mouse_sensitivity;
    float zoom;
    
    float lastX = 0, lastY = 0;

    vec3 world_up;

    Camera(vec3 pos = vec3(0.0f),
           float yaw_ = YAW,
           float pitch_ = PITCH,
           vec3 world_up_ = vec3(0.0f, 1.0f, 0.0f))
        : position(pos),
        world_up(world_up_),
        yaw(yaw_),
        pitch(pitch_),
        front(0, 0, -1),
        mouse_sensitivity(SENSITIVITY),
        zoom(ZOOM)
    {
        update_camera_vectors();
    }

    mat4 get_view() const {
        return lookAt(position, position + front, up);
    }

    // reverse z infinite far
    mat4 get_projection(float aspect) const {
        float f = 1.0f / std::tan(radians(zoom) * 0.5f);

        mat4 result(0.0f);
        result[0][0] = f / aspect;
        result[1][1] = -f; // vulkan
        result[2][2] = 0.0f; // infinity
        result[2][3] = -1.0f;
        result[3][2] = NEAR_PLANE;

        return result;
    }

    void update(double xpos, double ypos) {
        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos;
        lastX = xpos;
        lastY = ypos;

        xoffset *= mouse_sensitivity;
        yoffset *= mouse_sensitivity;
        yaw += xoffset;
        pitch += yoffset;
        
        if (pitch > 89.0f)
            pitch = 89.0f;
        if (pitch < -89.0f)
            pitch = -89.0f;
        
        // update front, right and up Vectors using the updated Euler angles
        update_camera_vectors();
    }

    // temp
    void move(GLFWwindow* window, double dt) {
        glm::vec3 movement(0.0f);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            movement.z += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            movement.z -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            movement.x -= 1.0f;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            movement.x += 1.0f;

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            movement.y += 1.0f;
        if (glfwGetKey(window, GLFW_KEY_Z) == GLFW_PRESS)
            movement.y -= 1.0f;

        if (glm::length(movement) > 0.0f)
            movement = glm::normalize(movement);

        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            movement *= 100.0f;
            
        glm::vec3 acceleration = front * movement.z + right * movement.x;

        position += acceleration * (float)dt;
    }

private:
    void update_camera_vectors() {
        front.x = cos(radians(yaw)) * cos(radians(pitch));
        front.y = sin(radians(pitch));
        front.z = sin(radians(yaw)) * cos(radians(pitch));
        front = normalize(front);

        right = normalize(cross(front, world_up));
        up = normalize(cross(right, front));
    }
};