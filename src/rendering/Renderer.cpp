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
layout(location = 4) in vec2 aUv;          // coordonnées du patron (m)
layout(location = 5) in float aFinish;     // aspect du tissu (0 uni, 1 ripstop, 2 sergé, 3 réfléchissant)

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out float vScalar;
out vec3 vColor;
out vec2 vUv;
out float vFinish;

void main() {
    vColor = aColor;
    vUv = aUv;
    vFinish = aFinish;
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
in vec2 vUv;
in float vFinish;
out vec4 fragColor;
uniform sampler2D uPrints;
uniform int   uHasPrints;
uniform vec2  uPrintOrigin;
uniform vec2  uPrintSize;
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
        // Sol d'atelier : plan continu, flaque de lumière, ombre de contact sous l'avatar.
        albedo = vec3(0.162, 0.150, 0.136);
        float pool = exp(-0.30 * dot(vWorldPos.xz, vWorldPos.xz));
        albedo *= mix(0.58, 1.38, pool);
        vec3 toC = uSphereCenter - vWorldPos;
        float t = max(dot(toC, L), 0.0);
        float dist = length(toC - L * t);
        shadow = mix(0.35, 1.0, smoothstep(uSphereRadius * 0.6, uSphereRadius * 1.25, dist));
        if (uSphereRadius < 0.01) {
            float contact = smoothstep(0.78, 0.05, length(vWorldPos.xz));
            shadow *= mix(1.0, 0.50, contact);
        }
    }
    float roughness = uRoughness;
    float sheenAmount = uSheen;
    vec3 F0 = vec3(0.04);
    if (uMode == 0 && uShowStrain == 0) {
        // Aspect du tissu, dessiné à partir des coordonnées du patron (en mètres).
        int finish = int(vFinish + 0.5);
        float aa = max(max(fwidth(vUv.x), fwidth(vUv.y)), 1e-6);   // taille d'un pixel sur le tissu
        if (finish == 1) {
            // Ripstop : quadrillage de renfort tous les 6 mm + armure fine.
            vec2 g = abs(fract(vUv / 0.006 + 0.5) - 0.5) * 0.006;
            float line = 1.0 - smoothstep(0.00035, 0.00035 + aa, min(g.x, g.y));
            float fade = 1.0 - smoothstep(0.0012, 0.0035, aa);
            float weave = sin(vUv.x * 7853.98) * sin(vUv.y * 7853.98);
            float fadeW = 1.0 - smoothstep(0.0002, 0.0006, aa);
            albedo *= 1.0 + (0.14 * line) * fade + 0.05 * weave * fadeW;
        } else if (finish == 2) {
            // Sergé : côtes en diagonale.
            float d = sin((vUv.x + vUv.y) * 2221.44);
            float fade = 1.0 - smoothstep(0.0006, 0.0018, aa);
            albedo *= 1.0 + 0.08 * d * fade;
        } else if (finish == 3) {
            // Réfléchissant : gris argenté, brillant.
            albedo = mix(albedo, vec3(0.80, 0.82, 0.85), 0.75);
            roughness = 0.28;
            F0 = vec3(0.30);
            sheenAmount = 0.0;
        } else if (finish == 4) {
            // Fermeture éclair : dents métalliques dessinées dans la texture du patron, un peu brillantes.
            roughness = 0.38;
            F0 = vec3(0.10);
            sheenAmount = 0.0;
        }
        if (uHasPrints == 1 && gl_FrontFacing) {
            vec4 p = texture(uPrints, (vUv - uPrintOrigin) / uPrintSize);
            albedo = mix(albedo, p.rgb, p.a);
        }
    }
    if (uMode == 0 && uShowStrain == 1) albedo = heatmap(vScalar / uStrainScale);
    // Envers du tissu (face tournée vers le corps) : plus sombre et bleuté, pour reconnaître l'endroit.
    if (uMode == 0 && uBackSide == 1 && !gl_FrontFacing) albedo = mix(albedo * 0.55, vec3(0.22, 0.30, 0.46), 0.45);

    vec3 base = pow(albedo, vec3(2.2));                 // sRGB -> linéaire
    float a = max(roughness * roughness, 0.03);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 1e-3);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3 F = fresnelSchlick(VoH, F0);
    vec3 specular = distributionGGX(NoH, a) * visibilitySmith(NoV, NoL, a) * F;
    vec3 diffuse = (1.0 - F) * base / PI;
    vec3 sheen = sheenAmount * base * pow(1.0 - NoV, 3.0);   // reflet rasant des fibres

    vec3 sun = vec3(3.55, 3.12, 2.52);                  // lampe d'atelier, un peu tungstène
    vec3 color = (diffuse + specular) * sun * NoL * shadow;
    // Ciel (hémisphère) + contre-jour froid
    vec3 sky = mix(vec3(0.09, 0.072, 0.058), vec3(0.27, 0.31, 0.40), 0.5 + 0.5 * N.y);
    color += base * sky + sheen * 1.15;
    color += base * 0.32 * max(dot(N, normalize(vec3(-0.35, 0.22, -0.90))), 0.0) * vec3(0.48, 0.60, 0.92);

    if (uMode == 2) {
        float fade = exp(-0.048 * dot(vWorldPos.xz, vWorldPos.xz));
        // Valeur linéaire qui, après tone mapping, rejoint l'horizon du cyclorama.
        color = mix(vec3(0.0104, 0.0078, 0.0055), color, fade);
    }

    color = color / (color + vec3(1.0));                // tone mapping de Reinhard
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

// Fond de la vue 3D : dégradé déjà en sRGB (pas de tone mapping).
const char* kBackdropVertex = R"GLSL(
#version 330 core
out vec2 vUv;
void main() {
    const vec2 pos[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 p = pos[gl_VertexID];
    vUv = p * 0.5 + 0.5;
    gl_Position = vec4(p, 1.0, 1.0);
}
)GLSL";

const char* kBackdropFragment = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
void main() {
    float y = clamp(vUv.y, 0.0, 1.0);
    vec3 top = vec3(0.034, 0.031, 0.028);
    vec3 horizon = vec3(0.125, 0.110, 0.094);
    vec3 below = vec3(0.058, 0.052, 0.046);
    vec3 col = y < 0.46 ? mix(below, horizon, smoothstep(0.0, 0.46, y))
                        : mix(horizon, top, smoothstep(0.46, 1.0, y));
    vec2 d = clamp(vUv, 0.0, 1.0) - vec2(0.50, 0.40);
    col += vec3(0.050, 0.034, 0.016) * exp(-5.2 * dot(d, d));
    fragColor = vec4(col, 1.0);
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
    if (printTexture_ != 0) gl::DeleteTextures(1, &printTexture_);
    if (backdropVao_ != 0 && gl::DeleteVertexArrays != nullptr) gl::DeleteVertexArrays(1, &backdropVao_);
}

void Renderer::setAvatarMesh(const std::vector<glm::vec3>& positions, const std::vector<glm::vec3>& normals,
                             const std::vector<uint32_t>& indices)
{
    createMesh(avatar_, positions, normals, indices, gl::STATIC_DRAW);
}

bool Renderer::init(std::string& error)
{
    if (!shader_.create(kVertexShader, kFragmentShader, error)) return false;
    if (!backdrop_.create(kBackdropVertex, kBackdropFragment, error)) return false;
    gl::GenVertexArrays(1, &backdropVao_);

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
    const std::vector<glm::vec2> uv0(positions.size(), glm::vec2(0.0f));
    attribute(mesh.uv, 4, 2, uv0.data(), uv0.size() * sizeof(glm::vec2));
    attribute(mesh.finish, 5, 1, zeros.data(), zeros.size() * sizeof(float));

    gl::GenBuffers(1, &mesh.index);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, mesh.index);
    gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, static_cast<gl::GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                   indices.data(), gl::STATIC_DRAW);
    gl::BindVertexArray(0);
}

void Renderer::destroyMesh(GpuMesh& mesh)
{
    if (mesh.vao == 0) return;
    const gl::GLuint buffers[] = {mesh.position, mesh.normal, mesh.scalar, mesh.color, mesh.uv, mesh.finish, mesh.index};
    gl::DeleteBuffers(7, buffers);
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

void Renderer::setClothPattern(const std::vector<glm::vec2>& uvs, const std::vector<float>& finish)
{
    if (cloth_.vao == 0) return;
    if (uvs.size() == cloth_.vertexCount) {
        gl::BindBuffer(gl::ARRAY_BUFFER, cloth_.uv);
        gl::BufferSubData(gl::ARRAY_BUFFER, 0, static_cast<gl::GLsizeiptr>(uvs.size() * sizeof(glm::vec2)), uvs.data());
    }
    if (finish.size() == cloth_.vertexCount) {
        gl::BindBuffer(gl::ARRAY_BUFFER, cloth_.finish);
        gl::BufferSubData(gl::ARRAY_BUFFER, 0, static_cast<gl::GLsizeiptr>(finish.size() * sizeof(float)), finish.data());
    }
}

void Renderer::setPrintTexture(int width, int height, const unsigned char* rgba, glm::vec2 origin, glm::vec2 size)
{
    if (width <= 0 || height <= 0 || rgba == nullptr) {
        if (printTexture_ != 0) gl::DeleteTextures(1, &printTexture_);
        printTexture_ = 0;
        return;
    }
    if (printTexture_ == 0) gl::GenTextures(1, &printTexture_);
    gl::BindTexture(gl::TEXTURE_2D, printTexture_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA), width, height, 0, gl::RGBA, gl::UNSIGNED_BYTE, rgba);
    gl::GenerateMipmap(gl::TEXTURE_2D);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<gl::GLint>(gl::LINEAR_MIPMAP_LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<gl::GLint>(gl::LINEAR));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<gl::GLint>(gl::CLAMP_TO_EDGE));
    gl::BindTexture(gl::TEXTURE_2D, 0);
    printOrigin_ = origin;
    printSize_ = glm::max(size, glm::vec2(1e-4f));
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
    gl::ClearColor(0.058f, 0.052f, 0.046f, 1.0f);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    gl::Disable(gl::SCISSOR_TEST);
    gl::Disable(gl::BLEND);
    gl::Disable(gl::CULL_FACE);
    if (backdropVao_ != 0) {
        gl::Disable(gl::DEPTH_TEST);
        backdrop_.use();
        gl::BindVertexArray(backdropVao_);
        gl::DrawArrays(gl::TRIANGLES, 0, 3);
        gl::Clear(gl::DEPTH_BUFFER_BIT);
    }
    gl::Enable(gl::DEPTH_TEST);

    const float aspect = static_cast<float>(scene.width) / static_cast<float>(std::max(scene.height, 1));
    const glm::mat4 viewProj = scene.camera->projection(aspect) * scene.camera->view();

    shader_.use();
    shader_.set("uViewProj", viewProj);
    shader_.set("uCameraPos", scene.camera->position());
    shader_.set("uLightDir", glm::normalize(glm::vec3(-0.30f, -1.0f, -0.46f)));
    shader_.set("uShowStrain", 0);
    shader_.set("uBackSide", 0);
    shader_.set("uHasPrints", 0);
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
        shader_.set("uBaseColor", glm::vec3(0.78f, 0.71f, 0.62f));
        shader_.set("uRoughness", 0.72f);
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
    shader_.set("uHasPrints", printTexture_ != 0 ? 1 : 0);
    if (printTexture_ != 0) {
        gl::ActiveTexture(gl::TEXTURE0);
        gl::BindTexture(gl::TEXTURE_2D, printTexture_);
        shader_.set("uPrints", 0);
        shader_.set("uPrintOrigin", printOrigin_);
        shader_.set("uPrintSize", printSize_);
    }
    if (cloth.wireframe) gl::PolygonMode(gl::FRONT_AND_BACK, gl::LINE);
    drawMesh(cloth_);
    if (cloth.wireframe) gl::PolygonMode(gl::FRONT_AND_BACK, gl::FILL);
    shader_.set("uVertexColor", 0);
    shader_.set("uHasPrints", 0);
    if (printTexture_ != 0) gl::BindTexture(gl::TEXTURE_2D, 0);
    gl::BindVertexArray(0);
}

} // namespace drapeo::rendering
