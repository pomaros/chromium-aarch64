// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
//
// Dawn (Chromium's WebGPU) on AArch64 AROS through its Vulkan backend:
// discover the Vulkan adapter (libvulkan.so.1 by dlopen, the driver through
// vulkan.hidd), create a device, draw one triangle from a WGSL shader into a
// 64x64 RGBA8 texture cleared to blue, copy it to a buffer, map it and check
// two pixels: the centre is red (the triangle) and a corner is blue (the
// clear). Prints [DAWNPROBE] lines and result=PASS.

#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>
#include <dawn/webgpu_cpp.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kSize = 64;
constexpr uint32_t kRowBytes = 256;  // a multiple of 256, as copies require

int g_failures = 0;

void Check(const char* what, bool ok) {
  std::printf("[DAWNPROBE] %s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) {
    g_failures++;
  }
}

const char kShader[] = R"(
@vertex fn vs(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(0.0, 0.8), vec2f(-0.8, -0.8), vec2f(0.8, -0.8));
  return vec4f(p[i], 0.0, 1.0);
}
@fragment fn fs() -> @location(0) vec4f {
  return vec4f(1.0, 0.0, 0.0, 1.0);
}
)";

}  // namespace

int main() {
  // Unbuffered, so a hang still shows how far the probe got.
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("[DAWNPROBE] start\n");
  dawnProcSetProcs(&dawn::native::GetProcs());
  dawn::native::DawnInstanceDescriptor dawnDesc = {};
  dawnDesc.loggingCallback = [](WGPULoggingType type, const char* message, void*) {
    std::printf("[DAWNPROBE] dawn log type=%u %s\n", static_cast<unsigned>(type),
                message ? message : "");
  };
  wgpu::InstanceDescriptor instanceDesc = {};
  instanceDesc.nextInChain = &dawnDesc;
  dawn::native::Instance instance(
      reinterpret_cast<const WGPUInstanceDescriptor*>(&instanceDesc));
  std::printf("[DAWNPROBE] instance\n");

  wgpu::RequestAdapterOptions options = {};
  options.backendType = wgpu::BackendType::Vulkan;
  std::printf("[DAWNPROBE] enumerating vulkan adapters\n");
  std::vector<dawn::native::Adapter> adapters = instance.EnumerateAdapters(&options);
  std::printf("[DAWNPROBE] vulkan adapters=%zu\n", adapters.size());
  if (adapters.empty()) {
    Check("adapter", false);
    std::printf("[DAWNPROBE] result=FAIL\n");
    return 20;
  }

  wgpu::AdapterProperties properties = {};
  wgpu::Adapter(adapters[0].Get()).GetProperties(&properties);
  std::printf("[DAWNPROBE] adapter name=\"%s\" driver=\"%s\" type=%u\n",
              properties.name ? properties.name : "",
              properties.driverDescription ? properties.driverDescription : "",
              static_cast<unsigned>(properties.adapterType));

  std::printf("[DAWNPROBE] creating device\n");
  wgpu::Device device = wgpu::Device::Acquire(adapters[0].CreateDevice());
  Check("device", device != nullptr);
  if (!device) {
    std::printf("[DAWNPROBE] result=FAIL\n");
    return 20;
  }
  device.SetUncapturedErrorCallback(
      [](WGPUErrorType type, const char* message, void*) {
        std::printf("[DAWNPROBE] device error type=%u %s\n",
                    static_cast<unsigned>(type), message ? message : "");
        g_failures++;
      },
      nullptr);

  wgpu::ShaderModuleWGSLDescriptor wgsl = {};
  wgsl.code = kShader;
  wgpu::ShaderModuleDescriptor shaderDesc = {};
  shaderDesc.nextInChain = &wgsl;
  wgpu::ShaderModule shader = device.CreateShaderModule(&shaderDesc);

  wgpu::ColorTargetState colorTarget = {};
  colorTarget.format = wgpu::TextureFormat::RGBA8Unorm;
  wgpu::FragmentState fragment = {};
  fragment.module = shader;
  fragment.entryPoint = "fs";
  fragment.targetCount = 1;
  fragment.targets = &colorTarget;
  wgpu::RenderPipelineDescriptor pipelineDesc = {};
  pipelineDesc.vertex.module = shader;
  pipelineDesc.vertex.entryPoint = "vs";
  pipelineDesc.fragment = &fragment;
  wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);
  Check("pipeline", pipeline != nullptr);

  wgpu::TextureDescriptor textureDesc = {};
  textureDesc.size = {kSize, kSize, 1};
  textureDesc.format = wgpu::TextureFormat::RGBA8Unorm;
  textureDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
  wgpu::Texture texture = device.CreateTexture(&textureDesc);

  wgpu::BufferDescriptor bufferDesc = {};
  bufferDesc.size = kRowBytes * kSize;
  bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
  wgpu::Buffer readback = device.CreateBuffer(&bufferDesc);

  wgpu::RenderPassColorAttachment attachment = {};
  attachment.view = texture.CreateView();
  attachment.loadOp = wgpu::LoadOp::Clear;
  attachment.storeOp = wgpu::StoreOp::Store;
  attachment.clearValue = {0.0, 0.0, 1.0, 1.0};
  wgpu::RenderPassDescriptor passDesc = {};
  passDesc.colorAttachmentCount = 1;
  passDesc.colorAttachments = &attachment;

  wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
  wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
  pass.SetPipeline(pipeline);
  pass.Draw(3);
  pass.End();
  wgpu::ImageCopyTexture source = {};
  source.texture = texture;
  wgpu::ImageCopyBuffer destination = {};
  destination.buffer = readback;
  destination.layout.bytesPerRow = kRowBytes;
  destination.layout.rowsPerImage = kSize;
  wgpu::Extent3D extent = {kSize, kSize, 1};
  encoder.CopyTextureToBuffer(&source, &destination, &extent);
  wgpu::CommandBuffer commands = encoder.Finish();
  device.GetQueue().Submit(1, &commands);
  std::printf("[DAWNPROBE] submitted\n");

  bool mapped = false;
  bool done = false;
  struct MapState {
    bool* mapped;
    bool* done;
  } state = {&mapped, &done};
  readback.MapAsync(
      wgpu::MapMode::Read, 0, bufferDesc.size,
      [](WGPUBufferMapAsyncStatus status, void* userdata) {
        auto* s = static_cast<MapState*>(userdata);
        *s->mapped = status == WGPUBufferMapAsyncStatus_Success;
        *s->done = true;
      },
      &state);
  for (int spin = 0; !done && spin < 10000; spin++) {
    device.Tick();
    dawn::native::InstanceProcessEvents(instance.Get());
  }
  Check("map", mapped);
  if (mapped) {
    const uint8_t* pixels = static_cast<const uint8_t*>(readback.GetConstMappedRange());
    const uint8_t* centre = pixels + (kSize / 2) * kRowBytes + (kSize / 2) * 4;
    const uint8_t* corner = pixels + 1 * kRowBytes + 1 * 4;
    std::printf("[DAWNPROBE] centre=%u,%u,%u,%u corner=%u,%u,%u,%u\n", centre[0],
                centre[1], centre[2], centre[3], corner[0], corner[1], corner[2],
                corner[3]);
    Check("triangle red", centre[0] == 255 && centre[1] == 0 && centre[2] == 0 &&
                              centre[3] == 255);
    Check("clear blue", corner[0] == 0 && corner[1] == 0 && corner[2] == 255 &&
                            corner[3] == 255);
    readback.Unmap();
  }

  std::printf("[DAWNPROBE] result=%s\n", g_failures ? "FAIL" : "PASS");
  return g_failures ? 20 : 0;
}
