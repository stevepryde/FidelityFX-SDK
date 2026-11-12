// Standalone input fixture for AMD's unmodified game-field shader.
// SPDX-License-Identifier: MIT
#define FFX_GPU 1
#define FFX_HLSL 1
#include "ffx_core.h"

struct Config { uint width; uint height; uint scenario; uint excludePadding; };
[[vk::push_constant]] ConstantBuffer<Config> config;
[[vk::binding(0, 0)]] RWStructuredBuffer<uint2> field;

int2 RenderSize() { return int2(config.width, config.height); }
int2 DisplaySize() { return RenderSize(); }
int2 InterpolationRectBase() { return int2(0, 0); }
int2 InterpolationRectSize() { return RenderSize(); }
// SDK 2.x renamed these accessors; both select the same full-screen rectangle.
int2 ColorRectBase() { return InterpolationRectBase(); }
int2 ColorRectSize() { return InterpolationRectSize(); }
uint BackBufferTransferFunction() { return 0; }
float MinLuminance() { return 0; }
float MaxLuminance() { return 1000; }
float2 Jitter() { return 0; }
float4 DeviceToViewSpaceTransformFactors() { return float4(-1.1920929e-7, .3, 1, 1); }
#define FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION 0
#include "frameinterpolation/ffx_frameinterpolation_common.h"

float2 SampleDistortionField(float2 uv) { return 0; }
float planeZ(float v) { return 2.0 / ((v - .5) * 2.0 * tan(radians(40.0))); }
float LoadInputDepth(int2 p) {
    if (!IsOnScreen(p, RenderSize())) return 0;
    if (config.scenario != 1) return .3;
    float v = (p.y + .5) / config.height;
    return v > .5 ? .3 / planeZ(v) : 0;
}
float2 LoadInputMotionVector(int2 p) {
    if (!IsOnScreen(p, RenderSize())) return 0;
    if (config.scenario != 1) return float2(0, -.25);
    float2 uv = (float2(p) + .5) / RenderSize();
    float2 mv = uv.y > .5 ? -(uv - .5) * 4.0 / (planeZ(uv.y) + 4.0) : 0;
    return f16tof32(f32tof16(mv)); // the SDK's RG16F dilated motion texture
}
// Use the SDK's original nearest-depth selection, including its tie order.
// The unused ReconstructAndDilate body needs these callbacks to parse.
void UpdateReconstructedDepthPreviousFrame(int2 p, float depth) {}
void StoreDilatedDepth(int2 p, float depth) {}
void StoreDilatedMotionVectors(int2 p, float2 mv) {}
#define FFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH 1
#define FFX_FRAMEINTERPOLATION_OPTION_LOW_RES_MOTION_VECTORS 1
#include "frameinterpolation/ffx_frameinterpolation_reconstruct_dilated_velocity_and_previous_depth.h"
float LoadDilatedDepth(int2 p) {
    // Shared SDK resources are allocated at maxRenderSize. Case 2 models
    // retained texels in a 32x752 allocation with logical RenderSize=32x750.
    if (config.scenario == 2 && IsOnScreen(p, int2(config.width, (config.height + 7) & ~7))) return .3;
    if (!IsOnScreen(p, RenderSize())) return 0;
    float depth; int2 nearest;
    FindNearestDepth(p, RenderSize(), depth, nearest);
    return depth;
}
float2 LoadDilatedMotionVector(int2 p) {
    if (config.scenario == 2 && IsOnScreen(p, int2(config.width, (config.height + 7) & ~7)))
        return float2(0, IsOnScreen(p, RenderSize()) ? -.25 : -.0625);
    if (!IsOnScreen(p, RenderSize())) return 0;
    float depth; int2 nearest;
    FindNearestDepth(p, RenderSize(), depth, nearest);
    return LoadInputMotionVector(nearest);
}
float stripe(int2 p, bool previous) {
    p = clamp(p, int2(0, 0), RenderSize() - 1);
    float v = (p.y + .5) / config.height;
    if (v <= .5) return 0;
    float worldZ = (previous ? 4.0 : 0.0) - planeZ(v);
    return (int(floor(worldZ / 3.1)) % 2) == 0 ? .25 : .75;
}
float4 sampleColor(float2 uv, bool previous) {
    if (config.scenario != 1) return 1;
    BilinearSamplingData b = GetBilinearSamplingData(uv, RenderSize());
    float color = 0;
    for (int i = 0; i < 4; ++i) color += stripe(b.iBasePos + b.iOffsets[i], previous) * b.fWeights[i];
    return float4(color, color, color, 1);
}
float4 SamplePreviousBackbuffer(float2 uv) { return sampleColor(uv, true); }
float4 SampleCurrentBackbuffer(float2 uv) { return sampleColor(uv, false); }
void UpdateGameMotionVectorField(int2 p, uint2 packed) {
    uint index = uint(p.y) * config.width + uint(p.x);
    InterlockedMax(field[index].x, packed.x);
    InterlockedMax(field[index].y, packed.y);
}
uint UpdateGameMotionVectorFieldEx(int2 p, uint2 packed) {
    uint index = uint(p.y) * config.width + uint(p.x), oldX, oldY;
    InterlockedMax(field[index].x, packed.x, oldX);
    InterlockedMax(field[index].y, packed.y, oldY);
    return max(oldX, oldY);
}
// Algorithm and packing are AMD's source, not a reimplementation.
#include "frameinterpolation/ffx_frameinterpolation_game_motion_vector_field.h"
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (config.excludePadding && any(id.xy >= uint2(RenderSize()))) return;
    computeGameFieldMvs(int2(id.xy));
}
