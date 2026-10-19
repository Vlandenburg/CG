#pragma once

#include "graphics_internal.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
};

struct SceneUBO {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec4 viewPos;
    alignas(16) glm::vec4 ambientLight;
    alignas(16) glm::vec4 dirLightDir;
    alignas(16) glm::vec4 dirLightColor;
    alignas(16) glm::vec4 pointLightPos;
    alignas(16) glm::vec4 pointLightColor;
    alignas(16) glm::vec4 spotLightPos;
    alignas(16) glm::vec4 spotLightDir;
    alignas(16) glm::vec4 spotLightParams;
};

struct ModelUBO {
    alignas(16) glm::mat4 model;
    alignas(16) glm::vec4 albedo;
    alignas(16) glm::vec4 specularColor;
    alignas(4)  float shininess;
};

namespace application {

bool initialize();
void shutdown();

void update(double time);
void render(const graphics::internal::FrameData& fd);

}