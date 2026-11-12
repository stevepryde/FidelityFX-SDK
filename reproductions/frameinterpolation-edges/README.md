# FSR3 game motion-field edge reproductions

A standalone C++/Vulkan runner and procedural HLSL fixture. No Rust, wgpu,
SGL, game, assets, captures, window, swapchain, or full SDK build is needed.
The shader includes AMD's **unchanged** common, nearest-depth selection, and
`computeGameFieldMvs` source from the SDK checkout you select.

Requires Python 3, a C++17 compiler, Vulkan, and DXC with SPIR-V support.
Use a Visual Studio developer shell on Windows.

```sh
python3 run.py --sdk /path/to/FidelityFX-SDK --vulkan-sdk /path/to/VulkanSDK/platform
```

With system Vulkan and DXC on Linux, omit `--vulkan-sdk`. `--dxc` and `--cxx`
select tools explicitly. Outputs go into `build/`; `result.txt` contains the
numbers and `ground-field.bin` contains row-major packed uint32 X/Y entries.

Tested on Apple M5 through Vulkan/MoltenVK, with DXC 1.9.0.5399 and Vulkan SDK
1.4.357.1. This is execution of the original HLSL compiled to SPIR-V; it is
**not** validation on native AMD hardware or a complete DX12/Vulkan FSR runtime.
The Windows/Linux build paths have not been executed here.

Both SDK 1.1.4 (`c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`) and main
(`7539ae8b6568ad29ed78a753404351daf3b5cca7`) reproduce the results below.

## 1. Padded invocations publish invalid primary vectors

At 32×750, every valid source moves upward by one-quarter of the frame.
No valid source can splat a primary vector into rows 700–749. The dispatch
is rounded up to 8×8, as in the SDK. The first pair of cases returns zero
for padded loads.

Observed: **32 primary zero-motion entries on the final row**. The independent
control excludes padded invocations before calling the unchanged function;
it produces **zero**. On this GPU, y=750's zero-motion UV round-trip becomes
749.99994, so the invalid invocation writes an in-bounds location.

The second pair models a **32×752 shared texture allocation** with logical
`RenderSize=32×750`. AMD allocates these textures at `maxRenderSize`, so texels
outside the current render size can contain data from earlier frames. Give
those allocated padded texels depth 0.3 and motion `(0,-0.0625)`; valid texels
still have `(0,-0.25)`. The padded invocations publish **96 false primaries**
in rows 726–728. The guard removes all 96. This case does not depend on the
zero-motion floating-point round-trip, and is the preferred cross-GPU repro.

Both pairs also require a valid interior primary with the correct motion.
The ground case checks an interior correspondence against its plane geometry.
A shader that stops producing output fails these controls.

`bounds-fix.patch` adds the render-domain guard to the shared HLSL function,
covering callers from either shader backend. Apply it at the current SDK root:

```sh
git apply /path/to/bounds-fix.patch
```

Then repeat the command: all four bounds cases must report zero invalid primaries
while preserving valid interior motion.
This patch was applied to a private copy of current main and executed here.
The original checkout and vendored sources were preserved.

Related upstream report: [#190](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/issues/190)
identifies the same input-domain failure in `ReconstructAndDilate`, a different
pass. This patch does not claim to fix that separate pass.

## 2. Outgoing perspective motion cannot be reconstructed

The final case excludes padding. Its input is an infinite stationary plane
2 metres below a level camera, vertical FOV 80°, 1200×750, reversed infinite
depth with near plane 0.3 m, and a 4 m forward camera step. There is no jitter,
UI, distortion, object motion, optical flow, history, or upscaler dependency
in this isolated motion-field dispatch.

For current-frame UV `(u,v)`, the plane has:

```
z = 2 / ((v - 0.5) * 2 * tan(40°))
depth = 0.3 / z
previousUV - currentUV = -(UV - 0.5) * 4 / (z + 4)
```

Sky has zero depth/motion. Motion is quantized to RG16F; AMD's original
`FindNearestDepth` selects the dilated depth/motion sample. Procedural color
samples perform bilinear sampling of stationary world stripes with a 6.2 m
period, so the two frames are not temporally aliased copies.

The original half-motion field has a maximum vertical magnitude of **117.279
pixels**, including secondary entries. At target pixel `(600,700)`, the exact
endpoint-linear midpoint projection requires **171.365 pixels**. The actual
secondary entry varies with thread ordering but cannot reach the required value.

The reference is derived from plane geometry, independently of the shader:
for target `v`, set `a=(v-.5)*tan(40°)` and solve
`a*z² + (4*a-1)*z - 2 = 0`; then half-motion is
`-(UV-.5)*4/(2*z+4)`.

This test stops at the first failing stage. It does not substitute a different
inpainting implementation or claim its field dump is a completed generated
frame. The SDK's later vector pyramid computes weighted averages, which cannot
create a vector outside the supplied range. This package alone does not test
the final image, optical-flow compensation, or a complete supported FSR runtime.

Separately, our original-HLSL sequence diagnostic produced bowed stripes;
replacing only its game field with geometric correspondence straightened them.
That diagnostic uses Metal replay and serialized SPD tile scheduling and is
**not included in this minimal package**. It motivated this reduction, but the
claim independently reproducible here is the motion-field range deficit.

The program prints `REPRODUCED` for this measured range deficit. That diagnostic
is **not** a passing image-quality test. `bounds-fix.patch` leaves it unresolved.
The broader correction needs correspondence for geometry visible previously
but outside the current view; simply rescaling secondary vectors would assume
unseen geometry and motion.

Files are MIT licensed; see `LICENSE`. AMD headers retain their own copyright
notices in the selected SDK. The downloadable bundle also contains an unchanged
subset of current AMD main in `amd-sdk/` so it needs no full SDK checkout:

```sh
python3 run.py --sdk amd-sdk --vulkan-sdk /path/to/VulkanSDK/platform
```

`AMD_SOURCE.txt` records that subset's provenance. `ISSUE.md` and `BOUNDS_PR.md`
are proposed submissions; nothing has been posted upstream.
