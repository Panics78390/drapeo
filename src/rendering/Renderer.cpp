#include "rendering/Renderer.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

#include "geometry/ClothMesh.h"

namespace drapeo::rendering {

namespace {

const char* kVertexShader = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aScalar;     // étirement (tissu) ; 0 ailleurs
layout(location = 3) in vec3 aColor;       // couleur de la pièce (sRGB)

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out float vScalar;
out vec3 vColor;

void main() {
    vColor = aColor;
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(uModel) * aNormal;      // échelle uniforme uniquement
    vScalar = aScalar;
    gl_Position = uViewProj * world;
}
)GLSL";

const char* kFragmentShader = R"GLSL(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in float vScalar;
in vec3 vColor;
out vec4 fragColor;
uniform int   uVertexColor;   // 1 = couleur par sommet

uniform int   uMode;          // 0 = tissu, 1 = obstacle, 2 = sol
uniform vec3  uCameraPos;
uniform vec3  uLightDir;      // direction vers laquelle la lumière se propage
uniform vec3  uBaseColor;     // sRGB
uniform float uRoughness;
uniform float uSheen;
uniform int   uShowStrain;
uniform float uStrainScale;
uniform int   uBackSide;      // 1 = envers du tissu teinté
uniform vec3  uSphereCenter;  // pour l'ombre analytique sur le sol
uniform float uSphereRadius;

const float PI = 3.14159265;

vec3 heatmap(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 cold = vec3(0.16, 0.36, 0.86), mid = vec3(0.22, 0.78, 0.42), hot = vec3(0.92, 0.22, 0.16);
    return t < 0.5 ? mix(cold, mid, t * 2.0) : mix(mid, hot, (t - 0.5) * 2.0);
}

float distributionGGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float visibilitySmith(float NoV, float NoL, float a) {
    float k = a * 0.5;
    return 0.25 / ((NoV * (1.0 - k) + k) * (NoL * (1.0 - k) + k));
}

vec3 fresnelSchlick(float VoH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
}

void main() {
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;                       // tissu double face
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 L = normalize(-uLightDir);
    vec3 H = normalize(V + L);

    vec3 albedo = uVertexColor == 1 ? vColor : uBaseColor;
    float shadow = 1.0;
    if (uMode == 2) {
        // Sol : damier de 50 cm qui s'estompe avec la distance.
        vec2 cell = floor(vWorldPos.xz * 2.0);
        float checker = mod(cell.x + cell.y, 2.0);
        albedo = mix(vec3(0.30, 0.31, 0.34), vec3(0.36, 0.37, 0.40), checker);
        // Ombre douce de la sphère : distance du rayon vers la lumière au centre.
        vec3 toC = uSphereCenter - vWorldPos;
        float t = max(dot(toC, L), 0.0);
        float dist = length(toC - L * t);
        shadow = mix(0.35, 1.0, smoothstep(uSphereRadius * 0.6, uSphereRadius * 1.25, dist));
    }
    if (uMode == 0 && uShowStrain == 1) albedo = heatmap(vScalar / uStrainScale);
    // Envers du tissu (face tournée vers le corps) : plus sombre et bleuté, pour reconnaître l'endroit.
    if (uMode == 0 && uBackSide == 1 && !gl_FrontFacing) albedo = mix(albedo * 0.55, vec3(0.22, 0.30, 0.46), 0.45);

    vec3 base = pow(albedo, vec3(2.2));                 // sRGB -> linéaire
    float a = max(uRoughness * uRoughness, 0.03);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 1e-3);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 F = fresnelSchlick(VoH, vec3(0.04));
    vec3 specular = distributionGGX(NoH, a) * visibilitySmith(NoV, NoL, a) * F;
    vec3 diffuse = (1.0 - F) * base / PI;
    vec3 sheen = uSheen * base * pow(1.0 - NoV, 3.0);   // reflet rasant des fibres

    vec3 sun = vec3(3.2, 3.05, 2.85);
    vec3 color = (diffuse + specular) * sun * NoL * shadow;
    // Ciel (hémisphère) + contre-jour froid
    vec3 sky = mix(vec3(0.10, 0.09, 0.08), vec3(0.32, 0.36, 0.44), 0.5 + 0.5 * N.y);
    color += base * sky + sheen * 1.2;
    color += base * 0.25 * max(dot(N, normalize(vec3(-0.5, 0.2, -0.8))), 0.0) * vec3(0.55, 0.65, 0.9);

    if (uMode == 2) {
        float fade = exp(-0.06 * dot(vWorldPos.xz, vWorldPos.xz));
        color = mix(vec3(0.0070, 0.0078, 0.0094), color, fade);   // = couleur du fond après tone mapping
    }

    color = color / (color + vec3(1.0));                // tone mapping de Reinhard
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

} // namespace

Renderer::~Renderer()
{
    if (gl::DeleteBuffers == nullptr) return;
    destroyMesh(cloth_);
    destroyMesh(sphere_);
    destroyMesh(ground_);
    destroyMesh(avatar_);
}

void Renderer::setAvatarMesh(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& normals,
                             const std::vector<uint32_t>& indices)
{
    createMesh(avatar_, positions, normals, indices, gl::STATIC_DRAW);
}

bool Renderer::init(std::string& error)
{
    if (!shader_.create(kVertexShader, kFragmentShader, error)) return false;

    std::vector<glm::vec3> pos;
    std::vector<uint32_t> idx;
    geometry::makeUvSphere(48, 96, pos, idx);
    createMesh(sphere_, pos, pos, idx, gl::STATIC_DRAW);   // sphère unité : normale = position

    const float s = 12.0f;
    const std::vector<glm::vec3> gp{{-s, 0, -s}, {s, 0, -s}, {s, 0, s}, {-s, 0, s}};
    const std::vector<glm::vec3> gn(4, glm::vec3(0, 1, 0));
    createMesh(ground_, gp, gn, {0, 2, 1, 0, 3, 2}, gl::STATIC_DRAW);
    return true;
}

void Renderer::createMesh(GpuMesh& mesh, const std::vector<glm::vec3>& positions,
                          const std::vector<glm::vec3>& normals, const std::vector<uint32_t>& indices,
                          gl::GLenum usage)
{
    destroyMesh(mesh);
    mesh.vertexCount = positions.size();
    mesh.indexCount = static_cast<gl::GLsizei>(indices.size());
    const std::vector<float> zeros(positions.size(), 0.0f);

    gl::GenVertexArrays(1, &mesh.vao);
    gl::BindVertexArray(mesh.vao);

    auto attribute = [&](gl::GLuint& buffer, gl::GLuint location, int components, const void* data,
                         std::size_t bytes) {
        gl::GenBuffers(1, &buffer);
        gl::BindBuffer(gl::ARRAY_BUFFER, buffer);
        gl::BufferData(gl::ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(bytes), data, usage);
        gl::VertexAttribPointer(location, components, gl::FLOAT, gl::FALSE_, 0, nullptr);
        gl::EnableVertexAttribArray(location);
    };
    attribute(mesh.position, 0, 3, positions.data(), positions.size() * sizeof(glm::vec3));
    attribute(mesh.normal, 1, 3, normals.data(), normals.size() * sizeof(glm::vec3));
    attribute(mesh.scalar, 2, 1, zeros.data(), zeros.size() * sizeof(float));
    const std::vector<glm::vec3> white(positions.size(), glm::vec3(0.8f));
    attribute(mesh.color, 3, 3, white.data(), white.size() * sizeof(glm::vec3));

    gl::GenBuffers(1, &mesh.index);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, mesh.index);
    gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                   indices.data(), gl::STATIC_DRAW);
    gl::BindVertexArray(0);
}

void Renderer::destroyMesh(GpuMesh& mesh)
{
    if (mesh.vao == 0) return;
    const gl::GLuint buffers[] = {mesh.position, mesh.normal, mesh.scalar, mesh.color, mesh.index};
    gl::DeleteBuffers(5, buffers);
    gl::DeleteVertexArrays(1, &mesh.vao);
    mesh = {};
}

void Renderer::drawMesh(const GpuMesh& mesh)
{
    gl::BindVertexArray(mesh.vao);
    gl::DrawElements(gl::TRIANGLES, mesh.indexCount, gl::UNSIGNED_INT, nullptr);
}

void Renderer::setClothTopology(const std::vector<uint32_t>& indices, std::size_t vertexCount)
{
    const std::vector<glm::vec3> zeros(vertexCount, glm::vec3(0.0f));
    createMesh(cloth_, zeros, zeros, indices, gl::DYNAMIC_DRAW);
    clothHasColors_ = false;
}

void Renderer::setClothColors(const std::vector<glm::vec3>& colors)
{
    clothHasColors_ = !colors.empty() && colors.size() == cloth_.vertexCount;
    if (!clothHasColors_) return;
    gl::BindBuffer(gl::ARRAY_BUFFER, cloth_.color);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, static_cast<gl::GLsizeiptr>(colors.size() * sizeof(glm::vec3)), colors.data());
}

void Renderer::updateCloth(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& normals,
                           const std::vector<float>& strain)
{
    if (cloth_.vao == 0 || positions.size() != cloth_.vertexCount) return;
    gl::BindBuffer(gl::ARRAY_BUFFER, cloth_.position);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, static_cast<gl::GLsizeiptr>(positions.size() * sizeof(glm::vec3)),
                      positions.data());
    gl::BindBuffer(gl::ARRAY_BUFFER, cloth_.normal);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, static_cast<gl::GLsizeiptr>(normals.size() * sizeof(glm::vec3)),
                      normals.data());
    gl::BindBuffer(gl::ARRAY_BUFFER, cloth_.scalar);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, static_cast<gl::GLsizeiptr>(strain.size() * sizeof(float)),
                      strain.data());
}

void Renderer::render(const SceneView& scene, const ClothAppearance& cloth)
{
    gl::Viewport(scene.x, scene.y, scene.width, scene.height);
    gl::Enable(gl::SCISSOR_TEST);
    gl::Scissor(scene.x, scene.y, scene.width, scene.height);
    gl::ClearColor(0.105f, 0.112f, 0.125f, 1.0f);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    gl::Disable(gl::SCISSOR_TEST);
    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::CULL_FACE);

    const float aspect = static_cast<float>(scene.width) / static_cast<float>(std::max(scene.height, 1));
    const glm::mat4 viewProj = scene.camera->projection(aspect) * scene.camera->view();

    shader_.use();
    shader_.set("uViewProj", viewProj);
    shader_.set("uCameraPos", scene.camera->position());
    shader_.set("uLightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.35f)));
    shader_.set("uShowStrain", 0);
    shader_.set("uBackSide", 0);
    shader_.set("uVertexColor", 0);

    const bool hasSphere = scene.spheres != nullptr && !scene.spheres->empty();
    shader_.set("uSphereCenter", hasSphere ? scene.spheres->front().center : glm::vec3(0, -100, 0));
    shader_.set("uSphereRadius", hasSphere ? scene.spheres->front().radius : 0.0f);

    // Sol
    if (scene.groundEnabled) {
        shader_.set("uMode", 2);
        shader_.set("uModel", glm::translate(glm::mat4(1.0f), glm::vec3(0, scene.groundHeight, 0)));
        shader_.set("uRoughness", 0.9f);
        shader_.set("uSheen", 0.0f);
        shader_.set("uBaseColor", glm::vec3(0.5f));
        drawMesh(ground_);
    }

    // Obstacles
    if (scene.spheres != nullptr) {
        shader_.set("uMode", 1);
        shader_.set("uBaseColor", glm::vec3(0.62f, 0.60f, 0.57f));
        shader_.set("uRoughness", 0.45f);
        shader_.set("uSheen", 0.0f);
        for (const auto& s : *scene.spheres) {
            // Rendu très légèrement plus petit : le tissu flotte à `thickness` au-dessus.
            glm::mat4 m = glm::translate(glm::mat4(1.0f), s.center);
            m = glm::scale(m, glm::vec3(s.radius * 0.995f));
            shader_.set("uModel", m);
            drawMesh(sphere_);
        }
    }

    // Avatar : gris chaud mat, comme un mannequin de couture
    if (avatar_.vao != 0) {
        shader_.set("uMode", 1);
        shader_.set("uBaseColor", glm::vec3(0.72f, 0.68f, 0.63f));
        shader_.set("uRoughness", 0.6f);
        shader_.set("uSheen", 0.0f);
        shader_.set("uModel", glm::mat4(1.0f));
        drawMesh(avatar_);
    }

    // Tissu
    shader_.set("uMode", 0);
    shader_.set("uModel", glm::mat4(1.0f));
    shader_.set("uBaseColor", cloth.color);
    shader_.set("uRoughness", cloth.roughness);
    shader_.set("uSheen", cloth.sheen);
    shader_.set("uShowStrain", cloth.showStrain ? 1 : 0);
    shader_.set("uVertexColor", clothHasColors_ ? 1 : 0);
    shader_.set("uStrainScale", cloth.strainScale);
    shader_.set("uBackSide", cloth.showBackSide ? 1 : 0);
    if (cloth.wireframe) gl::PolygonMode(gl::FRONT_AND_BACK, gl::LINE);
    drawMesh(cloth_);
    if (cloth.wireframe) gl::PolygonMode(gl::FRONT_AND_BACK, gl::FILL);
    shader_.set("uVertexColor", 0);
    gl::BindVertexArray(0);
}

} // namespace drapeo::rendering
