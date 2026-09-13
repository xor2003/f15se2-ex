#include "cockpit3d.h"
#include "shared/common.h"
#include "r2d.h"
#include "log.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <cmath>
#include <vector>
#include <cstring>

#define CGLTF_IMPLEMENTATION
#include "thirdparty/cgltf.h"

namespace {
struct Binding { const char *name; float x, y, width, height; };
/* Half-open source rectangles in the game's 320x200 instrument canvas. Model
 * placement is independent: UVs select the live feed on any shaped surface. */
const Binding bindings[] = {
    {"display_map", 24, 112, 73, 57},
    {"display_radar", 120, 104, 80, 72},
    {"display_target", 232, 128, 73, 57},
    {"indicator_R", 163, 191, 7, 7},
    {"indicator_I", 180, 191, 7, 7},
    {"indicator_B", 214, 191, 7, 7},
    {"indicator_L", 197, 191, 7, 7},
    {"weapons", 20, 188, 112, 12},
    {"throttle", 2, 114, 15, 47},
    {"fuel", 212, 128, 11, 50},
};
struct Vertex { float xyz[3], uv[2]; };
struct Part {
    std::vector<Vertex> vertices;
    GLuint texture = 0;
    int binding = -1;
    float color[4] = {1, 1, 1, 1};
};
std::vector<Part> parts;
std::vector<GLuint> textures;
GLuint sceneTexture = 0, instrumentsTexture = 0;
int textureWidth = 0, textureHeight = 0;
bool tried = false, captured = false;

GLuint uploadTexture(SDL_Surface *source) {
    SDL_Surface *rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
    if (!rgba) return 0;
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, rgba->pitch / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    SDL_DestroySurface(rgba);
    return texture;
}

bool loadNode(const cgltf_node *node, unsigned depth) {
    if (depth > 64 || node->skin) return false;
    float world[16];
    cgltf_node_transform_world(node, world);
    if (node->mesh) for (cgltf_size p = 0; p < node->mesh->primitives_count; p++) {
        const cgltf_primitive &primitive = node->mesh->primitives[p];
        if (primitive.type != cgltf_primitive_type_triangles || primitive.targets_count) return false;
        const cgltf_accessor *positions = nullptr, *uvs = nullptr;
        for (cgltf_size a = 0; a < primitive.attributes_count; a++) {
            const cgltf_attribute &attribute = primitive.attributes[a];
            if (attribute.type == cgltf_attribute_type_position) positions = attribute.data;
            if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uvs = attribute.data;
        }
        if (!positions || positions->type != cgltf_type_vec3 || positions->count > 1000000) return false;
        if (uvs && (uvs->type != cgltf_type_vec2 || uvs->count != positions->count)) return false;
        std::vector<float> xyz(positions->count * 3), uv(positions->count * 2, 0);
        if (cgltf_accessor_unpack_floats(positions, xyz.data(), xyz.size()) != xyz.size()) return false;
        if (uvs && cgltf_accessor_unpack_floats(uvs, uv.data(), uv.size()) != uv.size()) return false;
        Part part;
        const cgltf_material *material = primitive.material;
        if (material) {
            for (unsigned b = 0; b < sizeof(bindings) / sizeof(bindings[0]); b++)
                if (material->name && !strcmp(material->name, bindings[b].name)) part.binding = (int)b;
            if (material->has_pbr_metallic_roughness) {
                const auto &pbr = material->pbr_metallic_roughness;
                memcpy(part.color, pbr.base_color_factor, sizeof(part.color));
                if (part.binding < 0 && pbr.base_color_texture.texture) {
                    const cgltf_image *image = pbr.base_color_texture.texture->image;
                    if (!image || !image->buffer_view || pbr.base_color_texture.texcoord != 0 ||
                        pbr.base_color_texture.has_transform) return false;
                    SDL_IOStream *io = SDL_IOFromConstMem(cgltf_buffer_view_data(image->buffer_view), image->buffer_view->size);
                    SDL_Surface *surface = io ? SDL_LoadPNG_IO(io, true) : nullptr;
                    if (!surface) return false;
                    part.texture = uploadTexture(surface);
                    SDL_DestroySurface(surface);
                    if (!part.texture) return false;
                    textures.push_back(part.texture);
                }
            }
        }
        if ((part.binding >= 0 || part.texture) && !uvs) return false;
        const cgltf_size count = primitive.indices ? primitive.indices->count : positions->count;
        if (count % 3 || count > 3000000 || (primitive.indices && primitive.indices->is_sparse)) return false;
        part.vertices.reserve(count);
        for (cgltf_size v = 0; v < count; v++) {
            const cgltf_size index = primitive.indices ? cgltf_accessor_read_index(primitive.indices, v) : v;
            if (index >= positions->count) return false;
            Vertex vertex;
            for (int axis = 0; axis < 3; axis++) {
                vertex.xyz[axis] = world[axis] * xyz[index * 3] + world[4 + axis] * xyz[index * 3 + 1] +
                    world[8 + axis] * xyz[index * 3 + 2] + world[12 + axis];
                if (!std::isfinite(vertex.xyz[axis])) return false;
            }
            vertex.uv[0] = uv[index * 2];
            vertex.uv[1] = uv[index * 2 + 1];
            if (!std::isfinite(vertex.uv[0]) || !std::isfinite(vertex.uv[1])) return false;
            part.vertices.push_back(vertex);
        }
        parts.push_back(std::move(part));
    }
    for (cgltf_size child = 0; child < node->children_count; child++)
        if (!loadNode(node->children[child], depth + 1)) return false;
    return true;
}

bool loadCockpit() {
    if (tried) return !parts.empty();
    tried = true;
    const char *enabled = SDL_getenv("F15_3D_COCKPIT");
    if (enabled && !strcmp(enabled, "0")) return false;
    char path[512];
    if (!findReplacementAssetPath("cockpit", ".glb", path, sizeof(path))) return false;
    cgltf_options options = {};
    cgltf_data *data = nullptr;
    bool valid = cgltf_parse_file(&options, path, &data) == cgltf_result_success;
    if (valid) {
        valid = data->file_type == cgltf_file_type_glb && data->extensions_required_count == 0;
        for (cgltf_size i = 0; i < data->buffers_count; i++) valid = valid && !data->buffers[i].uri;
        valid = valid && cgltf_load_buffers(&options, data, path) == cgltf_result_success &&
            cgltf_validate(data) == cgltf_result_success && data->scene && !data->animations_count;
    }
    if (valid) for (cgltf_size i = 0; i < data->scene->nodes_count && valid; i++)
        valid = loadNode(data->scene->nodes[i], 0);
    cgltf_free(data);
    if (!valid || parts.empty()) {
        parts.clear();
        for (GLuint texture : textures) glDeleteTextures(1, &texture);
        textures.clear();
        LogWarn(("cockpit3d: cannot render %s; using flat cockpit (static triangles, UV0 and embedded PNG required)", path));
        return false;
    }
    LogInfo(("cockpit3d: loaded %s (%zu parts); 3D cockpit enabled", path, parts.size()));
    return true;
}

void copyFramebuffer(GLuint &texture, int width, int height) {
    if (!texture) glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, width, height, 0);
}
}

void cockpit3d_captureScene(int width, int height) {
    captured = false;
    if (width <= 0 || height <= 0) return;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    if (loadCockpit()) {
        copyFramebuffer(sceneTexture, width, height);
        textureWidth = width;
        textureHeight = height;
        captured = true;
    }
    glPopClientAttrib();
    glPopAttrib();
}

void cockpit3d_present(int width, int height, int shakePixels, int viewYawDegrees) {
    if (!captured) return;
    captured = false;
    if (width != textureWidth || height != textureHeight) return;
    R2DMapping mapping;
    r2d_computeMapping(320, 200, width, height, 0, &mapping);
    const float left = mapping.offX - shakePixels * mapping.scaleX;
    const float top = mapping.offY;
    const float right = left + 320 * mapping.scaleX;
    const float bottom = top + 200 * mapping.scaleY;
    const float panelTop = top + (viewYawDegrees == 0 ? 96 : 0) * mapping.scaleY;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_LIGHTING);
    glDisable(GL_FOG); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
    // Side/rear views must not overwrite the forward instrument feeds.
    if (viewYawDegrees == 0) copyFramebuffer(instrumentsTexture, width, height);
    glMatrixMode(GL_PROJECTION); glOrtho(0, width, height, 0, -1, 1);
    /* Remove the flat lower panel after capturing its live instruments. Keep
     * the already-drawn HUD above it, and restore the scene underneath. */
    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glBegin(GL_QUADS);
    glTexCoord2f(left / width, 1 - panelTop / height); glVertex2f(left, panelTop);
    glTexCoord2f(right / width, 1 - panelTop / height); glVertex2f(right, panelTop);
    glTexCoord2f(right / width, 1 - bottom / height); glVertex2f(right, bottom);
    glTexCoord2f(left / width, 1 - bottom / height); glVertex2f(left, bottom);
    glEnd();
    glViewport((int)left, (int)(height - bottom), (int)(right - left), (int)(bottom - top));
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)left, (int)(height - bottom), (int)(right - left), (int)(bottom - top));
    glDepthMask(GL_TRUE); glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL);
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.5f);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    /* Camera at origin, looking -Z, +Y up. 60-degree vertical field of view;
     * 320:200 projection then receives the game's normal pixel-aspect correction. */
    const double nearPlane = 0.05, halfHeight = nearPlane * std::tan(3.141592653589793 / 6);
    glFrustum(-halfHeight * 1.6, halfHeight * 1.6, -halfHeight, halfHeight, nearPlane, 100);
    glMatrixMode(GL_MODELVIEW);
    glRotatef((float)viewYawDegrees, 0, 1, 0);
    for (const Part &part : parts) {
        const Binding *binding = part.binding >= 0 ? &bindings[part.binding] : nullptr;
        if (binding && !instrumentsTexture) continue;
        const GLuint texture = binding ? instrumentsTexture : part.texture;
        if (texture) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texture); }
        else glDisable(GL_TEXTURE_2D);
        if (binding) glColor4f(1, 1, 1, 1);
        else glColor4fv(part.color);
        glBegin(GL_TRIANGLES);
        for (const Vertex &vertex : part.vertices) {
            if (binding) {
                const float x = left + (binding->x + vertex.uv[0] * binding->width) * mapping.scaleX;
                const float y = top + (binding->y + vertex.uv[1] * binding->height) * mapping.scaleY;
                glTexCoord2f(x / width, 1 - y / height);
            } else glTexCoord2fv(vertex.uv);
            glVertex3fv(vertex.xyz);
        }
        glEnd();
    }
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopAttrib();
}

void cockpit3d_shutdown(void) {
    for (GLuint texture : textures) glDeleteTextures(1, &texture);
    glDeleteTextures(1, &sceneTexture);
    glDeleteTextures(1, &instrumentsTexture);
    textures.clear(); parts.clear();
    sceneTexture = instrumentsTexture = 0;
    tried = captured = false;
}
