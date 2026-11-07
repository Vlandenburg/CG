#include "application.hpp"
#include <imgui.h>
#include <glm/gtc/constants.hpp>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <fstream>
#include <vector>
#include <array>
#include <cstring>
#include <cmath>

namespace {

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

glm::vec3 camPos = glm::vec3(0.0f, 0.0f, 3.5f);
float camYaw = -90.0f;
float camPitch = 0.0f;
float camFov = 45.0f;

float animTime = 0.0f;
int useNonTrivial = 0;
int selectedTexture = 0;

VkBuffer vertexBuffer = VK_NULL_HANDLE;
VmaAllocation vertexAlloc = VK_NULL_HANDLE;
VkBuffer indexBuffer = VK_NULL_HANDLE;
VmaAllocation indexAlloc = VK_NULL_HANDLE;

VkBuffer sceneUboBuffer = VK_NULL_HANDLE;
VmaAllocation sceneUboAlloc = VK_NULL_HANDLE;
void* sceneUboMapped = nullptr;

VkBuffer modelUboBuffer = VK_NULL_HANDLE;
VmaAllocation modelUboAlloc = VK_NULL_HANDLE;
void* modelUboMapped = nullptr;

struct TextureResource {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

TextureResource albedoTex1;
TextureResource albedoTex2;
TextureResource specTex;
TextureResource emissiveTex;
TextureResource blackDummyTex;
VkSampler textureSampler = VK_NULL_HANDLE;

VkDescriptorSetLayout sceneLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout modelLayout = VK_NULL_HANDLE;
VkDescriptorPool descPool = VK_NULL_HANDLE;
VkDescriptorSet sceneSet = VK_NULL_HANDLE;
VkDescriptorSet matSet1 = VK_NULL_HANDLE;
VkDescriptorSet matSet2 = VK_NULL_HANDLE;

VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkPipeline graphicsPipeline = VK_NULL_HANDLE;

std::vector<char> readFile(const std::string& filename) {
    std::vector<std::string> paths = { filename, "shaders/" + filename, "../shaders/" + filename, "C:/Users/Vladislav/Desktop/CG/cg-lab1/shaders/" + filename };
    for (const auto& p : paths) {
        std::ifstream file(p, std::ios::ate | std::ios::binary);
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

// Создание буферов в GPU VRAM через Staging Buffer
void createDeviceLocalBuffer(const void* data, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& dstBuffer, VmaAllocation& dstAlloc) {
    auto& ctx = graphics::internal::context;

    VkBuffer stagingBuf;
    VmaAllocation stagingAlloc;
    VkBufferCreateInfo stgInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    stgInfo.size = size;
    stgInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo stgAllocInfo{};
    stgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stgAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vmaCreateBuffer(ctx.allocator, &stgInfo, &stgAllocInfo, &stagingBuf, &stagingAlloc, nullptr);

    void* mapped;
    vmaMapMemory(ctx.allocator, stagingAlloc, &mapped);
    memcpy(mapped, data, size);
    vmaUnmapMemory(ctx.allocator, stagingAlloc);

    VkBufferCreateInfo dstInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    dstInfo.size = size;
    dstInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo devAllocInfo{};
    devAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vmaCreateBuffer(ctx.allocator, &dstInfo, &devAllocInfo, &dstBuffer, &dstAlloc, nullptr);

    VkCommandPool cmdPool;
    VkCommandPoolCreateInfo cpInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, ctx.graphics_queue_index};
    vkCreateCommandPool(ctx.device, &cpInfo, nullptr, &cmdPool);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    vkAllocateCommandBuffers(ctx.device, &cbInfo, &cmd);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferCopy copyRegion{0, 0, size};
    vkCmdCopyBuffer(cmd, stagingBuf, dstBuffer, 1, &copyRegion);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
    vkQueueSubmit(ctx.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphics_queue);

    vkDestroyCommandPool(ctx.device, cmdPool, nullptr);
    vmaDestroyBuffer(ctx.allocator, stagingBuf, stagingAlloc);
}

// Генерация сферы с нормалями и UV-координатами
void generateSphereWithUV(float radius, unsigned int rings, unsigned int sectors) {
    vertices.clear();
    indices.clear();
    for (unsigned int r = 0; r <= rings; ++r) {
        float v = (float)r / (float)rings;
        float theta = glm::pi<float>() * v;
        for (unsigned int s = 0; s <= sectors; ++s) {
            float u = (float)s / (float)sectors;
            float phi = 2.0f * glm::pi<float>() * u;

            glm::vec3 pos(radius * sin(theta) * cos(phi), radius * cos(theta), radius * sin(theta) * sin(phi));
            vertices.push_back({ pos, glm::normalize(pos), glm::vec2(u, v) });
        }
    }
    for (unsigned int r = 0; r < rings; ++r) {
        for (unsigned int s = 0; s < sectors; ++s) {
            uint32_t cur = r * (sectors + 1) + s;
            uint32_t next = cur + sectors + 1;
            indices.push_back(cur); indices.push_back(next); indices.push_back(cur + 1);
            indices.push_back(cur + 1); indices.push_back(next); indices.push_back(next + 1);
        }
    }
}

// Загрузка текстуры из памяти или файла в VRAM
TextureResource loadTexture(const unsigned char* pixels, int width, int height) {
    auto& ctx = graphics::internal::context;
    VkDeviceSize imgSize = width * height * 4;

    VkBuffer stgBuf;
    VmaAllocation stgAlloc;
    VkBufferCreateInfo stgInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    VmaAllocationCreateInfo stgAllocInfo{};
    stgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    stgAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vmaCreateBuffer(ctx.allocator, &stgInfo, &stgAllocInfo, &stgBuf, &stgAlloc, nullptr);

    void* mapped;
    vmaMapMemory(ctx.allocator, stgAlloc, &mapped);
    memcpy(mapped, pixels, imgSize);
    vmaUnmapMemory(ctx.allocator, stgAlloc);

    TextureResource res;
    VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = {(uint32_t)width, (uint32_t)height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    VmaAllocationCreateInfo imgAllocInfo{};
    imgAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vmaCreateImage(ctx.allocator, &imgInfo, &imgAllocInfo, &res.image, &res.alloc, nullptr);

    VkCommandPool cmdPool;
    VkCommandPoolCreateInfo cpInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, ctx.graphics_queue_index};
    vkCreateCommandPool(ctx.device, &cpInfo, nullptr, &cmdPool);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
    vkAllocateCommandBuffers(ctx.device, &cbInfo, &cmd);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = res.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {(uint32_t)width, (uint32_t)height, 1}};
    vkCmdCopyBufferToImage(cmd, stgBuf, res.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cmd, 0, nullptr};
    vkQueueSubmit(ctx.graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphics_queue);

    vkDestroyCommandPool(ctx.device, cmdPool, nullptr);
    vmaDestroyBuffer(ctx.allocator, stgBuf, stgAlloc);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = res.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx.device, &viewInfo, nullptr, &res.view);

    return res;
}

// Загрузка текстуры из файловой системы с автогенерацией при отсутствии
TextureResource loadOrCreateFileTexture(const std::string& filename, int type) {
    int w, h, channels;
    unsigned char* data = stbi_load(filename.c_str(), &w, &h, &channels, 4);
    if (!data) {
        w = 128; h = 128;
        std::vector<unsigned char> gen(w * h * 4);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idx = (y * w + x) * 4;
                if (type == 0) { // Checkered Albedo 1
                    bool c = ((x / 16) + (y / 16)) % 2 == 0;
                    gen[idx] = c ? 220 : 30; gen[idx+1] = c ? 50 : 180; gen[idx+2] = c ? 50 : 220; gen[idx+3] = 255;
                } else if (type == 1) { // Striped Albedo 2
                    gen[idx] = (x % 32 < 16) ? 240 : 20; gen[idx+1] = 200; gen[idx+2] = (y % 32 < 16) ? 240 : 40; gen[idx+3] = 255;
                } else if (type == 2) { // Specular Map
                    unsigned char val = ((x % 32 < 8) || (y % 32 < 8)) ? 255 : 30;
                    gen[idx] = val; gen[idx+1] = val; gen[idx+2] = val; gen[idx+3] = 255;
                } else if (type == 3) { // Emissive Glow Map
                    bool glow = ((x - 64)*(x - 64) + (y - 64)*(y - 64) < 30*30);
                    gen[idx] = glow ? 40 : 0; gen[idx+1] = glow ? 220 : 0; gen[idx+2] = glow ? 255 : 0; gen[idx+3] = 255;
                }
            }
        }
        stbi_write_png(filename.c_str(), w, h, 4, gen.data(), w * 4);
        data = stbi_load(filename.c_str(), &w, &h, &channels, 4);
    }
    TextureResource res = loadTexture(data, w, h);
    stbi_image_free(data);
    return res;
}

} // namespace

namespace application {

bool initialize() {
    auto& ctx = graphics::internal::context;

    generateSphereWithUV(1.0f, 16, 16);
    createDeviceLocalBuffer(vertices.data(), sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer, vertexAlloc);
    createDeviceLocalBuffer(indices.data(), sizeof(uint32_t) * indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer, indexAlloc);

    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, sizeof(SceneUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
    VmaAllocationCreateInfo aInfo{}; aInfo.usage = VMA_MEMORY_USAGE_AUTO; aInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    vmaCreateBuffer(ctx.allocator, &bufInfo, &aInfo, &sceneUboBuffer, &sceneUboAlloc, nullptr);
    vmaMapMemory(ctx.allocator, sceneUboAlloc, &sceneUboMapped);

    bufInfo.size = sizeof(ModelUBO);
    vmaCreateBuffer(ctx.allocator, &bufInfo, &aInfo, &modelUboBuffer, &modelUboAlloc, nullptr);
    vmaMapMemory(ctx.allocator, modelUboAlloc, &modelUboMapped);

    // Загрузка текстур из файлов на диске
    albedoTex1 = loadOrCreateFileTexture("albedo1.png", 0);
    albedoTex2 = loadOrCreateFileTexture("albedo2.png", 1);
    specTex = loadOrCreateFileTexture("specular.png", 2);
    emissiveTex = loadOrCreateFileTexture("emissive.png", 3);
    uint32_t black = 0xFF000000;
    blackDummyTex = loadTexture((unsigned char*)&black, 1, 1);

    VkSamplerCreateInfo sInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sInfo.magFilter = VK_FILTER_LINEAR; sInfo.minFilter = VK_FILTER_LINEAR;
    sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkCreateSampler(ctx.device, &sInfo, nullptr, &textureSampler);

    // Наборы дескрипторов (Set 0: Сцена, Set 1: Материал)
    VkDescriptorSetLayoutBinding b0{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo slInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &b0};
    vkCreateDescriptorSetLayout(ctx.device, &slInfo, nullptr, &sceneLayout);

    VkDescriptorSetLayoutBinding mb[4] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
    };
    VkDescriptorSetLayoutCreateInfo mlInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 4, mb};
    vkCreateDescriptorSetLayout(ctx.device, &mlInfo, nullptr, &modelLayout);

    VkDescriptorPoolSize pSizes[] = { {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16} };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 4, 2, pSizes};
    vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &descPool);

    VkDescriptorSetAllocateInfo saInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descPool, 1, &sceneLayout};
    vkAllocateDescriptorSets(ctx.device, &saInfo, &sceneSet);
    saInfo.pSetLayouts = &modelLayout;
    vkAllocateDescriptorSets(ctx.device, &saInfo, &matSet1);
    vkAllocateDescriptorSets(ctx.device, &saInfo, &matSet2);

    VkDescriptorBufferInfo bSceneInfo{sceneUboBuffer, 0, sizeof(SceneUBO)};
    VkWriteDescriptorSet wScene{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sceneSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bSceneInfo, nullptr};
    vkUpdateDescriptorSets(ctx.device, 1, &wScene, 0, nullptr);

    auto updateMaterialSet = [&](VkDescriptorSet set, VkImageView alb, VkImageView spc, VkImageView emi) {
        VkDescriptorBufferInfo bModelInfo{modelUboBuffer, 0, sizeof(ModelUBO)};
        VkDescriptorImageInfo iAlb{textureSampler, alb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo iSpc{textureSampler, spc, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorImageInfo iEmi{textureSampler, emi, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr[4] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bModelInfo, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &iAlb, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &iSpc, nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &iEmi, nullptr, nullptr}
        };
        vkUpdateDescriptorSets(ctx.device, 4, wr, 0, nullptr);
    };
    updateMaterialSet(matSet1, albedoTex1.view, specTex.view, emissiveTex.view);
    updateMaterialSet(matSet2, albedoTex2.view, blackDummyTex.view, blackDummyTex.view);

    // Push Constants для параметров нетривиального сэмплирования
    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlock)};
    VkDescriptorSetLayout layouts[] = {sceneLayout, modelLayout};
    VkPipelineLayoutCreateInfo plInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 2, layouts, 1, &pcr};
    vkCreatePipelineLayout(ctx.device, &plInfo, nullptr, &pipelineLayout);

    auto vert = readFile("vert.spv");
    auto frag = readFile("frag.spv");
    VkShaderModuleCreateInfo smInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, vert.size(), (uint32_t*)vert.data()};
    VkShaderModule vMod, fMod;
    vkCreateShaderModule(ctx.device, &smInfo, nullptr, &vMod);
    smInfo.codeSize = frag.size(); smInfo.pCode = (uint32_t*)frag.data();
    vkCreateShaderModule(ctx.device, &smInfo, nullptr, &fMod);

    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vMod, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main", nullptr}
    };

    VkVertexInputBindingDescription bd{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription ad[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, nullptr, 0, 1, &bd, 3, ad};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr};
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f};
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, nullptr, VK_FALSE, VK_FALSE};
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_FALSE, VK_FALSE, {}, {}, 0.0f, 1.0f};
    VkPipelineColorBlendAttachmentState cba{}; cba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &cba, {0,0,0,0}};
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dys{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dyn};

    VkGraphicsPipelineCreateInfo pInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0, 2, stages, &vi, &ia, nullptr, &vp, &rs, &ms, &ds, &cb, &dys, pipelineLayout, ctx.render_pass, 0, VK_NULL_HANDLE, 0};
    vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &graphicsPipeline);

    vkDestroyShaderModule(ctx.device, fMod, nullptr);
    vkDestroyShaderModule(ctx.device, vMod, nullptr);
    return true;
}

void shutdown() {
    auto& ctx = graphics::internal::context;
    vkQueueWaitIdle(ctx.graphics_queue);

    auto destroyTex = [&](TextureResource& t) {
        if (t.view) vkDestroyImageView(ctx.device, t.view, nullptr);
        if (t.image) vmaDestroyImage(ctx.allocator, t.image, t.alloc);
    };
    destroyTex(albedoTex1); destroyTex(albedoTex2); destroyTex(specTex); destroyTex(emissiveTex); destroyTex(blackDummyTex);
    if (textureSampler) vkDestroySampler(ctx.device, textureSampler, nullptr);

    vmaUnmapMemory(ctx.allocator, modelUboAlloc);
    vmaUnmapMemory(ctx.allocator, sceneUboAlloc);
    vmaDestroyBuffer(ctx.allocator, modelUboBuffer, modelUboAlloc);
    vmaDestroyBuffer(ctx.allocator, sceneUboBuffer, sceneUboAlloc);
    vmaDestroyBuffer(ctx.allocator, indexBuffer, indexAlloc);
    vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertexAlloc);

    vkDestroyPipeline(ctx.device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(ctx.device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(ctx.device, descPool, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, modelLayout, nullptr);
    vkDestroyDescriptorSetLayout(ctx.device, sceneLayout, nullptr);
}

void update(double time) {
    (void)time;
    animTime += ImGui::GetIO().DeltaTime;

    ImGui::Begin("Lab 3: Texturing");
    ImGui::Combo("Material / Texture", &selectedTexture, "Material 1 (Albedo+Spec+Emissive)\0Material 2 (Striped Albedo Only)\0");
    ImGui::Checkbox("Non-trivial Sampling (Push Constant)", (bool*)&useNonTrivial);
    ImGui::DragFloat3("Camera Position", &camPos.x, 0.05f);
    ImGui::SliderFloat("Camera Yaw", &camYaw, -180.0f, 180.0f);
    ImGui::SliderFloat("Camera Pitch", &camPitch, -89.0f, 89.0f);
    ImGui::End();

    glm::vec3 front(cos(glm::radians(camYaw)) * cos(glm::radians(camPitch)), sin(glm::radians(camPitch)), sin(glm::radians(camYaw)) * cos(glm::radians(camPitch)));
    front = glm::normalize(front);
    glm::mat4 view = glm::lookAt(camPos, camPos + front, glm::vec3(0.0f, 1.0f, 0.0f));

    auto extent = graphics::internal::context.swapchain_extent;
    float aspect = (extent.height > 0) ? (float)extent.width / (float)extent.height : 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(camFov), aspect, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;

    SceneUBO sUbo{ view, proj, glm::vec4(camPos, 1.0f), glm::vec4(-1.0f, -1.5f, -1.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) };
    memcpy(sceneUboMapped, &sUbo, sizeof(sUbo));

    glm::mat4 model = glm::rotate(glm::mat4(1.0f), animTime * 0.3f, glm::vec3(0, 1, 0));
    ModelUBO mUbo{ model };
    memcpy(modelUboMapped, &mUbo, sizeof(mUbo));
}

void render(const graphics::internal::FrameData& fd) {
    auto& ctx = graphics::internal::context;
    VkCommandBuffer cmd = fd.command_buffer;

    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, nullptr};
    vkBeginCommandBuffer(cmd, &bi);

    VkClearValue cv[2]{};
    cv[0].color = {{0.08f, 0.08f, 0.1f, 1.0f}};
    cv[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, ctx.render_pass, fd.framebuffer, {{0, 0}, ctx.swapchain_extent}, 2, cv};
    vkCmdBeginRenderPass(cmd, &rpi, VK_SUBPASS_CONTENTS_INLINE);

    if (graphicsPipeline) {
        VkViewport vp{0.0f, 0.0f, (float)ctx.swapchain_extent.width, (float)ctx.swapchain_extent.height, 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, ctx.swapchain_extent};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        PushConstBlock pc{animTime, useNonTrivial};
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlock), &pc);

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        VkDescriptorSet sets[] = {sceneSet, (selectedTexture == 0) ? matSet1 : matSet2};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2, sets, 0, nullptr);

        vkCmdDrawIndexed(cmd, (uint32_t)indices.size(), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

} // namespace application