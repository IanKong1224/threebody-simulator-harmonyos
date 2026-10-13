#include "renderer/gles/GLESRenderBackend.h"
#include "renderer/common/BasicMesh.h"
#include "renderer/common/SceneMesh.h"
#include "renderer/gles/SceneShaders.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
namespace threebody {
namespace {
void eglCheck(bool ok, const char *what) {
  if (!ok)
    throw std::runtime_error(std::string(what) + ": EGL " +
                             std::to_string(eglGetError()));
}
void glCheck(const char *what) {
  auto e = glGetError();
  if (e != GL_NO_ERROR)
    throw std::runtime_error(std::string(what) + ": GL " + std::to_string(e));
}
} // namespace
struct GLESRenderBackend::Impl {
  void *window;
  EGLDisplay display{EGL_NO_DISPLAY};
  EGLContext context{EGL_NO_CONTEXT};
  EGLSurface surface{EGL_NO_SURFACE};
  GLuint fbo{}, texture{}, depth{}, program{};
  struct Slot {
    GLuint vao{}, vbo{};
    GLsync fence{};
    uint64_t serial{};
    GLuint queries[5]{};
  };
  std::vector<Slot> slots;
  GLuint hdrFbo{}, hdrTexture{}, bloomFbo[2]{}, bloomTexture[2]{},
      postProgram{};
  GLint matrixLocation{-1}, timeLocation{-1}, brightnessLocation{-1},
      postLocation{-1}, haloLocation{-1};
  uint32_t bloomWidth{}, bloomHeight{}, workloadLevel{};
  using CounterFn = void(GL_APIENTRY *)(GLuint, GLenum);
  using ResultFn = void(GL_APIENTRY *)(GLuint, GLenum, GLuint64 *);
  CounterFn counter{};
  ResultFn queryResult{};
  bool gpuTiming{false}, disjoint{false};
  uint32_t width{}, height{};
  uint64_t serial{}, completed{};
  SceneMesh geometry;
  BasicMesh basicGeometry;
  bool basic{false};
  bool particles{false};
  explicit Impl(void *w) : window(w) {}
};
GLESRenderBackend::GLESRenderBackend(void *w) : p_(new Impl(w)) {}
GLESRenderBackend::~GLESRenderBackend() { shutdown(); }
bool GLESRenderBackend::initialize(const RenderConfig &config) {
  auto &p = *p_;
  if (config.framesInFlight < 1 || config.framesInFlight > 3 ||
      (p.window && config.framesInFlight != 1))
    throw std::runtime_error(
        "Supported in-flight capacity: headless 1..3, preview 1");
  p.slots.resize(config.framesInFlight);
  p.particles = config.visualStyle == VisualStyle::Particles;
  p.basic = config.visualStyle != VisualStyle::Detailed;
  p.workloadLevel = std::min(config.workloadLevel, uint32_t(2));
  try {
    p.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglCheck(p.display != EGL_NO_DISPLAY, "get display");
    eglCheck(eglInitialize(p.display, nullptr, nullptr), "initialize EGL");
    eglCheck(eglBindAPI(EGL_OPENGL_ES_API), "bind GLES");
    const EGLint attributes[] = {EGL_SURFACE_TYPE,
                                 p.window ? EGL_WINDOW_BIT : EGL_PBUFFER_BIT,
                                 EGL_RENDERABLE_TYPE,
                                 EGL_OPENGL_ES3_BIT,
                                 EGL_RED_SIZE,
                                 8,
                                 EGL_GREEN_SIZE,
                                 8,
                                 EGL_BLUE_SIZE,
                                 8,
                                 EGL_ALPHA_SIZE,
                                 8,
                                 EGL_NONE};
    EGLConfig ec;
    EGLint n = 0;
    eglCheck(eglChooseConfig(p.display, attributes, &ec, 1, &n) && n > 0,
             "choose GLES3 config");
    const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    p.context = eglCreateContext(p.display, ec, EGL_NO_CONTEXT, contextAttrs);
    eglCheck(p.context != EGL_NO_CONTEXT, "create context");
    if (p.window)
      p.surface = eglCreateWindowSurface(
          p.display, ec, reinterpret_cast<EGLNativeWindowType>(p.window),
          nullptr);
    else {
      const EGLint pb[] = {EGL_WIDTH, EGLint(config.width), EGL_HEIGHT,
                           EGLint(config.height), EGL_NONE};
      p.surface = eglCreatePbufferSurface(p.display, ec, pb);
    }
    eglCheck(p.surface != EGL_NO_SURFACE, "create surface");
    eglCheck(eglMakeCurrent(p.display, p.surface, p.surface, p.context),
             "make current");
    EGLint w = 0, h = 0;
    eglCheck(eglQuerySurface(p.display, p.surface, EGL_WIDTH, &w),
             "surface width");
    eglCheck(eglQuerySurface(p.display, p.surface, EGL_HEIGHT, &h),
             "surface height");
    p.width = w;
    p.height = h;
    eglSwapInterval(p.display, 1);
    glGenTextures(1, &p.texture);
    glBindTexture(GL_TEXTURE_2D, p.texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &p.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           p.texture, 0);
    if (!p.basic) {
      // Require real floating-point color attachments; never silently use LDR.
      bool floatColor = false;
      GLint extensionCount = 0;
      glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
      for (GLint i = 0; i < extensionCount; ++i) {
        const char *ext =
            reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
        if (ext && (std::strcmp(ext, "GL_EXT_color_buffer_float") == 0 ||
                    std::strcmp(ext, "GL_EXT_color_buffer_half_float") == 0))
          floatColor = true;
      }
      if (!floatColor)
        throw std::runtime_error("M6 requires RGBA16F color attachments (GLES "
                                 "float-color extension unavailable)");
      auto target = [](GLuint &texture, GLuint &fbo, uint32_t width,
                       uint32_t height) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, width, height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
          throw std::runtime_error("M6 RGBA16F framebuffer unsupported");
      };
      target(p.hdrTexture, p.hdrFbo, w, h);
      p.bloomWidth = std::max(1u, p.width / 4u);
      p.bloomHeight = std::max(1u, p.height / 4u);
      for (int i = 0; i < 2; ++i)
        target(p.bloomTexture[i], p.bloomFbo[i], p.bloomWidth, p.bloomHeight);
      glBindFramebuffer(GL_FRAMEBUFFER, p.basic ? p.fbo : p.hdrFbo);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, p.basic ? p.fbo : p.hdrFbo);
    glGenRenderbuffers(1, &p.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, p.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, p.depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      throw std::runtime_error("Incomplete offscreen framebuffer");
    auto compile = [](GLenum type, const char *source) {
      GLuint shader = glCreateShader(type);
      glShaderSource(shader, 1, &source, nullptr);
      glCompileShader(shader);
      GLint ok = 0;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
      if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Shader: ") + log);
      }
      return shader;
    };
    auto link = [&](const char *vertex, const char *fragment) {
      GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = 0, program = 0;
      try {
        fs = compile(GL_FRAGMENT_SHADER, fragment);
      } catch (...) {
        glDeleteShader(vs);
        throw;
      }
      program = glCreateProgram();
      glAttachShader(program, vs);
      glAttachShader(program, fs);
      glLinkProgram(program);
      glDeleteShader(vs);
      glDeleteShader(fs);
      GLint linked = 0;
      glGetProgramiv(program, GL_LINK_STATUS, &linked);
      if (!linked) {
        char log[1024]{};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        glDeleteProgram(program);
        throw std::runtime_error(std::string("Program: ") + log);
      }
      return program;
    };
    p.program =
        p.particles ? link(particles_vert, particles_frag) : (p.basic ? link(basic_vert, basic_frag) : link(scene_vert, scene_frag));
    if (!p.basic) {
      p.postProgram = link(post_vert, post_frag);
      p.brightnessLocation =
          glGetUniformLocation(p.program, "stellarBrightness");
      p.timeLocation = glGetUniformLocation(p.program, "materialTime");
      p.postLocation = glGetUniformLocation(p.postProgram, "postParams");
      p.haloLocation = glGetUniformLocation(p.postProgram, "haloStars[0]");
      glUseProgram(p.postProgram);
      glUniform1i(glGetUniformLocation(p.postProgram, "sceneTexture"), 0);
      glUniform1i(glGetUniformLocation(p.postProgram, "bloomTexture"), 1);
    }
    p.matrixLocation = glGetUniformLocation(p.program, "mvp");
    if ((!p.basic || p.particles) && p.matrixLocation < 0)
      throw std::runtime_error("Missing camera uniform");
    GLint extCount = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extCount);
    bool timerExt = false;
    for (GLint i = 0; i < extCount; ++i) {
      const char *ext =
          reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
      if (ext && std::strcmp(ext, "GL_EXT_disjoint_timer_query") == 0)
        timerExt = true;
    }
    if (timerExt && config.enableGpuTiming && !p.basic) {
      p.counter = reinterpret_cast<Impl::CounterFn>(
          eglGetProcAddress("glQueryCounterEXT"));
      p.queryResult = reinterpret_cast<Impl::ResultFn>(
          eglGetProcAddress("glGetQueryObjectui64vEXT"));
      GLint bits = 0;
      glGetQueryiv(0x8E28 /*TIMESTAMP_EXT*/, 0x8864 /*COUNTER_BITS_EXT*/,
                   &bits);
      p.gpuTiming = p.counter && p.queryResult && bits > 0;
    }
    for (auto &slot : p.slots) {
      if (p.gpuTiming)
        glGenQueries(5, slot.queries);
      glGenVertexArrays(1, &slot.vao);
      glBindVertexArray(slot.vao);
      glGenBuffers(1, &slot.vbo);
      glBindBuffer(GL_ARRAY_BUFFER, slot.vbo);
      glBufferData(GL_ARRAY_BUFFER,
                   ((p.basic && !p.particles) ? BasicMesh::maxVertices : SceneMesh::maxVertices) *
                       sizeof(SceneVertex),
                   nullptr, GL_DYNAMIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(
          0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
          reinterpret_cast<void *>(offsetof(SceneVertex, position)));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(
          1, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
          reinterpret_cast<void *>(offsetof(SceneVertex, normal)));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(
          2, 4, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
          reinterpret_cast<void *>(offsetof(SceneVertex, color)));
    }
    glCheck("create offscreen and mesh pipeline");
    return true;
  } catch (...) {
    shutdown();
    throw;
  }
}
FrameHandle GLESRenderBackend::submitFrame(const RenderFrameInput &input) {
  auto &p = *p_;
  auto &slot = p.slots[p.serial % p.slots.size()];
  if (slot.fence) {
    if (!isFrameComplete({slot.serial}))
      throw std::runtime_error("Offscreen frame still in flight");
    glDeleteSync(slot.fence);
    slot.fence = nullptr;
  }
  const auto &vertices = (p.basic && !p.particles)
                             ? p.basicGeometry.build(input, p.width, p.height)
                             : p.geometry.build(input);
  if (vertices.size() > SceneMesh::maxVertices)
    throw std::runtime_error("Scene vertex capacity exceeded");
  if (p.gpuTiming)
    p.counter(slot.queries[0], 0x8E28);
  glBindFramebuffer(GL_FRAMEBUFFER, p.basic ? p.fbo : p.hdrFbo);
  glViewport(0, 0, p.width, p.height);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.f, 0.f, 0.f, 1.f);
  glClearDepthf(1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(p.program);
  auto matrix = cameraMatrix(input.camera, float(p.width) / p.height);
  glUniformMatrix4fv(p.matrixLocation, 1, GL_FALSE, matrix.data());
  glUniform1f(p.brightnessLocation, input.stellarBrightness);
  glUniform1f(p.timeLocation, float(input.state.simulationTime));
  if(!p.basic && !p.particles){
    float lights[12]{};
    for(int i=0;i<3;++i){auto b=input.state.body[i];lights[i*4]=float(b.position.x);lights[i*4+1]=float(b.position.y);lights[i*4+2]=float(b.position.z);lights[i*4+3]=float(std::pow(b.mass,3.5));}
    glUniform4fv(glGetUniformLocation(p.program,"starLights[0]"),3,lights);
  }
  glBindVertexArray(slot.vao);
  glBindBuffer(GL_ARRAY_BUFFER, slot.vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertices.size() * sizeof(SceneVertex),
                  vertices.data());
  glDrawArrays(GL_TRIANGLES, 0, vertices.size());
  if (p.gpuTiming)
    p.counter(slot.queries[1], 0x8E28);
  glDisable(GL_DEPTH_TEST);
  if (!p.basic) {
    glUseProgram(p.postProgram);
    auto halos = stellarHalos(input, matrix, false);
    glUniform4fv(p.haloLocation, 3, halos.data());
    for (int pass = 0; pass < 5; ++pass) {
      const bool final = pass == 4;
      glBindFramebuffer(GL_FRAMEBUFFER, final ? p.fbo : p.bloomFbo[pass % 2]);
      glViewport(0, 0, final ? p.width : p.bloomWidth,
                 final ? p.height : p.bloomHeight);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, pass > 0 && pass < 4
                                       ? p.bloomTexture[(pass - 1) % 2]
                                       : p.hdrTexture);
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, final ? p.bloomTexture[1]
                                         : (pass > 0 && pass < 4
                                                ? p.bloomTexture[(pass - 1) % 2]
                                                : p.hdrTexture));
      glUniform4f(p.postLocation, float(pass), 1.0f, float(p.workloadLevel),
                  input.stellarBrightness);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      if (p.gpuTiming && (pass < 2 || final))
        p.counter(slot.queries[final ? 4 : pass + 2], 0x8E28);
    }
  }
  slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glCheck("submit offscreen");
  if (!slot.fence)
    throw std::runtime_error("No GLES completion fence");
  glFlush();
  slot.serial = ++p.serial;
  return {slot.serial};
}
bool GLESRenderBackend::isFrameComplete(FrameHandle h) {
  auto &p = *p_;
  if (!h.value || h.value > p.serial)
    return false;
  if (h.value <= p.completed)
    return true;
  auto &slot = p.slots[(h.value - 1) % p.slots.size()];
  if (slot.serial != h.value)
    return false;
  auto r = glClientWaitSync(slot.fence, 0, 0);
  if (r == GL_TIMEOUT_EXPIRED)
    return false;
  if (r == GL_WAIT_FAILED)
    throw std::runtime_error("GLES fence wait failed");
  p.completed = std::max(p.completed, h.value);
  return true;
}
void GLESRenderBackend::presentFrame(FrameHandle h) {
  auto &p = *p_;
  if (!p.window)
    return;
  if (h.value != p.serial || !isFrameComplete(h))
    throw std::runtime_error("Cannot present incomplete frame");
  glBindFramebuffer(GL_READ_FRAMEBUFFER, p.fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(0, 0, p.width, p.height, 0, 0, p.width, p.height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glCheck("preview blit");
  eglCheck(eglSwapBuffers(p.display, p.surface), "swap preview");
}
void GLESRenderBackend::shutdown() {
  auto &p = *p_;
  if (p.display != EGL_NO_DISPLAY) {
    if (p.context != EGL_NO_CONTEXT && p.surface != EGL_NO_SURFACE) {
      eglMakeCurrent(p.display, p.surface, p.surface, p.context);
      glFinish();
      if (p.postProgram)
        glDeleteProgram(p.postProgram);
      glDeleteFramebuffers(2, p.bloomFbo);
      glDeleteTextures(2, p.bloomTexture);
      if (p.hdrFbo)
        glDeleteFramebuffers(1, &p.hdrFbo);
      if (p.hdrTexture)
        glDeleteTextures(1, &p.hdrTexture);
      for (auto &slot : p.slots) {
        if (slot.queries[0])
          glDeleteQueries(5, slot.queries);
        if (slot.vbo)
          glDeleteBuffers(1, &slot.vbo);
        if (slot.vao)
          glDeleteVertexArrays(1, &slot.vao);
        if (slot.fence)
          glDeleteSync(slot.fence);
      }
      if (p.program)
        glDeleteProgram(p.program);
      if (p.depth)
        glDeleteRenderbuffers(1, &p.depth);
      if (p.fbo)
        glDeleteFramebuffers(1, &p.fbo);
      if (p.texture)
        glDeleteTextures(1, &p.texture);
    }
    eglMakeCurrent(p.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (p.surface != EGL_NO_SURFACE)
      eglDestroySurface(p.display, p.surface);
    if (p.context != EGL_NO_CONTEXT)
      eglDestroyContext(p.display, p.context);
    eglTerminate(p.display);
  }
  void *w = p.window;
  p_.reset(new Impl(w));
}
BackendCapabilities GLESRenderBackend::queryCapabilities() const {
  return {GraphicsBackendType::OpenGLES, true, p_->gpuTiming,
          p_->window != nullptr};
}
GpuTiming GLESRenderBackend::getGpuTiming(FrameHandle h) {
  auto &p = *p_;
  if (!p.gpuTiming || !h.value || !isFrameComplete(h))
    return {};
  GLint disjoint = 0;
  glGetIntegerv(0x8FBB /*GPU_DISJOINT_EXT*/, &disjoint);
  p.disjoint = p.disjoint || disjoint != 0;
  if (p.disjoint)
    return {}; // Never reinterpret disjoint timestamps as valid.
  auto &slot = p.slots[(h.value - 1) % p.slots.size()];
  if (slot.serial != h.value)
    return {};
  GLuint64 ticks[5]{};
  for (int i = 0; i < 5; ++i) {
    GLuint available = 0;
    glGetQueryObjectuiv(slot.queries[i], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available)
      return {};
    p.queryResult(slot.queries[i], GL_QUERY_RESULT, &ticks[i]);
  }
  for (int i = 1; i < 5; ++i)
    if (ticks[i] < ticks[i - 1])
      return {};
  GpuTiming result{true, double(ticks[4] - ticks[0]) / 1e6};
  result.stagesValid = true;
  for (int i = 0; i < 4; ++i)
    result.stageMs[i] = double(ticks[i + 1] - ticks[i]) / 1e6;
  return result;
}
void GLESRenderBackend::resizePreview(uint32_t, uint32_t) {
  throw std::runtime_error("Preview resize requires surface recreation");
}
#ifdef THREEBODY_RENDER_TESTING
std::vector<uint8_t> GLESRenderBackend::readbackForTest() {
  auto &p = *p_;
  if (p.window || !p.serial || !isFrameComplete({p.serial}))
    throw std::runtime_error("Readback requires completed headless frame");
  std::vector<uint8_t> pixels(size_t(p.width) * p.height * 4),
      flipped(pixels.size());
  glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
  glReadPixels(0, 0, p.width, p.height, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels.data());
  glCheck("readback");
  for (uint32_t y = 0; y < p.height; ++y)
    std::memcpy(flipped.data() + size_t(y) * p.width * 4,
                pixels.data() + size_t(p.height - 1 - y) * p.width * 4,
                p.width * 4);
  return flipped;
}
#endif
} // namespace threebody
