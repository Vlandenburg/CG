#include "application.hpp"

#include <imgui.h>
#include <glm/gtc/constants.hpp>

#include <fstream>
#include <vector>
#include <array>
#include <cstring>
#include <iostream>

namespace {

// Геометрия
std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

// Параметры UI
glm::vec3 position = glm::vec3(0.0f);
glm::vec3 rotation = glm::vec3(0.0f);
glm::vec3 scaleVal = glm::vec3(1.0f);

bool isPerspective = true;
float fov = 45.0f;

bool isAnimated = true;
float animTime = 0.0f;
float animSpeed = 1.0f;
float trajRadius = 2.0f;

glm::vec4 sphereColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
bool showSecondObject = true; // Задание 6

// Ресурсы Vulkan
VkBuffer vertexBuffer = VK_NULL_HANDLE;
VmaAllocation vertexBufferAlloc = VK_NULL_HANDLE;

VkBuffer indexBuffer = VK_NULL_HANDLE;
VmaAllocation indexBufferAlloc = VK_NULL_HANDLE;

VkBuffer ubo1 = VK_NULL_HANDLE;
VmaAllocation uboAlloc1 = VK_NULL_HANDLE;
void* uboMapped1 = nullptr;

VkBuffer ubo2 = VK_NULL_HANDLE;
VmaAllocation uboAlloc2 = VK_NULL_HANDLE;
void* uboMapped2 = nullptr;

VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descPool = VK_NULL_HANDLE;
VkDescriptorSet descSet1 = VK_NULL_HANDLE;
VkDescriptorSet descSet2 = VK_NULL_HANDLE;

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline graphicsPipeline = VK_NULL_HANDLE;

// Безопасное чтение SPIR-V файлов
std::vector<char> readFile(const std::string& filename) {
    std::vector<std::string> searchPaths = {
        filename,
        "shaders/" + filename,
        "../shaders/" + filename,
        "../../shaders/" + filename,
        "C:/Users/Vladislav/Desktop/CG/cg-lab1/shaders/" + filename
    };

    for (const auto& path : searchPaths) {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (file.is_open()) {
            size_t fileSize = (size_t)file.tellg();
            std::vector<char> buffer(fileSize);
            file.seekg(0);
            file.read(buffer.data(), fileSize);
            return buffer;
        }
    }
    return {};
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return shaderModule;
}

// Генерация сферы (~100 вершин)
void generateSphere(float radius, unsigned int rings, unsigned int sectors) {
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
            glm::vec3 col = (pos / radius + 1.0f) * 0.5f; // Процедурный цвет (Задание 5)
            vertices.push_back({pos, col});
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

// Выделение буферов через VMA
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

    // 1. Геометрия
    generateSphere(1.0f, 10, 10);

    // 2. Vertex Buffer
    VkDeviceSize vSize = sizeof(Vertex) * vertices.size();
    createBuffer(vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexBufferAlloc);
    void* vData = nullptr;
    if (vmaMapMemory(ctx.allocator, vertexBufferAlloc, &vData) == VK_SUCCESS) {
        memcpy(vData, vertices.data(), vSize);
        vmaUnmapMemory(ctx.allocator, vertexBufferAlloc);
    }

    // 3. Index Buffer
    VkDeviceSize iSize = sizeof(uint32_t) * indices.size();
    createBuffer(iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer, indexBufferAlloc);
    void* iData = nullptr;
    if (vmaMapMemory(ctx.allocator, indexBufferAlloc, &iData) == VK_SUCCESS) {
        memcpy(iData, indices.data(), iSize);
        vmaUnmapMemory(ctx.allocator, indexBufferAlloc);
    }

    // 4. Uniform Buffers (Задание 6: два объекта)
    VkDeviceSize uboSize = sizeof(UniformBufferObject);
    createBuffer(uboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ubo1, uboAlloc1);
    vmaMapMemory(ctx.allocator, uboAlloc1, &uboMapped1);

    createBuffer(uboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ubo2, uboAlloc2);
    vmaMapMemory(ctx.allocator, uboAlloc2, &uboMapped2);

    // 5. Descriptor Layout
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;
    vkCreateDescriptorSetLayout(ctx.device, &layoutInfo, nullptr, &descSetLayout);

    // 6. Descriptor Pool
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;
    vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &descPool);

    // 7. Descriptor Sets
    VkDescriptorSetLayout layouts[] = {descSetLayout, descSetLayout};
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descPool;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    VkDescriptorSet sets[2];
    vkAllocateDescriptorSets(ctx.device, &allocInfo, sets);
    descSet1 = sets[0];
    descSet2 = sets[1];

    VkDescriptorBufferInfo bInfo1{ubo1, 0, sizeof(UniformBufferObject)};
    VkDescriptorBufferInfo bInfo2{ubo2, 0, sizeof(UniformBufferObject)};
    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descSet1;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &bInfo1;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descSet2;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &bInfo2;
    vkUpdateDescriptorSets(ctx.device, 2, writes, 0, nullptr);

    // 8. Pipeline Layout
    VkPipelineLayoutCreateInfo pLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pLayoutInfo.setLayoutCount = 1;
    pLayoutInfo.pSetLayouts = &descSetLayout;
    vkCreatePipelineLayout(ctx.device, &pLayoutInfo, nullptr, &pipelineLayout);

    // 9. Graphics Pipeline
    auto vertCode = readFile("vert.spv");
    auto fragCode = readFile("frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        return false;
    }

    VkShaderModule vertShader = createShaderModule(ctx.device, vertCode);
    VkShaderModule fragShader = createShaderModule(ctx.device, fragCode);

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertShader;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragShader;
    shaderStages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attribDesc[2]{};
    attribDesc[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
    attribDesc[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)};

    VkPipelineVertexInputStateCreateInfo vertInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertInput.vertexBindingDescriptionCount = 1;
    vertInput.pVertexBindingDescriptions = &bindingDesc;
    vertInput.vertexAttributeDescriptionCount = 2;
    vertInput.pVertexAttributeDescriptions = attribDesc;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xf;

    VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipeInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = shaderStages;
    pipeInfo.pVertexInputState = &vertInput;
    pipeInfo.pInputAssemblyState = &inputAssembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &rasterizer;
    pipeInfo.pMultisampleState = &multisampling;
    pipeInfo.pColorBlendState = &colorBlending;
    pipeInfo.pDynamicState = &dynamicState;
    pipeInfo.layout = pipelineLayout;
    pipeInfo.renderPass = ctx.render_pass;
    pipeInfo.pDepthStencilState = &depthStencil;

    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &graphicsPipeline);

    vkDestroyShaderModule(ctx.device, fragShader, nullptr);
    vkDestroyShaderModule(ctx.device, vertShader, nullptr);
    return true;
}

void shutdown() {
    auto& ctx = graphics::internal::context;
    vkQueueWaitIdle(ctx.graphics_queue);

    if (uboMapped1) vmaUnmapMemory(ctx.allocator, uboAlloc1);
    if (uboMapped2) vmaUnmapMemory(ctx.allocator, uboAlloc2);
    if (ubo1) vmaDestroyBuffer(ctx.allocator, ubo1, uboAlloc1);
    if (ubo2) vmaDestroyBuffer(ctx.allocator, ubo2, uboAlloc2);
    if (indexBuffer) vmaDestroyBuffer(ctx.allocator, indexBuffer, indexBufferAlloc);
    if (vertexBuffer) vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertexBufferAlloc);

    if (graphicsPipeline) vkDestroyPipeline(ctx.device, graphicsPipeline, nullptr);
    if (pipelineLayout) vkDestroyPipelineLayout(ctx.device, pipelineLayout, nullptr);
    if (descPool) vkDestroyDescriptorPool(ctx.device, descPool, nullptr);
    if (descSetLayout) vkDestroyDescriptorSetLayout(ctx.device, descSetLayout, nullptr);
}

void update(double time) {
    (void)time;

    // 1. Интерфейс ImGui
    ImGui::Begin("Вариант 5: Сфера");
    if (ImGui::CollapsingHeader("Проекция", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* projs[] = {"Ортографическая", "Перспективная"};
        int cur = isPerspective ? 1 : 0;
        if (ImGui::Combo("Проекция", &cur, projs, 2)) isPerspective = (cur == 1);
        if (isPerspective) ImGui::SliderFloat("FOV", &fov, 20.0f, 90.0f);
    }
    if (ImGui::CollapsingHeader("Трансформации", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Позиция", &position.x, 0.05f);
        ImGui::DragFloat3("Поворот", &rotation.x, 1.0f);
        ImGui::DragFloat3("Масштаб", &scaleVal.x, 0.05f);
    }
    if (ImGui::CollapsingHeader("Анимация (Траектория)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Включить анимацию", &isAnimated);
        ImGui::SliderFloat("Скорость", &animSpeed, 0.1f, 5.0f);
        ImGui::SliderFloat("Радиус", &trajRadius, 0.5f, 5.0f);
    }
    if (ImGui::CollapsingHeader("Цвет & Задание 6", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit4("Цвет сферы 1", &sphereColor.r);
        ImGui::Checkbox("Отображать второй объект (Задание 6)", &showSecondObject);
    }
    ImGui::End();

    // 2. Анимация по сложной траектории
    if (isAnimated) {
        animTime += ImGui::GetIO().DeltaTime * animSpeed;
        position.x = trajRadius * cos(animTime);
        position.y = 0.5f * sin(2.0f * animTime);
        position.z = trajRadius * sin(animTime);
        rotation.y += animSpeed * 45.0f * ImGui::GetIO().DeltaTime;
    }

    // 3. Вычисление матриц
    glm::mat4 model1 = glm::translate(glm::mat4(1.0f), position);
    model1 = glm::rotate(model1, glm::radians(rotation.x), glm::vec3(1, 0, 0));
    model1 = glm::rotate(model1, glm::radians(rotation.y), glm::vec3(0, 1, 0));
    model1 = glm::rotate(model1, glm::radians(rotation.z), glm::vec3(0, 0, 1));
    model1 = glm::scale(model1, scaleVal);

    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 3.0f, 6.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    auto extent = graphics::internal::context.swapchain_extent;
    float aspect = (extent.height > 0) ? (float)extent.width / (float)extent.height : 1.0f;
    glm::mat4 proj = isPerspective ? glm::perspective(glm::radians(fov), aspect, 0.1f, 100.0f)
                                   : glm::ortho(-3.0f * aspect, 3.0f * aspect, -3.0f, 3.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f; // Инверсия Y для Vulkan

    // Запись в UBO 1
    if (uboMapped1) {
        UniformBufferObject uboData1{model1, view, proj, sphereColor};
        memcpy(uboMapped1, &uboData1, sizeof(uboData1));
    }

    // Запись в UBO 2
    if (uboMapped2) {
        glm::mat4 model2 = glm::translate(glm::mat4(1.0f), glm::vec3(2.5f, 0.0f, 0.0f));
        UniformBufferObject uboData2{model2, view, proj, glm::vec4(0.3f, 0.8f, 0.4f, 1.0f)};
        memcpy(uboMapped2, &uboData2, sizeof(uboData2));
    }
}

void render(const graphics::internal::FrameData& fd) {
    auto& ctx = graphics::internal::context;
    VkCommandBuffer cmd = fd.command_buffer;

    // 1. Сброс и начало записи командного буфера
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo cmdBeginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cmdBeginInfo);

    // 2. Настройка очистки и запуск Render Pass
    VkClearValue clearValues[2]{};
    clearValues[0].color = {{0.12f, 0.12f, 0.15f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBeginInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBeginInfo.renderPass = ctx.render_pass;
    rpBeginInfo.framebuffer = fd.framebuffer;
    rpBeginInfo.renderArea.offset = {0, 0};
    rpBeginInfo.renderArea.extent = ctx.swapchain_extent;
    rpBeginInfo.clearValueCount = 2;
    rpBeginInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 3. Отрисовка геометрии
    if (graphicsPipeline && vertexBuffer && indexBuffer) {
        VkViewport viewport{0.0f, 0.0f, (float)ctx.swapchain_extent.width, (float)ctx.swapchain_extent.height, 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, ctx.swapchain_extent};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        // Рисуем сферу 1
        if (descSet1 != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descSet1, 0, nullptr);
            vkCmdDrawIndexed(cmd, (uint32_t)indices.size(), 1, 0, 0, 0);
        }

        // Рисуем сферу 2 (Задание 6)
        if (showSecondObject && descSet2 != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descSet2, 0, nullptr);
            vkCmdDrawIndexed(cmd, (uint32_t)indices.size(), 1, 0, 0, 0);
        }
    }

    // 4. Завершение Render Pass и фиксация буфера
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

} // namespace application