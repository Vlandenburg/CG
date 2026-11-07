#pragma once

#include "graphics_internal.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct SceneUBO {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec4 viewPos;
    alignas(16) glm::vec4 dirLightDir;
    alignas(16) glm::vec4 dirLightColor;
};

struct ModelUBO {
    alignas(16) glm::mat4 model;
};

struct PushConstBlock {
    float time;
    int nonTrivial;
};

namespace application {

bool initialize();
void shutdown();
void update(double time);
void render(const graphics::internal::FrameData& fd);

}