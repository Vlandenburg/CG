#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 dirLightDir;
    vec4 dirLightColor;
} scene;

layout(set = 1, binding = 0) uniform ModelUBO {
    mat4 model;
} model;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;

void main() {
    vec4 worldPos = model.model * vec4(inPosition, 1.0);
    gl_Position = scene.proj * scene.view * worldPos;
    fragPos = worldPos.xyz;
    fragNormal = normalize(mat3(transpose(inverse(model.model))) * inNormal);
    fragUV = inUV;
}