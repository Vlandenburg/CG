#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 ambientLight;
    vec4 dirLightDir;
    vec4 dirLightColor;
    vec4 pointLightPos;
    vec4 pointLightColor;
    vec4 spotLightPos;
    vec4 spotLightDir;
    vec4 spotLightParams; 
} scene;

layout(set = 1, binding = 0) uniform ModelUBO {
    mat4 model;
    vec4 albedo;
    vec4 specularColor;
    float shininess;
} model;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 fragPos;
layout(location = 1) out vec3 fragNormal;

void main() {
    vec4 worldPos = model.model * vec4(inPosition, 1.0);
    gl_Position = scene.proj * scene.view * worldPos;

    fragPos = worldPos.xyz;
    
    fragNormal = normalize(mat3(transpose(inverse(model.model))) * inNormal);
}