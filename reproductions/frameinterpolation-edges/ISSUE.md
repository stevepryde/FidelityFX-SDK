# FSR3 game-field reconstruction loses outgoing perspective motion on a procedural plane

The current-to-previous game-motion field cannot represent the correct motion
in an outgoing screen-edge region, even for an infinite stationary plane with
exact depth and motion. This report isolates the motion-field stage of a
screen-edge image artifact we are investigating.

A small standalone reproduction is attached. It runs your unmodified
`computeGameFieldMvs` and common helpers through Vulkan, with procedural inputs.
It requires DXC, Vulkan and a C++ compiler; no renderer, game, captured resources,
Rust port, or complete SDK build is involved. One command and full input
formulas are in `README.md`.

Reproduced against SDK 1.1.4 (`c6efa6bf7f2027b3ec94f28578bb5965eabb9e55`) and
current main (`7539ae8b6568ad29ed78a753404351daf3b5cca7`). Local execution is
Apple M5 / MoltenVK, DXC 1.9.0.5399. Native AMD/DX12 confirmation would be useful.

**Scene:** 1200×750, stationary plane 2 m below a level camera, 80° vertical FOV,
4 m forward movement between frames, reversed infinite depth, near plane 0.3 m.
No jitter, distortion, UI, dynamic objects, or optical-flow dependency. The
fixture uses your original nearest-depth selection and RG16F motion precision.

**Expected:** at target pixel (600,700), endpoint-linear midpoint reprojection
requires about 171.365 pixels of vertical half-motion, from the analytic plane
projection. This matches the interpolation model used by the primary splats.

**Observed:** the maximum vertical magnitude in the generated game field is
117.279 pixels. Secondary propagation copies visible-source vectors, and later
pyramid inpainting only averages available vectors, so it cannot supply the
missing larger value. The specific secondary winner varies with GPU ordering;
the range deficit does not depend on that ordering.

The attachment stops at the motion-field stage. It does not test final-image
quality or possible compensation by later optical-flow/color-inpainting stages.
Separately, our Metal replay of the original-HLSL sequence (with serialized SPD
tile scheduling) produced bowed stripes; replacing only its game field with
analytic correspondence straightened them. That larger diagnostic is not part
of this attachment and is not a claim of supported native AMD/DX12 validation.

There is also a separate padded-invocation bounds bug in this pass, covered by
the first four cases and a two-line proposed guard. The broad ground case excludes
padding and still fails. This distinguishes it from the related input-domain
report in #190, which concerns `ReconstructAndDilate`.

[RFC #197](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/issues/197)
discusses interpolation confidence and performance, but does not add the
outgoing-domain correspondence missing here. We found no matching open or closed
PR in the SDK repository on 2026-11-12.

We would welcome an upstream correction or advice on the intended way to cover
previous-visible/current-outside geometry. The reproduction and geometric
reference are provided independently of any proposed replacement algorithm.
