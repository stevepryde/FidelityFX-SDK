# Exclude out-of-render sources from the frame-interpolation game field

Rounded-up dispatch threads can load depth/motion outside `RenderSize()`
and splat false primary vectors into valid edge pixels. Guard the input domain
before building the game field. This is related to #190's report in the separate
`ReconstructAndDilate` pass; it does not close that issue.

The attached 32×750 standalone Vulkan reproduction models allocated padded
texels in a 32×752 shared texture. It produces 96 false edge primaries with
unchanged main, and zero after this patch. Valid sources move upward, so none
can legitimately reach those rows. Interior motion remains correct.
A separate zero-filled-padding case produces 32 false final-row primaries on
this GPU due to floating-point rounding; those also disappear. Tested on Apple M5/MoltenVK
with DXC 1.9.0.5399; native AMD/DX12 validation remains welcome.

The broader outgoing-ground reconstruction defect is independently reproduced
with padding excluded and is not fixed by this guard.
