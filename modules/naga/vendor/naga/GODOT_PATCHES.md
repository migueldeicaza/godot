# Godot patches

This is Naga 29.0.3 from crates.io, vendored because the experimental Godot
Metal backend needs one GLSL frontend correction that is not present in Naga
trunk as of commit `83b9fa1661a7ecb7c98e3c48ef400ef7d4d2b40a`.

Godot's Vulkan-style GLSL uses the same separate `texture2D` object with both a
comparison sampler and an ordinary sampler. Naga promotes the texture to its
`Depth` image class when it encounters the comparison constructor. The local
patch keeps the ordinary `sampler*` constructor and texture-operation overloads
available for that class. Naga's IR validator and MSL backend already support
this combination. It also lowers GLSL's floating-point explicit LOD to Naga's
integer depth-texture LOD representation.

The GLSL frontend patch also lowers the unsigned-scalar subgroup reductions and
`subgroupBroadcastFirst` operations used by Godot's clustered Uber Shader into
Naga's existing subgroup IR statements. Common GLSL math builtins also receive
explicit 16-bit floating-point overloads for Godot's mobile forward lighting,
and overload ranking permits the standard f16-to-f32 widening conversion.

Uniform arrays whose elements are textures or samplers are lowered to Naga
`BindingArray` types instead of ordinary data arrays, enabling Godot's
dynamically indexed lightmap texture arrays. The MSL backend emits fixed-size
binding arrays as direct `metal::array` texture or sampler arguments, matching
Godot's existing flat Metal resource slots; runtime-sized binding arrays retain
Naga's argument-buffer representation.
