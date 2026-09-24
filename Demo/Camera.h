/**
 * @ Author: Amélie Heinrich
 * @ Copyright: Copyright (c) 2026 Amélie Heinrich. All rights reserved.
 *
 * First person camera. Right-handed, Y up, clip depth [0, 1].
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

struct Camera {
    glm::vec3 position = glm::vec3(0.0f, 4.0f, 14.0f);
    float yaw = -glm::half_pi<float>(); // Looking down -Z
    float pitch = -0.25f;
    float fovY = glm::radians(60.0f);
    float nearPlane = 0.05f;
    float moveSpeed = 6.0f;
    float lookSensitivity = 0.0025f;

    glm::vec3 Forward() const {
        return glm::vec3(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
    }

    glm::vec3 Right() const {
        return glm::normalize(glm::cross(Forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    void Look(float dx, float dy) {
        yaw += dx * lookSensitivity;
        pitch = std::clamp(pitch - dy * lookSensitivity, -1.55f, 1.55f);
    }

    // forward/right/up in [-1, 1]
    void Move(float forward, float right, float up, float dt, bool fast) {
        float speed = moveSpeed * (fast ? 4.0f : 1.0f) * dt;
        position += Forward() * (forward * speed) + Right() * (right * speed) + glm::vec3(0.0f, up * speed, 0.0f);
    }

    glm::mat4 View() const {
        return glm::lookAtRH(position, position + Forward(), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Infinite far plane. Reverse-Z maps the near plane to 1 and infinity to 0, standard maps near to 0 and infinity to 1.
    glm::mat4 Projection(float aspect, bool reverseZ) const {
        float f = 1.0f / std::tan(fovY * 0.5f);
        glm::mat4 p(0.0f);
        p[0][0] = f / aspect;
        p[1][1] = f;
        p[2][3] = -1.0f;
        if (reverseZ) {
            p[2][2] = 0.0f;
            p[3][2] = nearPlane;
        } else {
            p[2][2] = -1.0f;
            p[3][2] = -nearPlane;
        }
        return p;
    }
};
