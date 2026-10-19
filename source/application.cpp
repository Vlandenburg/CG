#include "application.hpp"

#include <imgui.h>
#include <glm/gtc/constants.hpp>

#include <fstream>
#include <vector>
#include <cmath>

namespace {

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

// Камера
glm::vec3 camPos = glm::vec3(0.0f, 1.5f, 4.0f);
float camYaw = -90.0f;
float camPitch = -15.0f;
float camFov = 45.0f;

// Модель
glm::vec3 modelPos = glm::vec3(0.0f);
glm::vec3 modelRot = glm::vec3(0.0f);
float modelScale = 1.0f;

// Материал
glm::vec4 matAlbedo = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f);
glm::vec4 matSpecular = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
float matShininess = 64.0f;

// Свет
glm::vec4 ambientLight = glm::vec4(0.2f, 0.2f, 0.25f, 1.0f);

glm::vec3 dirLightDir = glm::vec3(-1.0f, -1.5f, -1.0f);
glm::vec3 dirLightColor = glm::vec3(1.0f, 0.95f, 0.8f);
float dirLightIntensity = 1.0f;

glm::vec3 pointLightPos = glm::vec3(2.0f, 1.5f, 1.0f);
glm::vec3 pointLightColor = glm::vec3(0.2f, 0.6f, 1.0f);
float pointLightIntensity = 2.0f;

glm::vec3 spotLightPos = glm::vec3(0.0f, 3.0f, 2.0f);
glm::vec3 spotLightDir = glm::vec3(0.0f, -1.0f, -0.5f);
float spotInnerAngle = 12.5f;
float spotOuterAngle = 20.0f;
float spotLightIntensity = 3.0f;

// Ресурсы Vulkan
VkBuffer vertexBuffer = VK_NULL_HANDLE;
VmaAllocation vertexBufferAlloc = VK_NULL_HANDLE;

VkBuffer indexBuffer = VK_NULL_HANDLE;
VmaAllocation indexBufferAlloc = VK_NULL_HANDLE;

VkBuffer sceneUboBuffer = VK_NULL_HANDLE;
VmaAllocation sceneUboAlloc = VK_NULL_HANDLE;
void* sceneUboMapped = nullptr;

VkBuffer modelUboBuffer = VK_NULL_HANDLE;
VmaAllocation modelUboAlloc = VK_NULL_HANDLE;
void* modelUboMapped = nullptr;

VkDescriptorSetLayout sceneDescLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout modelDescLayout = VK_NULL_HANDLE;
VkDescriptorPool descPool = VK_NULL_HANDLE;
VkDescriptorSet sceneDescSet = VK_NULL_HANDLE;
VkDescriptorSet modelDescSet = VK_NULL_HANDLE;

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline graphicsPipeline = VK_NULL_HANDLE;

std::vector<char> readFile(const std::string& filename) {
    std::vector<std::string> paths = {
        filename, "shaders/" + filename, "../shaders/" + filename,
        "C:/Users/Vladislav/Desktop/CG/cg-lab1/shaders/" + filename
    };
    for (const auto& path : paths) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (file.is_open()) {
            size_t size = (size_t)file.tellg();
            std::vector<char> buf(size);
            file.seekg(0);
            file.read(buf.data(), size);
            return buf;
        }
    }
    return {};
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    vkCreateShaderModule(device, &createInfo, nullptr, &mod);
    return mod;
}

void generateSphereWithNormals(float radius, unsigned int rings, unsigned int sectors) {
    vertices.clear();
    indices.clear();
    for (unsigned int r = 0; r <= rings; ++r) {
        float theta = glm::pi<float>() * float(r) / float(rings);
        for (unsigned int s = 0; s <= sectors; ++s) {
            float phi = 2.0f * glm::pi<float>() * float(s) / float(sectors);

            float x = radius * sin(theta) * cos(phi);
            float y = radius * cos(theta);
            float z = radius * sin(theta) * sin(phi);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(pos); // Процедурная нормаль сферы!

            vertices.push_back({pos, normal});
        }
    }
    for (unsigned int r = 0; r < rings; ++r) {
        for (unsigned int s = 0; s < sectors; ++s) {
            unsigned int cur = r * (sectors + 1) + s;
            unsigned int next = cur + sectors + 1;
            indices.push_back(cur); indices.push_back(next); indices.push_back(cur + 1);
            indices.push_back(cur + 1); indices.push_back(next); indices.push_back(next + 1);
        }
    }
}

void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buffer, VmaAllocation& alloc) {
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vmaCreateBuffer(graphics::internal::context.allocator, &bufInfo, &allocInfo, &buffer, &alloc, nullptr);
}

} // namespace

namespace application {

bool initialize() {
    auto& ctx = graphics::internal::context;

    generateSphereWithNormals(1.0f, 10, 10);

    createBuffer(sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexBufferAlloc);
    void* vData;
    vmaMapMemory(ctx.allocator, vertexBufferAlloc, &vData);
    memcpy(vData, vertices.data(), sizeof(Vertex) * vertices.size());
    vmaUnmapMemory(ctx.allocator, vertexBufferAlloc);

    createBuffer(sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer, indexBufferAlloc);
    void* iData;
    vmaMapMemory(ctx.allocator, indexBufferAlloc, &iData);
    memcpy(iData, indices.data(), sizeof(uint32_t) * indices.size());
    vmaUnmapMemory(ctx.allocator, indexBufferAlloc);

    createBuffer(sizeof(SceneUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sceneUboBuffer, sceneUboAlloc);
    vmaMapMemory(ctx.allocator, sceneUboAlloc, &sceneUboMapped);

    createBuffer(sizeof(ModelUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, modelUboBuffer, modelUboAlloc);
    vmaMapMemory(ctx.allocator, modelUboAlloc, &modelUboMapped);

    VkDescriptorSetLayoutBinding bScene{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lSceneInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &bScene};
    vkCreateDescriptorSetLayout(ctx.device, &lSceneInfo, nullptr, &sceneDescLayout);

    VkDescriptorSetLayoutBinding bModel{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo lModelInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &bModel};
    vkCreateDescriptorSetLayout(ctx.device, &lModelInfo, nullptr, &modelDescLayout);

    VkDescriptorPoolSize poolSizes[] = { {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4} };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 2, 1, poolSizes};
    vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &descPool);

    VkDescriptorSetAllocateInfo allocScene{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descPool, 1, &sceneDescLayout};
    vkAllocateDescriptorSets(ctx.device, &allocScene, &sceneDescSet);

    VkDescriptorSetAllocateInfo allocModel{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descPool, 1, &modelDescLayout};
    vkAllocateDescriptorSets(ctx.device, &allocModel, &modelDescSet);

    VkDescriptorBufferInfo biScene{sceneUboBuffer, 0, sizeof(SceneUBO)};
    VkDescriptorBufferInfo biModel{modelUboBuffer, 0, sizeof(ModelUBO)};

    VkWriteDescriptorSet writes[2]{};
    writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sceneDescSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &biScene, nullptr};
    writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, modelDescSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &biModel, nullptr};
    vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);

    // 6. Pipeline Layout & Pipeline
    VkDescriptorSetLayout layouts[] = {sceneDescLayout, modelDescLayout};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 2, layouts, 0, nullptr};
    vkCreatePipelineLayout(ctx.device, &plInfo, nullptr, &pipelineLayout);

    auto vertCode = readFile("vert.spv");
    auto fragCode = readFile("frag.spv");
    VkShaderModule vertMod = createShaderModule(ctx.device, vertCode);
    VkShaderModule fragMod = createShaderModule(ctx.device, fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertMod, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragMod, "main", nullptr};

    VkVertexInputBindingDescription bDesc{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription aDesc[2]{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)}
    };
    VkPipelineVertexInputStateCreateInfo viInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 1, &bDesc, 2, aDesc};
    VkPipelineInputAssemblyStateCreateInfo iaInfo{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkPipelineViewportStateCreateInfo vpInfo{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr};

    VkPipelineRasterizationStateCreateInfo rInfo{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f};
    VkPipelineMultisampleStateCreateInfo msInfo{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, nullptr, VK_FALSE, VK_FALSE};

    VkPipelineDepthStencilStateCreateInfo dsInfo{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_FALSE, VK_FALSE, {}, {}, 0.0f, 1.0f};
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cbInfo{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &cba, {0,0,0,0}};

    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynInfo{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dyn};

    VkGraphicsPipelineCreateInfo pInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0, 2, stages, &viInfo, &iaInfo, nullptr, &vpInfo, &rInfo, &msInfo, &dsInfo, &cbInfo, &dynInfo, pipelineLayout, ctx.render_pass, 0, VK_NULL_HANDLE, 0};
    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &graphicsPipeline);

    vkDestroyShaderModule(ctx.device, fragMod, nullptr);
    vkDestroyShaderModule(ctx.device, vertMod, nullptr);
    return true;
}

void shutdown() {
    auto& ctx = graphics::internal::context;
    vkQueueWaitIdle(ctx.graphics_queue);

    vmaUnmapMemory(ctx.allocator, modelUboAlloc);
    vmaUnmapMemory(ctx.allocator, sceneUboAlloc);
    vmaDestroyBuffer(ctx.allocator, modelUboBuffer, modelUboAlloc);
    vmaDestroyBuffer(ctx.allocator, sceneUboBuffer, sceneUboAlloc);
    vmaDestroyBuffer(ctx.allocator, indexBuffer, indexBufferAlloc);
    vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertexBufferAlloc);

    vkDestroyPipeline(ctx.device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(ctx.device, descPool, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, modelDescLayout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, sceneDescLayout, nullptr);
}

void update(double time) {
    (void)time;

    ImGui::Begin("Лабораторная 2: Камера и Освещение");

    if (ImGui::CollapsingHeader("Камера", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Позиция камеры", &camPos.x, 0.05f);
        ImGui::SliderFloat("Рыскание (Yaw)", &camYaw, -180.0f, 180.0f);
        ImGui::SliderFloat("Тангаж (Pitch)", &camPitch, -89.0f, 89.0f);
        ImGui::SliderFloat("Угол обзора (FOV)", &camFov, 20.0f, 90.0f);
    }

    if (ImGui::CollapsingHeader("Материал (Блинн-Фонг)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Альбедо (Цвет)", &matAlbedo.r);
        ImGui::ColorEdit3("Цвет блика (Specular)", &matSpecular.r);
        ImGui::SliderFloat("Блеск (Shininess)", &matShininess, 2.0f, 256.0f);
    }

    if (ImGui::CollapsingHeader("Направленный свет", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Направление", &dirLightDir.x, 0.05f);
        ImGui::ColorEdit3("Цвет", &dirLightColor.r);
        ImGui::SliderFloat("Интенсивность", &dirLightIntensity, 0.0f, 5.0f);
    }

    if (ImGui::CollapsingHeader("Точечный свет", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Позиция света", &pointLightPos.x, 0.05f);
        ImGui::ColorEdit3("Цвет точки", &pointLightColor.r);
        ImGui::SliderFloat("Интенсивность точки", &pointLightIntensity, 0.0f, 10.0f);
    }

    if (ImGui::CollapsingHeader("Прожектор (Spotlight)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Позиция прожектора", &spotLightPos.x, 0.05f);
        ImGui::DragFloat3("Направление луча", &spotLightDir.x, 0.05f);
        ImGui::SliderFloat("Интенсивность луча", &spotLightIntensity, 0.0f, 10.0f);
        ImGui::SliderFloat("Внутренний угол", &spotInnerAngle, 1.0f, 45.0f);
        ImGui::SliderFloat("Внешний угол", &spotOuterAngle, spotInnerAngle, 60.0f);
    }

    if (ImGui::CollapsingHeader("Рассеянный свет (Ambient)")) {
        ImGui::ColorEdit3("Цвет Ambient", &ambientLight.r);
        ImGui::SliderFloat("Яркость Ambient", &ambientLight.a, 0.0f, 1.0f);
    }

    ImGui::End();

    glm::vec3 front;
    front.x = cos(glm::radians(camYaw)) * cos(glm::radians(camPitch));
    front.y = sin(glm::radians(camPitch));
    front.z = sin(glm::radians(camYaw)) * cos(glm::radians(camPitch));
    front = glm::normalize(front);

    glm::mat4 view = glm::lookAt(camPos, camPos + front, glm::vec3(0.0f, 1.0f, 0.0f));

    auto extent = graphics::internal::context.swapchain_extent;
    float aspect = (extent.height > 0) ? (float)extent.width / (float)extent.height : 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(camFov), aspect, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    SceneUBO sUbo{};
    sUbo.view = view;
    sUbo.proj = proj;
    sUbo.viewPos = glm::vec4(camPos, 1.0f);
    sUbo.ambientLight = ambientLight;
    sUbo.dirLightDir = glm::vec4(dirLightDir, dirLightIntensity);
    sUbo.dirLightColor = glm::vec4(dirLightColor, 1.0f);
    sUbo.pointLightPos = glm::vec4(pointLightPos, pointLightIntensity);
    sUbo.pointLightColor = glm::vec4(pointLightColor, 1.0f);
    sUbo.spotLightPos = glm::vec4(spotLightPos, 1.0f);
    sUbo.spotLightDir = glm::vec4(spotLightDir, 1.0f);
    sUbo.spotLightParams = glm::vec4(cos(glm::radians(spotInnerAngle)), cos(glm::radians(spotOuterAngle)), spotLightIntensity, 0.0f);

    memcpy(sceneUboMapped, &sUbo, sizeof(sUbo));

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, modelPos);
    model = glm::scale(model, glm::vec3(modelScale));

    ModelUBO mUbo{};
    mUbo.model = model;
    mUbo.albedo = matAlbedo;
    mUbo.specularColor = matSpecular;
    mUbo.shininess = matShininess;

    memcpy(modelUboMapped, &mUbo, sizeof(mUbo));
}

void render(const graphics::internal::FrameData& fd) {
    auto& ctx = graphics::internal::context;
    VkCommandBuffer cmd = fd.command_buffer;

    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo cmdBeginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cmdBeginInfo);

    VkClearValue clearValues[2]{};
    clearValues[0].color = {{0.08f, 0.08f, 0.1f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBeginInfo.renderPass = ctx.render_pass;
    rpBeginInfo.framebuffer = fd.framebuffer;
    rpBeginInfo.renderArea.offset = {0, 0};
    rpBeginInfo.renderArea.extent = ctx.swapchain_extent;
    rpBeginInfo.clearValueCount = 2;
    rpBeginInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (graphicsPipeline && vertexBuffer && indexBuffer) {
        VkViewport viewport{0.0f, 0.0f, (float)ctx.swapchain_extent.width, (float)ctx.swapchain_extent.height, 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, ctx.swapchain_extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        //(Сцена = 0, Модель = 1)
        VkDescriptorSet sets[] = {sceneDescSet, modelDescSet};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, sets, 0, nullptr);

        vkCmdDrawIndexed(cmd, (uint32_t)indices.size(), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

} 