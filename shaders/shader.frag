#version 450

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 dirLightDir;
    vec4 dirLightColor;
} scene;

layout(set = 1, binding = 1) uniform sampler2D albedoMap;
layout(set = 1, binding = 2) uniform sampler2D specularMap;
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;

layout(push_constant) uniform PushConsts {
    float time;
    int nonTrivial;
} pc;

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 uv = fragUV;
    vec4 albedoColor;

    // Нетривиальное параметризованное сэмплирование (Задание 2)
    if (pc.nonTrivial == 1) {
        vec2 uv1 = uv + vec2(sin(pc.time + uv.y * 6.28) * 0.05, cos(pc.time + uv.x * 6.28) * 0.05);
        vec2 uv2 = uv * 2.0 + vec2(pc.time * 0.1);
        vec4 col1 = texture(albedoMap, uv1);
        vec4 col2 = texture(albedoMap, uv2);
        albedoColor = mix(col1, col2, 0.5) * vec4(0.8 + 0.2 * sin(pc.time), 1.0, 0.8 + 0.2 * cos(pc.time), 1.0);
    } else {
        albedoColor = texture(albedoMap, uv);
    }

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(scene.viewPos.xyz - fragPos);
    vec3 L = normalize(-scene.dirLightDir.xyz);

    vec3 ambient = 0.2 * albedoColor.rgb;
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * scene.dirLightColor.rgb * albedoColor.rgb;

    vec3 H = normalize(L + V);
    float specFactor = texture(specularMap, uv).r;
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    vec3 specular = spec * specFactor * scene.dirLightColor.rgb;

    // Emissive текстура добавляется напрямую в обход освещения (Задание 3)
    vec3 emissive = texture(emissiveMap, uv).rgb;

    outColor = vec4(ambient + diffuse + specular + emissive, 1.0);
}