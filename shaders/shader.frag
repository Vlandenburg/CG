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
    vec4 spotLightParams; // x: inner cos, y: outer cos, z: intensity
} scene;

layout(set = 1, binding = 0) uniform ModelUBO {
    mat4 model;
    vec4 albedo;
    vec4 specularColor;
    float shininess;
} model;

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

vec3 CalcBlinnPhong(vec3 L, vec3 lightColor, float intensity, vec3 N, vec3 V) {
    // Diffuse
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * lightColor * intensity * model.albedo.rgb;

    // Specular (Blinn-Phong)
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), model.shininess);
    vec3 specular = spec * model.specularColor.rgb * lightColor * intensity;

    return diffuse + specular;
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(scene.viewPos.xyz - fragPos);

    vec3 result = scene.ambientLight.rgb * scene.ambientLight.a * model.albedo.rgb;

    vec3 dirL = normalize(-scene.dirLightDir.xyz);
    result += CalcBlinnPhong(dirL, scene.dirLightColor.rgb, scene.dirLightDir.w, N, V);

    // Point Light 
    vec3 pointLVec = scene.pointLightPos.xyz - fragPos;
    float dPoint = length(pointLVec);
    vec3 pointL = normalize(pointLVec);
    float pointAtten = 1.0 / (1.0 + 0.1 * dPoint + 0.2 * dPoint * dPoint);
    result += CalcBlinnPhong(pointL, scene.pointLightColor.rgb, scene.pointLightPos.w * pointAtten, N, V);

    // 4. Spotlight 
    vec3 spotLVec = scene.spotLightPos.xyz - fragPos;
    float dSpot = length(spotLVec);
    vec3 spotL = normalize(spotLVec);
    float spotAtten = 1.0 / (1.0 + 0.1 * dSpot + 0.2 * dSpot * dSpot);

    float theta = dot(spotL, normalize(-scene.spotLightDir.xyz));
    float epsilon = scene.spotLightParams.x - scene.spotLightParams.y;
    float spotIntensity = clamp((theta - scene.spotLightParams.y) / max(epsilon, 0.0001), 0.0, 1.0);

    result += CalcBlinnPhong(spotL, vec3(1.0), scene.spotLightParams.z * spotAtten * spotIntensity, N, V);

    outColor = vec4(result, 1.0);
}