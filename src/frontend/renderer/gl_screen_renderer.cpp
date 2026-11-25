#include "frontend/renderer/gl_screen_renderer.hpp"

namespace elmasds::frontend {

namespace {

using PfnGlCreateShader = GLuint(APIENTRY*)(GLenum);
using PfnGlShaderSource = void(APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using PfnGlCompileShader = void(APIENTRY*)(GLuint);
using PfnGlGetShaderiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using PfnGlDeleteShader = void(APIENTRY*)(GLuint);
using PfnGlCreateProgram = GLuint(APIENTRY*)();
using PfnGlAttachShader = void(APIENTRY*)(GLuint, GLuint);
using PfnGlLinkProgram = void(APIENTRY*)(GLuint);
using PfnGlGetProgramiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using PfnGlUseProgram = void(APIENTRY*)(GLuint);
using PfnGlDeleteProgram = void(APIENTRY*)(GLuint);
using PfnGlGenVertexArrays = void(APIENTRY*)(GLsizei, GLuint*);
using PfnGlBindVertexArray = void(APIENTRY*)(GLuint);
using PfnGlDeleteVertexArrays = void(APIENTRY*)(GLsizei, const GLuint*);

struct GlProcs {
    PfnGlCreateShader create_shader = nullptr;
    PfnGlShaderSource shader_source = nullptr;
    PfnGlCompileShader compile_shader = nullptr;
    PfnGlGetShaderiv get_shader_iv = nullptr;
    PfnGlDeleteShader delete_shader = nullptr;
    PfnGlCreateProgram create_program = nullptr;
    PfnGlAttachShader attach_shader = nullptr;
    PfnGlLinkProgram link_program = nullptr;
    PfnGlGetProgramiv get_program_iv = nullptr;
    PfnGlUseProgram use_program = nullptr;
    PfnGlDeleteProgram delete_program = nullptr;
    PfnGlGenVertexArrays gen_vertex_arrays = nullptr;
    PfnGlBindVertexArray bind_vertex_array = nullptr;
    PfnGlDeleteVertexArrays delete_vertex_arrays = nullptr;

    [[nodiscard]] bool load() noexcept {
        create_shader = reinterpret_cast<PfnGlCreateShader>(SDL_GL_GetProcAddress("glCreateShader"));
        shader_source = reinterpret_cast<PfnGlShaderSource>(SDL_GL_GetProcAddress("glShaderSource"));
        compile_shader =
            reinterpret_cast<PfnGlCompileShader>(SDL_GL_GetProcAddress("glCompileShader"));
        get_shader_iv = reinterpret_cast<PfnGlGetShaderiv>(SDL_GL_GetProcAddress("glGetShaderiv"));
        delete_shader =
            reinterpret_cast<PfnGlDeleteShader>(SDL_GL_GetProcAddress("glDeleteShader"));
        create_program =
            reinterpret_cast<PfnGlCreateProgram>(SDL_GL_GetProcAddress("glCreateProgram"));
        attach_shader =
            reinterpret_cast<PfnGlAttachShader>(SDL_GL_GetProcAddress("glAttachShader"));
        link_program = reinterpret_cast<PfnGlLinkProgram>(SDL_GL_GetProcAddress("glLinkProgram"));
        get_program_iv =
            reinterpret_cast<PfnGlGetProgramiv>(SDL_GL_GetProcAddress("glGetProgramiv"));
        use_program = reinterpret_cast<PfnGlUseProgram>(SDL_GL_GetProcAddress("glUseProgram"));
        delete_program =
            reinterpret_cast<PfnGlDeleteProgram>(SDL_GL_GetProcAddress("glDeleteProgram"));
        gen_vertex_arrays =
            reinterpret_cast<PfnGlGenVertexArrays>(SDL_GL_GetProcAddress("glGenVertexArrays"));
        bind_vertex_array =
            reinterpret_cast<PfnGlBindVertexArray>(SDL_GL_GetProcAddress("glBindVertexArray"));
        delete_vertex_arrays = reinterpret_cast<PfnGlDeleteVertexArrays>(
            SDL_GL_GetProcAddress("glDeleteVertexArrays"));
        return create_shader != nullptr && shader_source != nullptr &&
               compile_shader != nullptr && get_shader_iv != nullptr &&
               delete_shader != nullptr && create_program != nullptr &&
               attach_shader != nullptr && link_program != nullptr &&
               get_program_iv != nullptr && use_program != nullptr &&
               delete_program != nullptr && gen_vertex_arrays != nullptr &&
               bind_vertex_array != nullptr && delete_vertex_arrays != nullptr;
    }
};

GlProcs g_gl{};

constexpr const char* kVertexShaderSrc = R"(#version 330 core
out vec2 v_uv;
void main() {
    vec2 pos[4] = vec2[4](
        vec2(-1.0,  1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0, -1.0),
        vec2( 1.0, -1.0)
    );
    vec2 uv[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0)
    );
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
    v_uv = uv[gl_VertexID];
}
)";

constexpr const char* kFragmentShaderSrc = R"(#version 330 core
in vec2 v_uv;
out vec4 frag_color;
uniform sampler2D u_tex;
void main() {
    vec4 c = texture(u_tex, v_uv);
    frag_color = vec4(c.b, c.g, c.r, 1.0);
}
)";

[[nodiscard]] GLuint compile_gl_shader(GLenum type, const char* src) noexcept {
    const GLuint s = g_gl.create_shader(type);
    g_gl.shader_source(s, 1, &src, nullptr);
    g_gl.compile_shader(s);
    GLint ok = 0;
    g_gl.get_shader_iv(s, GL_COMPILE_STATUS, &ok);
    if (ok == 0) {
        g_gl.delete_shader(s);
        return 0U;
    }
    return s;
}

[[nodiscard]] GLuint create_screen_texture() noexcept {
    GLuint tex = 0U;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(kScreenWidth),
                 static_cast<GLsizei>(kScreenHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    return tex;
}

}  // namespace

GlScreenRenderer::~GlScreenRenderer() noexcept {
    destroy();
}

bool GlScreenRenderer::initialize() noexcept {
    if (!g_gl.load()) {
        return false;
    }

    const GLuint vs = compile_gl_shader(GL_VERTEX_SHADER, kVertexShaderSrc);
    const GLuint fs = compile_gl_shader(GL_FRAGMENT_SHADER, kFragmentShaderSrc);
    if (vs == 0U || fs == 0U) {
        return false;
    }

    program_ = g_gl.create_program();
    g_gl.attach_shader(program_, vs);
    g_gl.attach_shader(program_, fs);
    g_gl.link_program(program_);
    g_gl.delete_shader(vs);
    g_gl.delete_shader(fs);

    GLint linked = 0;
    g_gl.get_program_iv(program_, GL_LINK_STATUS, &linked);
    if (linked == 0) {
        g_gl.delete_program(program_);
        program_ = 0U;
        return false;
    }

    g_gl.gen_vertex_arrays(1, &vao_);
    top_tex_ = create_screen_texture();
    bottom_tex_ = create_screen_texture();
    initialized_ = true;
    return true;
}

void GlScreenRenderer::destroy() noexcept {
    if (!initialized_) {
        return;
    }
    if (top_tex_ != 0U) {
        glDeleteTextures(1, &top_tex_);
        top_tex_ = 0U;
    }
    if (bottom_tex_ != 0U) {
        glDeleteTextures(1, &bottom_tex_);
        bottom_tex_ = 0U;
    }
    if (vao_ != 0U) {
        g_gl.delete_vertex_arrays(1, &vao_);
        vao_ = 0U;
    }
    if (program_ != 0U) {
        g_gl.delete_program(program_);
        program_ = 0U;
    }
    initialized_ = false;
}

void GlScreenRenderer::draw_screen_quad(const ScreenRect& rect,
                                        i32 window_height,
                                        GLuint texture,
                                        std::span<const u32> pixels) noexcept {
    if (rect.width <= 0 || rect.height <= 0 || pixels.size() < (kScreenWidth * kScreenHeight)) {
        return;
    }
    const i32 gl_y = window_height - (rect.y + rect.height);
    glViewport(rect.x, gl_y, rect.width, rect.height);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(kScreenWidth),
                    static_cast<GLsizei>(kScreenHeight), GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void GlScreenRenderer::render(const DualScreenLayout& layout,
                              i32 window_width,
                              i32 window_height,
                              std::span<const u32> top_pixels,
                              std::span<const u32> bottom_pixels) noexcept {
    glViewport(0, 0, window_width, window_height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.06F, 0.06F, 0.08F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!initialized_) {
        return;
    }

    g_gl.use_program(program_);
    g_gl.bind_vertex_array(vao_);
    draw_screen_quad(layout.top_screen, window_height, top_tex_, top_pixels);
    draw_screen_quad(layout.bottom_screen, window_height, bottom_tex_, bottom_pixels);
}

}  // namespace elmasds::frontend
