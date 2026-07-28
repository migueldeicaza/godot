use naga::{
    back::{msl, spv},
    front::glsl,
    valid::{ValidationFlags, Validator},
    ResourceBinding, ShaderStage,
};
use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::{c_char, c_void, CStr, CString},
    panic::{catch_unwind, AssertUnwindSafe},
    ptr, slice,
    sync::atomic::{AtomicUsize, Ordering},
};

static DUMP_INDEX: AtomicUsize = AtomicUsize::new(0);

struct ParsedShader {
    module: naga::Module,
    info: naga::valid::ModuleInfo,
    stage: ShaderStage,
    specialization_constants: Vec<SpecializationConstant>,
    combined_samplers: Vec<CombinedSampler>,
}

struct SpecializationConstant {
    id: u32,
    name: String,
}

struct CombinedSampler {
    group: u32,
    binding: u32,
    synthetic_sampler_binding: u32,
    image_dimension: naga::ImageDimension,
    image_arrayed: bool,
}

const SYNTHETIC_SAMPLER_BINDING_OFFSET: u32 = 1_000;
const DONT_UNROLL_MARKER: &str = "_godot_naga_dont_unroll";

const MODF_POLYFILL: &str = r#"
float _godot_naga_modf(float value, out float whole) {
    whole = trunc(value);
    return value - whole;
}
"#;

const INVERSE_POLYFILLS: &str = r#"
mat2 _godot_naga_inverse(mat2 m) {
    mat2 adj;
    adj[0][0] = m[1][1];
    adj[0][1] = -m[0][1];
    adj[1][0] = -m[1][0];
    adj[1][1] = m[0][0];
    float det = m[0][0] * m[1][1] - m[1][0] * m[0][1];
    return adj * (1.0 / det);
}

mat3 _godot_naga_inverse(mat3 m) {
    mat3 adj;
    adj[0][0] =   (m[1][1] * m[2][2] - m[2][1] * m[1][2]);
    adj[1][0] = - (m[1][0] * m[2][2] - m[2][0] * m[1][2]);
    adj[2][0] =   (m[1][0] * m[2][1] - m[2][0] * m[1][1]);
    adj[0][1] = - (m[0][1] * m[2][2] - m[2][1] * m[0][2]);
    adj[1][1] =   (m[0][0] * m[2][2] - m[2][0] * m[0][2]);
    adj[2][1] = - (m[0][0] * m[2][1] - m[2][0] * m[0][1]);
    adj[0][2] =   (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
    adj[1][2] = - (m[0][0] * m[1][2] - m[1][0] * m[0][2]);
    adj[2][2] =   (m[0][0] * m[1][1] - m[1][0] * m[0][1]);
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    return adj * (1.0 / det);
}

mat4 _godot_naga_inverse(mat4 m) {
    float f00 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
    float f01 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
    float f02 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
    float f03 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
    float f04 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
    float f05 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
    float f06 = m[1][2] * m[3][3] - m[3][2] * m[1][3];
    float f07 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
    float f08 = m[1][1] * m[3][2] - m[3][1] * m[1][2];
    float f09 = m[1][0] * m[3][3] - m[3][0] * m[1][3];
    float f10 = m[1][0] * m[3][2] - m[3][0] * m[1][2];
    float f11 = m[1][1] * m[3][3] - m[3][1] * m[1][3];
    float f12 = m[1][0] * m[3][1] - m[3][0] * m[1][1];
    float f13 = m[1][2] * m[2][3] - m[2][2] * m[1][3];
    float f14 = m[1][1] * m[2][3] - m[2][1] * m[1][3];
    float f15 = m[1][1] * m[2][2] - m[2][1] * m[1][2];
    float f16 = m[1][0] * m[2][3] - m[2][0] * m[1][3];
    float f17 = m[1][0] * m[2][2] - m[2][0] * m[1][2];
    float f18 = m[1][0] * m[2][1] - m[2][0] * m[1][1];
    mat4 adj;
    adj[0][0] =   (m[1][1] * f00 - m[1][2] * f01 + m[1][3] * f02);
    adj[1][0] = - (m[1][0] * f00 - m[1][2] * f03 + m[1][3] * f04);
    adj[2][0] =   (m[1][0] * f01 - m[1][1] * f03 + m[1][3] * f05);
    adj[3][0] = - (m[1][0] * f02 - m[1][1] * f04 + m[1][2] * f05);
    adj[0][1] = - (m[0][1] * f00 - m[0][2] * f01 + m[0][3] * f02);
    adj[1][1] =   (m[0][0] * f00 - m[0][2] * f03 + m[0][3] * f04);
    adj[2][1] = - (m[0][0] * f01 - m[0][1] * f03 + m[0][3] * f05);
    adj[3][1] =   (m[0][0] * f02 - m[0][1] * f04 + m[0][2] * f05);
    adj[0][2] =   (m[0][1] * f06 - m[0][2] * f07 + m[0][3] * f08);
    adj[1][2] = - (m[0][0] * f06 - m[0][2] * f09 + m[0][3] * f10);
    adj[2][2] =   (m[0][0] * f11 - m[0][1] * f09 + m[0][3] * f12);
    adj[3][2] = - (m[0][0] * f08 - m[0][1] * f10 + m[0][2] * f12);
    adj[0][3] = - (m[0][1] * f13 - m[0][2] * f14 + m[0][3] * f15);
    adj[1][3] =   (m[0][0] * f13 - m[0][2] * f16 + m[0][3] * f17);
    adj[2][3] = - (m[0][0] * f14 - m[0][1] * f16 + m[0][3] * f18);
    adj[3][3] =   (m[0][0] * f15 - m[0][1] * f17 + m[0][2] * f18);
    float det = m[0][0] * adj[0][0] + m[0][1] * adj[1][0]
        + m[0][2] * adj[2][0] + m[0][3] * adj[3][0];
    return adj * (1.0 / det);
}
"#;

fn lower_specialization_constants(
    source: &str,
) -> Result<(String, Vec<SpecializationConstant>), String> {
    let mut adjusted = String::with_capacity(source.len());
    let mut constants = Vec::new();
    for line in source.split_inclusive('\n') {
        let Some(layout_start) = line.find("layout(constant_id") else {
            adjusted.push_str(line);
            continue;
        };
        let layout = &line[layout_start..];
        let equals = layout
            .find('=')
            .ok_or_else(|| "Specialization constant has no ID".to_owned())?;
        let id_text: String = layout[equals + 1..]
            .trim_start()
            .chars()
            .take_while(char::is_ascii_digit)
            .collect();
        let id = id_text
            .parse()
            .map_err(|_| "Specialization constant has an invalid ID".to_owned())?;
        let layout_end = layout
            .find(')')
            .ok_or_else(|| "Specialization constant has an unterminated layout".to_owned())?;
        let declaration = layout[layout_end + 1..].trim_start();
        let before_value = declaration
            .split_once('=')
            .map(|(before, _)| before)
            .ok_or_else(|| "Specialization constant has no default value".to_owned())?;
        let name = before_value
            .trim()
            .strip_prefix("const ")
            .and_then(|declaration| declaration.split_whitespace().last())
            .ok_or_else(|| "Specialization constant has an invalid declaration".to_owned())?
            .to_owned();
        constants.push(SpecializationConstant { id, name });
        adjusted.push_str(&line[..layout_start]);
        adjusted.push_str(&layout[layout_end + 1..]);
    }
    Ok((adjusted, constants))
}

fn strip_precision_qualifiers(source: &str) -> String {
    let mut adjusted = String::with_capacity(source.len());
    let mut position = 0;
    while position < source.len() {
        let next = ["highp", "mediump", "lowp"]
            .into_iter()
            .filter_map(|qualifier| {
                source[position..]
                    .find(qualifier)
                    .map(|relative| (position + relative, qualifier))
            })
            .min_by_key(|(start, _)| *start);
        let Some((start, qualifier)) = next else {
            adjusted.push_str(&source[position..]);
            break;
        };
        let end = start + qualifier.len();
        let before_is_identifier = source[..start]
            .chars()
            .next_back()
            .is_some_and(|ch| ch.is_ascii_alphanumeric() || ch == '_');
        let after_is_identifier = source[end..]
            .chars()
            .next()
            .is_some_and(|ch| ch.is_ascii_alphanumeric() || ch == '_');
        adjusted.push_str(&source[position..start]);
        if before_is_identifier || after_is_identifier {
            adjusted.push_str(qualifier);
        }
        position = end;
    }
    adjusted
}

fn mark_comparison_samplers(source: String) -> Result<String, String> {
    const CONSTRUCTORS: [&str; 6] = [
        "sampler1DShadow(",
        "sampler1DArrayShadow(",
        "sampler2DShadow(",
        "sampler2DArrayShadow(",
        "samplerCubeShadow(",
        "samplerCubeArrayShadow(",
    ];

    let mut comparison_samplers = BTreeSet::new();
    let mut comparison_textures = BTreeSet::new();
    for constructor in CONSTRUCTORS {
        let mut position = 0;
        while let Some(relative) = source[position..].find(constructor) {
            let arguments_start = position + relative + constructor.len();
            let mut depth = 0;
            let mut comma = None;
            let mut arguments_end = None;
            for (relative, ch) in source[arguments_start..].char_indices() {
                match ch {
                    '(' => depth += 1,
                    ')' if depth == 0 => {
                        arguments_end = Some(arguments_start + relative);
                        break;
                    }
                    ')' => depth -= 1,
                    ',' if depth == 0 && comma.is_none() => {
                        comma = Some(arguments_start + relative)
                    }
                    _ => {}
                }
            }
            let arguments_end = arguments_end.ok_or_else(|| {
                format!("Unterminated comparison sampler constructor '{constructor}'")
            })?;
            let comma = comma.ok_or_else(|| {
                format!("Comparison sampler constructor '{constructor}' has no sampler argument")
            })?;
            let texture = source[arguments_start..comma].trim();
            let sampler = source[comma + 1..arguments_end].trim();
            for (kind, expression) in [("texture", texture), ("sampler", sampler)] {
                if expression.is_empty()
                    || !expression
                        .chars()
                        .all(|ch| ch.is_ascii_alphanumeric() || ch == '_')
                    || !expression
                        .chars()
                        .next()
                        .is_some_and(|ch| ch.is_ascii_alphabetic() || ch == '_')
                {
                    return Err(format!(
                        "Comparison sampler constructor '{constructor}' has unsupported {kind} expression '{expression}'"
                    ));
                }
            }
            comparison_textures.insert(texture.to_owned());
            comparison_samplers.insert(sampler.to_owned());
            position = arguments_end + 1;
        }
    }

    let mut adjusted = source;
    for sampler in comparison_samplers {
        let declaration = format!("uniform sampler {sampler}");
        if !adjusted.contains(&declaration) {
            return Err(format!(
                "Comparison sampler '{sampler}' has no separate sampler declaration"
            ));
        }
        adjusted = adjusted.replace(&declaration, &format!("uniform samplerShadow {sampler}"));
    }
    Ok(remove_scalar_depth_sample_swizzles(
        adjusted,
        &comparison_textures,
    ))
}

fn remove_scalar_depth_sample_swizzles(
    mut source: String,
    comparison_textures: &BTreeSet<String>,
) -> String {
    const CONSTRUCTORS: [&str; 6] = [
        "sampler1D(",
        "sampler1DArray(",
        "sampler2D(",
        "sampler2DArray(",
        "samplerCube(",
        "samplerCubeArray(",
    ];
    let mut removals = Vec::new();
    for texture in comparison_textures {
        for constructor in CONSTRUCTORS {
            let pattern = format!("{constructor}{texture},");
            let mut position = 0;
            while let Some(relative) = source[position..].find(&pattern) {
                let sampler_start = position + relative;
                let Some(call_start) = source[..sampler_start].rfind('(') else {
                    break;
                };
                let function_start = source[..call_start]
                    .char_indices()
                    .rev()
                    .take_while(|(_, ch)| ch.is_ascii_alphanumeric() || *ch == '_')
                    .last()
                    .map(|(index, _)| index)
                    .unwrap_or(call_start);
                if !source[function_start..call_start].starts_with("texture") {
                    position = sampler_start + pattern.len();
                    continue;
                }

                let mut depth = 0;
                let mut call_end = None;
                for (relative, ch) in source[call_start + 1..].char_indices() {
                    match ch {
                        '(' => depth += 1,
                        ')' if depth == 0 => {
                            call_end = Some(call_start + 1 + relative);
                            break;
                        }
                        ')' => depth -= 1,
                        _ => {}
                    }
                }
                let Some(call_end) = call_end else {
                    break;
                };
                if source[call_end + 1..].starts_with(".r")
                    || source[call_end + 1..].starts_with(".x")
                {
                    removals.push(call_end + 1..call_end + 3);
                }
                position = call_end + 1;
            }
        }
    }
    removals.sort_by_key(|range| range.start);
    removals.dedup_by_key(|range| range.start);
    for range in removals.into_iter().rev() {
        source.replace_range(range, "");
    }
    source
}

fn preserve_dont_unroll_annotations(mut source: String) -> Result<String, String> {
    const DEFINITION: &str = "#define SPEC_CONSTANT_LOOP_ANNOTATION [[dont_unroll]]";
    if !source.contains(DEFINITION) {
        return Ok(source);
    }

    source = source.replace(
        DEFINITION,
        "#define SPEC_CONSTANT_LOOP_ANNOTATION _godot_naga_dont_unroll();",
    );
    let version_start = source
        .find("#version")
        .ok_or_else(|| "Godot shader has no version directive".to_owned())?;
    let header_end = version_start
        + source[version_start..]
            .find('\n')
            .ok_or_else(|| "Godot shader has an unterminated version directive".to_owned())?
        + 1;
    source.insert_str(header_end, "void _godot_naga_dont_unroll() {}\n");
    Ok(source)
}

fn add_modf_polyfill(mut source: String) -> Result<String, String> {
    if !source.contains("modf(") {
        return Ok(source);
    }
    source = source.replace("modf(", "_godot_naga_modf(");
    let version_start = source
        .find("#version")
        .ok_or_else(|| "Godot shader has no version directive".to_owned())?;
    let header_end = version_start
        + source[version_start..]
            .find('\n')
            .ok_or_else(|| "Godot shader has an unterminated version directive".to_owned())?
        + 1;
    source.insert_str(header_end, MODF_POLYFILL);
    Ok(source)
}

fn wrap_qualified_boolean_access(source: String, access: &str) -> String {
    let is_identifier_character =
        |character: char| character.is_ascii_alphanumeric() || character == '_';
    let mut adjusted = String::with_capacity(source.len());
    let mut offset = 0;
    while let Some(relative_start) = source[offset..].find(access) {
        let start = offset + relative_start;
        let end = start + access.len();
        let has_identifier_prefix = source[..start]
            .chars()
            .next_back()
            .is_some_and(is_identifier_character);
        let has_identifier_suffix = source[end..]
            .chars()
            .next()
            .is_some_and(is_identifier_character);
        adjusted.push_str(&source[offset..start]);
        if has_identifier_prefix || has_identifier_suffix {
            adjusted.push_str(access);
        } else {
            adjusted.push_str(&format!("bool({access})"));
        }
        offset = end;
    }
    adjusted.push_str(&source[offset..]);
    adjusted
}

fn lower_forward_buffer_booleans(mut source: String) -> String {
    // GLSL buffer booleans occupy a 32-bit slot in Godot's CPU-side layouts,
    // while Naga deliberately rejects `bool` as a host-shareable IR type. Keep
    // the ABI intact by exposing those slots to Naga as uints and converting
    // their reads back to booleans. This is intentionally limited to fields in
    // the built-in forward Uber-shader layouts exercised by this bridge.
    for field in [
        "exterior",
        "box_project",
        "blend_splits",
        "use_occlusion",
        "gi_upscale_for_msaa",
        "volumetric_fog_enabled",
        "blend_ambient",
    ] {
        source = source.replace(&format!("bool {field};"), &format!("uint {field};"));
    }

    for access in [
        "reflections.data[ref_index].box_project",
        "directional_lights.data[i].blend_splits",
        "sdfgi.use_occlusion",
        "implementation_data.gi_upscale_for_msaa",
        "implementation_data.volumetric_fog_enabled",
        "voxel_gi_instances.data[index].blend_ambient",
        "canvas_data.use_pixel_snap",
        "sky_scene_data.fog_enabled",
        "sky_scene_data.volumetric_fog_enabled",
        "sky_scene_data.fog_use_legacy_blending",
    ] {
        source = wrap_qualified_boolean_access(source, access);
    }

    // Material uniforms are generated dynamically, so their field names cannot
    // be enumerated above. Scalar booleans have the same 32-bit ABI as uints in
    // Godot's uniform layouts; expose them as uints to Naga and restore boolean
    // semantics at every qualified read.
    let mut adjusted = String::with_capacity(source.len());
    let mut in_resource_block = false;
    let mut boolean_fields = Vec::new();
    let mut pending_boolean_fields = Vec::new();
    let mut qualified_accesses = Vec::new();
    for source_line in source.split_inclusive('\n') {
        let mut line = source_line.to_owned();
        let trimmed = line.trim_start().to_owned();
        if !in_resource_block && !pending_boolean_fields.is_empty() {
            let declaration = trimmed.trim();
            if declaration.is_empty()
                || declaration.starts_with("//")
                || declaration.starts_with("/*")
            {
                adjusted.push_str(&line);
                continue;
            }
            if let Some(instance) = declaration.strip_suffix(';') {
                if instance
                    .chars()
                    .all(|character| character.is_ascii_alphanumeric() || character == '_')
                {
                    qualified_accesses.extend(
                        pending_boolean_fields
                            .iter()
                            .map(|field| format!("{instance}.{field}")),
                    );
                }
            }
            pending_boolean_fields.clear();
        }
        if !in_resource_block
            && trimmed.contains("layout(")
            && (trimmed.contains(" uniform ") || trimmed.contains(" buffer "))
            && trimmed.contains('{')
        {
            in_resource_block = true;
            boolean_fields.clear();
        }
        if in_resource_block {
            if let Some(declaration) = trimmed.strip_prefix("bool ") {
                if let Some(field) = declaration.trim().strip_suffix(';') {
                    if field
                        .chars()
                        .all(|character| character.is_ascii_alphanumeric() || character == '_')
                    {
                        let indentation = line.len() - trimmed.len();
                        line.replace_range(indentation..indentation + "bool".len(), "uint");
                        boolean_fields.push(field.to_owned());
                    }
                }
            }
            if let Some(after_brace) = trimmed.strip_prefix('}') {
                if let Some(instance) = after_brace.trim().strip_suffix(';') {
                    if instance
                        .chars()
                        .all(|character| character.is_ascii_alphanumeric() || character == '_')
                    {
                        qualified_accesses.extend(
                            boolean_fields
                                .iter()
                                .map(|field| format!("{instance}.{field}")),
                        );
                    }
                } else {
                    pending_boolean_fields.clone_from(&boolean_fields);
                }
                in_resource_block = false;
                boolean_fields.clear();
            }
        }
        adjusted.push_str(&line);
    }
    for access in qualified_accesses {
        adjusted = wrap_qualified_boolean_access(adjusted, &access);
    }
    adjusted
}

fn rewrite_matrix_column_xyz_assignments(source: String) -> String {
    let mut adjusted = String::with_capacity(source.len());
    for source_line in source.split_inclusive('\n') {
        let trimmed = source_line.trim_start();
        let indentation = &source_line[..source_line.len() - trimmed.len()];
        let mut replacement = None;
        for column in 0..4 {
            let target = format!("txform[{column}].xyz");
            let Some(assignment) = trimmed.strip_prefix(&target) else {
                continue;
            };
            let Some((operator, right)) =
                [" += ", " -= ", " *= ", " = "]
                    .into_iter()
                    .find_map(|operator| {
                        assignment
                            .strip_prefix(operator)
                            .map(|right| (operator, right))
                    })
            else {
                continue;
            };
            let Some(right) = right.trim_end().strip_suffix(';') else {
                continue;
            };
            let value = match operator {
                " = " => right.to_owned(),
                " += " => format!("{target} + {right}"),
                " -= " => format!("{target} - {right}"),
                " *= " => format!("{target} * {right}"),
                _ => unreachable!(),
            };
            let newline = source_line.ends_with('\n').then_some("\n").unwrap_or("");
            replacement = Some(format!(
                "{indentation}txform[{column}] = vec4({value}, txform[{column}].w);{newline}"
            ));
            break;
        }
        adjusted.push_str(replacement.as_deref().unwrap_or(source_line));
    }
    adjusted
}

fn adjust_builtin_shader_syntax(source: String) -> String {
    // Naga requires storage buffers to be readable even when the shader only
    // writes them. Metal assigns the same resource slot and Godot already
    // reflects these buffers as writable, so widening access preserves the ABI.
    let source = if source.contains("struct DirectionalLightData") {
        let mut source = source.replace("bool enabled;", "uint enabled;");
        source = wrap_qualified_boolean_access(source, "directional_lights.data[i].enabled");
        for index in 0..4 {
            source = wrap_qualified_boolean_access(
                source,
                &format!("directional_lights.data[{index}].enabled"),
            );
        }
        source
    } else {
        source
    };
    let source = source
        .replace("restrict writeonly buffer", "restrict buffer")
        .replace("restrict buffer writeonly", "restrict buffer")
        .replace("buffer restrict writeonly", "buffer restrict")
        .replace("-1.0 / 0.0", "uintBitsToFloat(0xff800000u)");
    // Naga's GLSL frontend does not lower assignment through a matrix-column
    // swizzle. Preserve the untouched W component explicitly.
    rewrite_matrix_column_xyz_assignments(source)
}

fn widen_forward_half_arguments(source: String) -> String {
    // Make the intended precision explicit at the two boundaries where Naga's
    // GLSL overload inference otherwise differs from GLSLang.
    source
        .replace(
            "vec3(eye_vec), roughness, points,",
            "vec3(eye_vec), float(roughness), points,",
        )
        .replace(
            "half a004 = min(r.x * r.x, exp2(half(-9.28) * ndotv)) * r.x + r.y;",
            "half a004 = half(min(r.x * r.x, exp2(half(-9.28) * ndotv)) * r.x + r.y);",
        )
}

fn type_mobile_uint_returns(source: String) -> String {
    source
        .replace(
            "case SHADER_COUNT_NONE:\n\t\t\treturn 0;",
            "case SHADER_COUNT_NONE:\n\t\t\treturn 0u;",
        )
        .replace(
            "case SHADER_COUNT_SINGLE:\n\t\t\treturn 1;",
            "case SHADER_COUNT_SINGLE:\n\t\t\treturn 1u;",
        )
        .replace(
            "case SHADER_COUNT_MULTIPLE:\n\t\t\treturn bound;\n\t}\n}",
            "case SHADER_COUNT_MULTIPLE:\n\t\t\treturn bound;\n\t}\n\treturn 0u;\n}",
        )
        .replace(
            "uint sc_decals(uint bound) {\n\tif (((sc_packed_1() >> 22) & 1U) != 0) {\n\t\treturn bound;\n\t} else {\n\t\treturn 0;",
            "uint sc_decals(uint bound) {\n\tif (((sc_packed_1() >> 22) & 1U) != 0) {\n\t\treturn bound;\n\t} else {\n\t\treturn 0u;",
        )
}

fn unpack_forward_packed_int3(source: String) -> String {
    const MEMBER: &str = "sdfgi.cascades[cascade].probe_world_offset";
    source.replace(
        &format!("{MEMBER} + probe_posi"),
        &format!("ivec3({MEMBER}.x, {MEMBER}.y, {MEMBER}.z) + probe_posi"),
    )
}

fn inline_forward_depth_texture_parameter(source: String) -> String {
    // Every call passes the same directional depth atlas. Referencing it
    // directly avoids a Naga GLSL type-inference cycle where an ordinary
    // texture function parameter becomes a depth texture only after its
    // textureLod expression has already been lowered as a color sample.
    const DEPTH_HINT: &str = r#"float _godot_naga_directional_depth_hint() {
    return textureProj(sampler2DShadow(directional_shadow_atlas, shadow_sampler), vec4(0.5));
}

float _godot_naga_shadow_atlas_depth_hint() {
    return textureProj(sampler2DShadow(shadow_atlas, shadow_sampler), vec4(0.5));
}

"#;
    let mut source = source;
    for return_type in ["float", "half"] {
        let signature = format!("{return_type} sample_directional_soft_shadow(texture2D shadow, ");
        if source.contains(&signature) {
            source = source.replacen(&signature, &format!("{DEPTH_HINT}{signature}"), 1);
            break;
        }
    }
    source.replace(
        "sampler2D(shadow, SAMPLER_LINEAR_CLAMP)",
        "sampler2D(directional_shadow_atlas, SAMPLER_LINEAR_CLAMP)",
    )
}

fn layout_value(line: &str, name: &str) -> Result<u32, String> {
    let start = line
        .find(name)
        .ok_or_else(|| format!("Combined sampler declaration is missing '{name}'"))?
        + name.len();
    let value = line[start..]
        .trim_start()
        .strip_prefix('=')
        .ok_or_else(|| format!("Combined sampler declaration has no value for '{name}'"))?
        .trim_start();
    let digits = value
        .chars()
        .take_while(char::is_ascii_digit)
        .collect::<String>();
    digits
        .parse()
        .map_err(|_| format!("Combined sampler declaration has an invalid '{name}' value"))
}

fn replace_identifier(source: &str, identifier: &str, replacement: &str) -> String {
    let is_identifier_character =
        |character: char| character.is_ascii_alphanumeric() || character == '_';
    let mut adjusted = String::with_capacity(source.len());
    let mut position = 0;
    while let Some(relative) = source[position..].find(identifier) {
        let start = position + relative;
        let end = start + identifier.len();
        let before_is_identifier = source[..start]
            .chars()
            .next_back()
            .is_some_and(is_identifier_character);
        let after_is_identifier = source[end..]
            .chars()
            .next()
            .is_some_and(is_identifier_character);
        adjusted.push_str(&source[position..start]);
        if before_is_identifier || after_is_identifier {
            adjusted.push_str(identifier);
        } else {
            adjusted.push_str(replacement);
        }
        position = end;
    }
    adjusted.push_str(&source[position..]);
    adjusted
}

fn strip_unused_ltc_helper(mut source: String) -> Result<String, String> {
    const CALL: &str = "ltc_evaluate_specular(";
    const DEFINITION: &str = "void ltc_evaluate_specular(";

    // Several generated compute shaders include the shared area-light helper even
    // though they never call it. Naga's GLSL frontend rejects its combined-sampler
    // parameters before dead-code elimination can remove the function.
    if source.match_indices(CALL).count() != 1 {
        return Ok(source);
    }
    let Some(start) = source.find(DEFINITION) else {
        return Ok(source);
    };
    let opening_brace = start
        + source[start..]
            .find('{')
            .ok_or_else(|| "LTC helper has no opening brace".to_owned())?;
    let mut depth = 0_u32;
    let mut end = None;
    for (relative, character) in source[opening_brace..].char_indices() {
        match character {
            '{' => depth += 1,
            '}' => {
                depth = depth
                    .checked_sub(1)
                    .ok_or_else(|| "LTC helper has an unmatched closing brace".to_owned())?;
                if depth == 0 {
                    end = Some(opening_brace + relative + character.len_utf8());
                    break;
                }
            }
            _ => {}
        }
    }
    let mut end = end.ok_or_else(|| "LTC helper has no closing brace".to_owned())?;
    if source[end..].starts_with('\n') {
        end += 1;
    }
    source.replace_range(start..end, "");
    Ok(source)
}

fn split_combined_samplers(source: String) -> Result<(String, Vec<CombinedSampler>), String> {
    struct SourceCombinedSampler {
        name: String,
        sampler_type: &'static str,
        texture_type: &'static str,
        binding: CombinedSampler,
    }

    const TYPES: [(&str, &str, naga::ImageDimension, bool); 6] = [
        ("sampler1D", "texture1D", naga::ImageDimension::D1, false),
        (
            "sampler1DArray",
            "texture1DArray",
            naga::ImageDimension::D1,
            true,
        ),
        ("sampler2D", "texture2D", naga::ImageDimension::D2, false),
        (
            "sampler2DArray",
            "texture2DArray",
            naga::ImageDimension::D2,
            true,
        ),
        ("sampler3D", "texture3D", naga::ImageDimension::D3, false),
        (
            "samplerCube",
            "textureCube",
            naga::ImageDimension::Cube,
            false,
        ),
    ];

    let mut active = Vec::new();
    let mut conditional_names = BTreeSet::new();
    for line in source.lines() {
        let Some((_, declaration)) = line.split_once("uniform ") else {
            continue;
        };
        let mut declaration_tokens = declaration.split_whitespace();
        let Some(source_type) = declaration_tokens.next() else {
            continue;
        };
        let Some((sampler_type, texture_type, image_dimension, image_arrayed)) = TYPES
            .iter()
            .find(|(sampler_type, _, _, _)| *sampler_type == source_type)
            .copied()
        else {
            continue;
        };
        let Some(name) = declaration_tokens
            .next()
            .and_then(|token| token.split(';').next())
        else {
            continue;
        };
        if name.is_empty()
            || !name
                .chars()
                .all(|character| character.is_ascii_alphanumeric() || character == '_')
        {
            continue;
        }
        if conditional_names.contains(name) {
            continue;
        }
        if let Some(index) = active
            .iter()
            .position(|candidate: &SourceCombinedSampler| candidate.name == name)
        {
            // The source still contains preprocessor branches. If several branches
            // declare the same binding with different sampler dimensions, leave that
            // name untouched and let the GLSLang parser bridge select the active one.
            active.remove(index);
            conditional_names.insert(name.to_owned());
            continue;
        }
        let group = if line.contains("set =") || line.contains("set=") {
            layout_value(line, "set")?
        } else {
            0
        };
        let binding = layout_value(line, "binding")?;
        active.push(SourceCombinedSampler {
            name: name.to_owned(),
            sampler_type,
            texture_type,
            binding: CombinedSampler {
                group,
                binding,
                synthetic_sampler_binding: binding + SYNTHETIC_SAMPLER_BINDING_OFFSET,
                image_dimension,
                image_arrayed,
            },
        });
    }
    if active.is_empty() {
        return Ok((source, Vec::new()));
    }

    let mut adjusted = String::with_capacity(source.len());
    for source_line in source.split_inclusive('\n') {
        let mut line = source_line.to_owned();
        for combined in &active {
            let declaration = format!("uniform {} {};", combined.sampler_type, combined.name);
            if line.contains(&declaration) {
                line = line.replace(
                    &declaration,
                    &format!(
                        "uniform {} {}_texture;\n\
                         layout(set = {}, binding = {}) uniform sampler {}_sampler;",
                        combined.texture_type,
                        combined.name,
                        combined.binding.group,
                        combined.binding.synthetic_sampler_binding,
                        combined.name
                    ),
                );
                continue;
            }

            if line.trim_start().starts_with("void ltc_evaluate_specular(") {
                line = line.replace(
                    &format!("{} {}, ", combined.sampler_type, combined.name),
                    "",
                );
                continue;
            }
            if line.contains("ltc_evaluate_specular(") {
                line = line.replace(&format!("{}, ", combined.name), "");
                continue;
            }
            line = replace_identifier(
                &line,
                &combined.name,
                &format!(
                    "{}({}_texture, {}_sampler)",
                    combined.sampler_type, combined.name, combined.name
                ),
            );
        }
        adjusted.push_str(&line);
    }
    let mut combined_bindings = Vec::new();
    for combined in active {
        if !combined_bindings.iter().any(|binding: &CombinedSampler| {
            binding.group == combined.binding.group && binding.binding == combined.binding.binding
        }) {
            combined_bindings.push(combined.binding);
        }
    }
    Ok((adjusted, combined_bindings))
}

fn add_inverse_polyfills(source: String) -> Result<String, String> {
    let mut adjusted = String::with_capacity(source.len());
    let mut position = 0;
    let mut replaced = false;
    while let Some(relative) = source[position..].find("inverse") {
        let start = position + relative;
        let name_end = start + "inverse".len();
        let before_is_identifier = source[..start]
            .chars()
            .next_back()
            .is_some_and(|ch| ch.is_ascii_alphanumeric() || ch == '_');
        let call_start = name_end
            + source[name_end..]
                .chars()
                .take_while(|ch| ch.is_ascii_whitespace())
                .map(char::len_utf8)
                .sum::<usize>();
        adjusted.push_str(&source[position..start]);
        if !before_is_identifier && source[call_start..].starts_with('(') {
            adjusted.push_str("_godot_naga_inverse");
            replaced = true;
        } else {
            adjusted.push_str("inverse");
        }
        position = name_end;
    }
    adjusted.push_str(&source[position..]);

    if replaced {
        let version_start = adjusted
            .find("#version")
            .ok_or_else(|| "Godot shader has no version directive".to_owned())?;
        let header_end = version_start
            + adjusted[version_start..]
                .find('\n')
                .ok_or_else(|| "Godot shader has an unterminated version directive".to_owned())?
            + 1;
        adjusted.insert_str(header_end, INVERSE_POLYFILLS);
    }
    Ok(adjusted)
}

fn godot_source(
    stage: ShaderStage,
    source: &str,
) -> Result<(String, Vec<SpecializationConstant>, Vec<CombinedSampler>), String> {
    let adjusted = inline_forward_depth_texture_parameter(strip_precision_qualifiers(source));
    let mut adjusted = mark_comparison_samplers(adjusted)?;
    adjusted = preserve_dont_unroll_annotations(adjusted)?;
    adjusted = add_modf_polyfill(adjusted)?;
    adjusted = lower_forward_buffer_booleans(adjusted);
    adjusted = adjust_builtin_shader_syntax(adjusted);
    adjusted = widen_forward_half_arguments(adjusted);
    adjusted = type_mobile_uint_returns(adjusted);
    adjusted = unpack_forward_packed_int3(adjusted);
    adjusted = strip_unused_ltc_helper(adjusted)?;
    let (source, combined_samplers) = split_combined_samplers(adjusted)?;
    adjusted = source;
    let (source, specialization_constants) = lower_specialization_constants(&adjusted)?;
    adjusted = add_inverse_polyfills(source)?;
    if stage != ShaderStage::Vertex {
        return Ok((adjusted, specialization_constants, combined_samplers));
    }

    let main_end = adjusted
        .rfind('}')
        .ok_or_else(|| "Godot vertex shader has no closing main brace".to_owned())?;
    adjusted.insert_str(main_end, "\n    gl_Position.y = -gl_Position.y;\n");
    Ok((adjusted, specialization_constants, combined_samplers))
}

fn godot_glslang_source(source: &str) -> Result<String, String> {
    let (source, _) = split_combined_samplers(strip_precision_qualifiers(source))?;
    add_inverse_polyfills(source)
}

fn lower_ir_overrides(module: &mut naga::Module) -> Result<Vec<SpecializationConstant>, String> {
    struct OverrideData {
        handle: naga::Handle<naga::Override>,
        name: String,
        id: u32,
        ty: naga::Handle<naga::Type>,
        init: naga::Handle<naga::Expression>,
        span: naga::Span,
    }

    let overrides = module
        .overrides
        .iter()
        .map(|(handle, value)| {
            let name = value
                .name
                .clone()
                .ok_or_else(|| "Naga specialization constant has no name".to_owned())?;
            let id = value
                .id
                .ok_or_else(|| format!("Naga specialization constant '{name}' has no ID"))?;
            let init = value.init.ok_or_else(|| {
                format!("Naga specialization constant '{name}' has no default value")
            })?;
            Ok(OverrideData {
                handle,
                name,
                id: id.into(),
                ty: value.ty,
                init,
                span: module.overrides.get_span(handle),
            })
        })
        .collect::<Result<Vec<_>, String>>()?;
    if overrides.is_empty() {
        return Ok(Vec::new());
    }

    let mut replacements = Vec::with_capacity(overrides.len());
    let mut specialization_constants = Vec::with_capacity(overrides.len());
    for value in overrides {
        if value.handle.index() != replacements.len() {
            return Err("Naga specialization constant handles are not contiguous".to_owned());
        }
        let constant = module.constants.append(
            naga::Constant {
                name: Some(value.name.clone()),
                ty: value.ty,
                init: value.init,
            },
            value.span,
        );
        replacements.push(constant);
        specialization_constants.push(SpecializationConstant {
            id: value.id,
            name: value.name,
        });
    }

    let replace = |expressions: &mut naga::Arena<naga::Expression>| {
        for (_, expression) in expressions.iter_mut() {
            if let naga::Expression::Override(handle) = *expression {
                *expression = naga::Expression::Constant(replacements[handle.index()]);
            }
        }
    };
    replace(&mut module.global_expressions);
    for (_, function) in module.functions.iter_mut() {
        replace(&mut function.expressions);
    }
    for entry_point in &mut module.entry_points {
        replace(&mut entry_point.function.expressions);
    }
    module.overrides.clear();
    Ok(specialization_constants)
}

fn find_combined_samplers(module: &naga::Module) -> Vec<CombinedSampler> {
    module
        .global_variables
        .iter()
        .filter_map(|(_, variable)| {
            let binding = variable.binding.as_ref()?;
            if binding.binding < SYNTHETIC_SAMPLER_BINDING_OFFSET {
                return None;
            }
            let texture_binding = binding.binding - SYNTHETIC_SAMPLER_BINDING_OFFSET;
            let (image_dimension, image_arrayed) = module
                .global_variables
                .iter()
                .find_map(|(_, candidate)| {
                    let candidate_binding = candidate.binding.as_ref()?;
                    if candidate_binding.group != binding.group
                        || candidate_binding.binding != texture_binding
                    {
                        return None;
                    }
                    match module.types[candidate.ty].inner {
                        naga::TypeInner::Image { dim, arrayed, .. } => Some((dim, arrayed)),
                        _ => None,
                    }
                })
                // The original bridge supported only the 2D LTC samplers. Keep
                // that type as a fallback if SPIR-V removed an inactive texture.
                .unwrap_or((naga::ImageDimension::D2, false));
            Some(CombinedSampler {
                group: binding.group,
                binding: texture_binding,
                synthetic_sampler_binding: binding.binding,
                image_dimension,
                image_arrayed,
            })
        })
        .collect()
}

fn invalid_spirv_id_context(bytes: &[u8], id: u32) -> String {
    let words = bytes
        .chunks_exact(4)
        .map(|word| u32::from_le_bytes(word.try_into().unwrap()))
        .collect::<Vec<_>>();
    let mut context = Vec::new();
    let mut offset = 5;
    while offset < words.len() {
        let instruction = words[offset];
        let word_count = (instruction >> 16) as usize;
        if word_count == 0 || offset + word_count > words.len() {
            break;
        }
        let operands = &words[offset + 1..offset + word_count];
        if operands.contains(&id) {
            let opcode = instruction & 0xffff;
            let name = spirv::Op::from_u32(opcode)
                .map(|op| format!("{op:?}"))
                .unwrap_or_else(|| format!("Op({opcode})"));
            context.push(format!("word {offset}: {name} {operands:?}"));
        }
        offset += word_count;
    }
    if context.is_empty() {
        String::new()
    } else {
        format!("\nInstructions referencing %{id}:\n{}", context.join("\n"))
    }
}

fn combined_sampler_context(source: &str) -> String {
    let lines = source
        .lines()
        .enumerate()
        .filter(|(_, line)| line.contains("ltc_lut1") || line.contains("ltc_lut2"))
        .map(|(index, line)| format!("{}: {}", index + 1, line.trim()))
        .collect::<Vec<_>>();
    if lines.is_empty() {
        String::new()
    } else {
        format!("\nCombined sampler occurrences:\n{}", lines.join("\n"))
    }
}

fn restore_specialization_constants(
    mut source: String,
    constants: &[SpecializationConstant],
) -> Result<String, String> {
    for constant in constants {
        let Some(line) = source
            .lines()
            .find(|line| {
                if !line.starts_with("constant ") {
                    return false;
                }
                let Some((declaration, _)) = line
                    .strip_suffix(';')
                    .and_then(|line| line.split_once(" = "))
                else {
                    return false;
                };
                let Some(emitted_name) = declaration.split_whitespace().last() else {
                    return false;
                };
                emitted_name == constant.name
                    || emitted_name
                        .strip_prefix(&constant.name)
                        .is_some_and(|suffix| {
                            !suffix.is_empty()
                                && suffix
                                    .chars()
                                    .all(|character| character == '_' || character.is_ascii_digit())
                        })
            })
            .map(str::to_owned)
        else {
            // Naga omits constants that are unused by this entry point. Metal permits
            // the pipeline to provide only the function constants that remain active.
            continue;
        };
        let (declaration, default) = line
            .strip_suffix(';')
            .and_then(|line| line.split_once(" = "))
            .ok_or_else(|| {
                format!(
                    "Naga MSL specialization constant '{}' has an invalid declaration",
                    constant.name
                )
            })?;
        let (type_and_name, emitted_name) = declaration
            .strip_prefix("constant ")
            .and_then(|declaration| declaration.rsplit_once(' '))
            .ok_or_else(|| {
                format!(
                    "Naga MSL specialization constant '{}' has an invalid type",
                    constant.name
                )
            })?;
        let type_name = type_and_name.trim();
        let temporary = format!("{emitted_name}_tmp");
        let replacement = format!(
            "constant {type_name} {temporary} [[function_constant({})]];\n\
             constant {type_name} {emitted_name} = is_function_constant_defined({temporary}) ? {temporary} : {default};",
            constant.id
        );
        source = source.replacen(&line, &replacement, 1);
    }
    Ok(source)
}

fn restore_spirv_specialization_constants(
    words: &mut Vec<u32>,
    constants: &[SpecializationConstant],
) -> Result<(), String> {
    if constants.is_empty() {
        return Ok(());
    }

    let mut ids_by_name = BTreeMap::new();
    let mut offset = 5;
    while offset < words.len() {
        let instruction = words[offset];
        let word_count = (instruction >> 16) as usize;
        if word_count == 0 || offset + word_count > words.len() {
            return Err(
                "Naga generated malformed SPIR-V while restoring specialization constants"
                    .to_owned(),
            );
        }
        if instruction & 0xffff == spirv::Op::Name as u32 && word_count >= 3 {
            let mut name_bytes = words[offset + 2..offset + word_count]
                .iter()
                .flat_map(|word| word.to_le_bytes())
                .collect::<Vec<_>>();
            name_bytes.truncate(
                name_bytes
                    .iter()
                    .position(|byte| *byte == 0)
                    .unwrap_or(name_bytes.len()),
            );
            if let Ok(name) = String::from_utf8(name_bytes) {
                ids_by_name.insert(name, words[offset + 1]);
            }
        }
        offset += word_count;
    }

    let mut specialization_ids = BTreeMap::new();
    for constant in constants {
        let result_id = ids_by_name.get(&constant.name).ok_or_else(|| {
            format!(
                "Naga SPIR-V omitted specialization constant '{}'",
                constant.name
            )
        })?;
        specialization_ids.insert(*result_id, constant.id);
    }

    let mut restored = BTreeMap::new();
    offset = 5;
    while offset < words.len() {
        let instruction = words[offset];
        let word_count = (instruction >> 16) as usize;
        let opcode = instruction & 0xffff;
        if word_count >= 3 {
            let result_id = words[offset + 2];
            if let Some(constant_id) = specialization_ids.get(&result_id) {
                let replacement = if opcode == spirv::Op::ConstantTrue as u32 {
                    spirv::Op::SpecConstantTrue
                } else if opcode == spirv::Op::ConstantFalse as u32 {
                    spirv::Op::SpecConstantFalse
                } else if opcode == spirv::Op::Constant as u32 {
                    spirv::Op::SpecConstant
                } else {
                    return Err(format!(
                        "Naga SPIR-V specialization constant %{result_id} uses unsupported opcode {opcode}"
                    ));
                };
                words[offset] = (instruction & 0xffff_0000) | replacement as u32;
                restored.insert(result_id, *constant_id);
            }
        }
        offset += word_count;
    }

    if restored.len() != specialization_ids.len() {
        return Err(
            "Naga omitted the value of an active SPIR-V specialization constant".to_owned(),
        );
    }

    let type_start = {
        let mut offset = 5;
        loop {
            if offset >= words.len() {
                return Err("Naga SPIR-V has no type section".to_owned());
            }
            let instruction = words[offset];
            let opcode = instruction & 0xffff;
            if (spirv::Op::TypeVoid as u32..=spirv::Op::TypeForwardPointer as u32).contains(&opcode)
            {
                break offset;
            }
            offset += (instruction >> 16) as usize;
        }
    };
    let mut decorations = Vec::with_capacity(restored.len() * 4);
    for (result_id, constant_id) in restored {
        decorations.extend_from_slice(&[
            (4 << 16) | spirv::Op::Decorate as u32,
            result_id,
            spirv::Decoration::SpecId as u32,
            constant_id,
        ]);
    }
    words.splice(type_start..type_start, decorations);
    Ok(())
}

#[repr(C)]
pub struct GodotNagaBinding {
    group: u32,
    binding: u32,
    buffer: i32,
    texture: i32,
    sampler: i32,
    writable: u8,
}

#[repr(C)]
pub struct GodotNagaBytes {
    data: *mut u8,
    length: usize,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GodotNagaUniformReflection {
    group: u32,
    binding: u32,
    kind: u32,
    length: u32,
    writable: u32,
    active: u32,
    image_dimension: u32,
    image_format: u32,
    image_arrayed: u32,
    image_multisampled: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct GodotNagaSpecializationReflection {
    kind: u32,
    constant_id: u32,
    default_value: u32,
}

#[repr(C)]
pub struct GodotNagaReflection {
    stage: u32,
    vertex_input_mask: u64,
    fragment_output_mask: u32,
    push_constant_size: u32,
    has_multiview: u32,
    compute_local_size: [u32; 3],
    uniforms: *mut GodotNagaUniformReflection,
    uniform_count: usize,
    specialization_constants: *mut GodotNagaSpecializationReflection,
    specialization_constant_count: usize,
}

impl Default for GodotNagaReflection {
    fn default() -> Self {
        Self {
            stage: u32::MAX,
            vertex_input_mask: 0,
            fragment_output_mask: 0,
            push_constant_size: 0,
            has_multiview: 0,
            compute_local_size: [0; 3],
            uniforms: ptr::null_mut(),
            uniform_count: 0,
            specialization_constants: ptr::null_mut(),
            specialization_constant_count: 0,
        }
    }
}

const REFLECTION_UNIFORM_SAMPLER: u32 = 0;
const REFLECTION_UNIFORM_TEXTURE: u32 = 1;
const REFLECTION_UNIFORM_IMAGE: u32 = 2;
const REFLECTION_UNIFORM_UNIFORM_BUFFER: u32 = 3;
const REFLECTION_UNIFORM_STORAGE_BUFFER: u32 = 4;

const REFLECTION_IMAGE_DIMENSION_NONE: u32 = 0;
const REFLECTION_IMAGE_DIMENSION_1D: u32 = 1;
const REFLECTION_IMAGE_DIMENSION_2D: u32 = 2;
const REFLECTION_IMAGE_DIMENSION_3D: u32 = 3;
const REFLECTION_IMAGE_DIMENSION_CUBE: u32 = 4;

const REFLECTION_SPECIALIZATION_BOOL: u32 = 0;
const REFLECTION_SPECIALIZATION_INT: u32 = 1;
const REFLECTION_SPECIALIZATION_FLOAT: u32 = 2;

fn reflection_array_size(size: naga::ArraySize) -> Result<u32, String> {
    match size {
        naga::ArraySize::Constant(size) => Ok(size.get()),
        naga::ArraySize::Pending(_) => {
            Err("Naga reflection does not support override-sized resource arrays".to_owned())
        }
        naga::ArraySize::Dynamic => {
            Err("Naga reflection does not support runtime-sized resource arrays".to_owned())
        }
    }
}

fn reflection_resource_type(
    module: &naga::Module,
    ty: naga::Handle<naga::Type>,
) -> Result<(naga::Handle<naga::Type>, u32), String> {
    match module.types[ty].inner {
        naga::TypeInner::BindingArray { base, size } => Ok((base, reflection_array_size(size)?)),
        _ => Ok((ty, 1)),
    }
}

fn reflection_image_dimension(dim: naga::ImageDimension) -> u32 {
    match dim {
        naga::ImageDimension::D1 => REFLECTION_IMAGE_DIMENSION_1D,
        naga::ImageDimension::D2 => REFLECTION_IMAGE_DIMENSION_2D,
        naga::ImageDimension::D3 => REFLECTION_IMAGE_DIMENSION_3D,
        naga::ImageDimension::Cube => REFLECTION_IMAGE_DIMENSION_CUBE,
    }
}

// These values are the C ABI shared with NagaShaderModule::ReflectionImageFormat.
fn reflection_storage_format(format: naga::StorageFormat) -> u32 {
    match format {
        naga::StorageFormat::R8Unorm => 1,
        naga::StorageFormat::R8Snorm => 2,
        naga::StorageFormat::R8Uint => 3,
        naga::StorageFormat::R8Sint => 4,
        naga::StorageFormat::R16Uint => 5,
        naga::StorageFormat::R16Sint => 6,
        naga::StorageFormat::R16Float => 7,
        naga::StorageFormat::Rg8Unorm => 8,
        naga::StorageFormat::Rg8Snorm => 9,
        naga::StorageFormat::Rg8Uint => 10,
        naga::StorageFormat::Rg8Sint => 11,
        naga::StorageFormat::R32Uint => 12,
        naga::StorageFormat::R32Sint => 13,
        naga::StorageFormat::R32Float => 14,
        naga::StorageFormat::Rg16Uint => 15,
        naga::StorageFormat::Rg16Sint => 16,
        naga::StorageFormat::Rg16Float => 17,
        naga::StorageFormat::Rgba8Unorm => 18,
        naga::StorageFormat::Rgba8Snorm => 19,
        naga::StorageFormat::Rgba8Uint => 20,
        naga::StorageFormat::Rgba8Sint => 21,
        naga::StorageFormat::Bgra8Unorm => 22,
        naga::StorageFormat::Rgb10a2Uint => 23,
        naga::StorageFormat::Rgb10a2Unorm => 24,
        naga::StorageFormat::Rg11b10Ufloat => 25,
        naga::StorageFormat::R64Uint => 26,
        naga::StorageFormat::Rg32Uint => 27,
        naga::StorageFormat::Rg32Sint => 28,
        naga::StorageFormat::Rg32Float => 29,
        naga::StorageFormat::Rgba16Uint => 30,
        naga::StorageFormat::Rgba16Sint => 31,
        naga::StorageFormat::Rgba16Float => 32,
        naga::StorageFormat::Rgba32Uint => 33,
        naga::StorageFormat::Rgba32Sint => 34,
        naga::StorageFormat::Rgba32Float => 35,
        naga::StorageFormat::R16Unorm => 36,
        naga::StorageFormat::R16Snorm => 37,
        naga::StorageFormat::Rg16Unorm => 38,
        naga::StorageFormat::Rg16Snorm => 39,
        naga::StorageFormat::Rgba16Unorm => 40,
        naga::StorageFormat::Rgba16Snorm => 41,
    }
}

fn visit_io_bindings(
    module: &naga::Module,
    ty: naga::Handle<naga::Type>,
    binding: Option<&naga::Binding>,
    visitor: &mut impl FnMut(&naga::Binding),
) {
    if let Some(binding) = binding {
        visitor(binding);
        return;
    }
    if let naga::TypeInner::Struct { ref members, .. } = module.types[ty].inner {
        for member in members {
            visit_io_bindings(module, member.ty, member.binding.as_ref(), visitor);
        }
    }
}

fn specialization_default(
    module: &naga::Module,
    constant: &SpecializationConstant,
) -> Result<GodotNagaSpecializationReflection, String> {
    let value = module
        .constants
        .iter()
        .find_map(|(_, value)| (value.name.as_deref() == Some(&constant.name)).then_some(value))
        .ok_or_else(|| {
            format!(
                "Naga reflection could not find specialization constant '{}'",
                constant.name
            )
        })?;
    let scalar = match module.types[value.ty].inner {
        naga::TypeInner::Scalar(scalar) => scalar,
        _ => {
            return Err(format!(
                "Naga specialization constant '{}' is not scalar",
                constant.name
            ))
        }
    };
    if scalar.width != 4 && scalar.kind != naga::ScalarKind::Bool {
        return Err(format!(
            "Naga specialization constant '{}' is not 32-bit",
            constant.name
        ));
    }
    let (kind, default_value) = match module.global_expressions[value.init] {
        naga::Expression::Literal(naga::Literal::Bool(value)) => {
            (REFLECTION_SPECIALIZATION_BOOL, u32::from(value))
        }
        naga::Expression::Literal(naga::Literal::I32(value)) => {
            (REFLECTION_SPECIALIZATION_INT, value as u32)
        }
        naga::Expression::Literal(naga::Literal::U32(value)) => {
            (REFLECTION_SPECIALIZATION_INT, value)
        }
        naga::Expression::Literal(naga::Literal::F32(value)) => {
            (REFLECTION_SPECIALIZATION_FLOAT, value.to_bits())
        }
        naga::Expression::ZeroValue(_) => match scalar.kind {
            naga::ScalarKind::Bool => (REFLECTION_SPECIALIZATION_BOOL, 0),
            naga::ScalarKind::Sint | naga::ScalarKind::Uint => (REFLECTION_SPECIALIZATION_INT, 0),
            naga::ScalarKind::Float => (REFLECTION_SPECIALIZATION_FLOAT, 0),
            _ => {
                return Err(format!(
                    "Naga specialization constant '{}' has an unsupported scalar kind",
                    constant.name
                ))
            }
        },
        _ => {
            return Err(format!(
                "Naga specialization constant '{}' has a non-literal default",
                constant.name
            ))
        }
    };
    Ok(GodotNagaSpecializationReflection {
        kind,
        constant_id: constant.id,
        default_value,
    })
}

fn reflect_shader(shader: &ParsedShader) -> Result<GodotNagaReflection, String> {
    let module = &shader.module;
    let (entry_point_index, entry_point) = module
        .entry_points
        .iter()
        .enumerate()
        .find(|(_, entry)| entry.stage == shader.stage && entry.name == "main")
        .ok_or_else(|| "Naga reflection could not find the main entry point".to_owned())?;
    let entry_point_info = shader.info.get_entry_point(entry_point_index);
    let mut layouter = naga::proc::Layouter::default();
    layouter
        .update(naga::proc::GlobalCtx {
            types: &module.types,
            constants: &module.constants,
            overrides: &module.overrides,
            global_expressions: &module.global_expressions,
        })
        .map_err(|err| format!("Naga reflection layout failed: {err}"))?;

    let mut uniforms = Vec::new();
    let mut push_constant_size = 0;
    for (handle, variable) in module.global_variables.iter() {
        if variable.space == naga::AddressSpace::Immediate {
            push_constant_size = layouter[variable.ty].size;
        }
        let Some(binding) = variable.binding else {
            continue;
        };
        let (resource_ty, resource_count) = reflection_resource_type(module, variable.ty)?;
        let resource_inner = &module.types[resource_ty].inner;
        let mut uniform = GodotNagaUniformReflection {
            group: binding.group,
            binding: binding.binding,
            kind: u32::MAX,
            length: 0,
            writable: 0,
            active: u32::from(!entry_point_info[handle].is_empty()),
            image_dimension: REFLECTION_IMAGE_DIMENSION_NONE,
            image_format: 0,
            image_arrayed: 0,
            image_multisampled: 0,
        };
        match variable.space {
            naga::AddressSpace::Uniform => {
                uniform.kind = REFLECTION_UNIFORM_UNIFORM_BUFFER;
                uniform.length = layouter[resource_ty]
                    .size
                    .checked_mul(resource_count)
                    .ok_or_else(|| "Naga uniform-buffer reflection size overflow".to_owned())?;
            }
            naga::AddressSpace::Storage { access } => {
                uniform.kind = REFLECTION_UNIFORM_STORAGE_BUFFER;
                // Godot's storage-buffer layouts use zero here; their runtime size
                // is supplied by the bound buffer rather than the shader reflection.
                uniform.length = 0;
                uniform.writable = u32::from(
                    access.intersects(naga::StorageAccess::STORE | naga::StorageAccess::ATOMIC),
                );
            }
            naga::AddressSpace::Handle => match *resource_inner {
                naga::TypeInner::Sampler { .. } => {
                    uniform.kind = REFLECTION_UNIFORM_SAMPLER;
                    uniform.length = resource_count;
                }
                naga::TypeInner::Image {
                    dim,
                    arrayed,
                    class,
                } => {
                    uniform.kind = match class {
                        naga::ImageClass::Storage { format, access } => {
                            uniform.image_format = reflection_storage_format(format);
                            uniform.writable = u32::from(access.intersects(
                                naga::StorageAccess::STORE | naga::StorageAccess::ATOMIC,
                            ));
                            REFLECTION_UNIFORM_IMAGE
                        }
                        naga::ImageClass::External => {
                            return Err(
                                "Naga reflection does not support external textures".to_owned()
                            )
                        }
                        _ => REFLECTION_UNIFORM_TEXTURE,
                    };
                    uniform.length = resource_count;
                    uniform.image_dimension = reflection_image_dimension(dim);
                    uniform.image_arrayed = u32::from(arrayed);
                    uniform.image_multisampled = u32::from(match class {
                        naga::ImageClass::Sampled { multi, .. }
                        | naga::ImageClass::Depth { multi } => multi,
                        _ => false,
                    });
                }
                _ => {
                    return Err(format!(
                        "Naga reflection does not support handle resource type {resource_inner:?}"
                    ))
                }
            },
            _ => {
                return Err(format!(
                    "Naga reflection does not support bound address space {:?}",
                    variable.space
                ))
            }
        }
        uniforms.push(uniform);
    }
    // Naga can remove either half of an inactive combined GLSL sampler. It also
    // removes the sampler half from texelFetch-only code. Preserve the declared
    // descriptor layout so Godot can merge both resources into one binding.
    for combined in &shader.combined_samplers {
        if !uniforms
            .iter()
            .any(|uniform| uniform.group == combined.group && uniform.binding == combined.binding)
        {
            uniforms.push(GodotNagaUniformReflection {
                group: combined.group,
                binding: combined.binding,
                kind: REFLECTION_UNIFORM_TEXTURE,
                length: 1,
                writable: 0,
                active: 0,
                image_dimension: reflection_image_dimension(combined.image_dimension),
                image_format: 0,
                image_arrayed: u32::from(combined.image_arrayed),
                image_multisampled: 0,
            });
        }
        if !uniforms.iter().any(|uniform| {
            uniform.group == combined.group && uniform.binding == combined.synthetic_sampler_binding
        }) {
            uniforms.push(GodotNagaUniformReflection {
                group: combined.group,
                binding: combined.synthetic_sampler_binding,
                kind: REFLECTION_UNIFORM_SAMPLER,
                length: 1,
                writable: 0,
                active: 0,
                image_dimension: REFLECTION_IMAGE_DIMENSION_NONE,
                image_format: 0,
                image_arrayed: 0,
                image_multisampled: 0,
            });
        }
    }
    uniforms.sort_by_key(|uniform| (uniform.group, uniform.binding));

    let mut vertex_input_mask = 0;
    let mut fragment_output_mask = 0;
    let mut has_multiview = false;
    let mut inspect_input = |binding: &naga::Binding| match *binding {
        naga::Binding::Location { location, .. } if shader.stage == ShaderStage::Vertex => {
            vertex_input_mask |= 1u64 << location;
        }
        naga::Binding::BuiltIn(naga::BuiltIn::ViewIndex) => has_multiview = true,
        _ => {}
    };
    for argument in &entry_point.function.arguments {
        visit_io_bindings(
            module,
            argument.ty,
            argument.binding.as_ref(),
            &mut inspect_input,
        );
    }
    if let Some(ref result) = entry_point.function.result {
        visit_io_bindings(
            module,
            result.ty,
            result.binding.as_ref(),
            &mut |binding| match *binding {
                naga::Binding::Location { location, .. }
                    if shader.stage == ShaderStage::Fragment =>
                {
                    fragment_output_mask |= 1 << location;
                }
                naga::Binding::BuiltIn(naga::BuiltIn::ViewIndex) => has_multiview = true,
                _ => {}
            },
        );
    }

    let specialization_constants = shader
        .specialization_constants
        .iter()
        .map(|constant| specialization_default(module, constant))
        .collect::<Result<Vec<_>, _>>()?;
    let mut uniform_slice = uniforms.into_boxed_slice();
    let mut specialization_slice = specialization_constants.into_boxed_slice();
    let reflection = GodotNagaReflection {
        stage: match shader.stage {
            ShaderStage::Vertex => 0,
            ShaderStage::Fragment => 1,
            ShaderStage::Compute => 4,
            _ => {
                return Err(format!(
                    "Naga reflection does not support stage {:?}",
                    shader.stage
                ))
            }
        },
        vertex_input_mask,
        fragment_output_mask,
        push_constant_size,
        has_multiview: u32::from(has_multiview),
        compute_local_size: entry_point.workgroup_size,
        uniforms: uniform_slice.as_mut_ptr(),
        uniform_count: uniform_slice.len(),
        specialization_constants: specialization_slice.as_mut_ptr(),
        specialization_constant_count: specialization_slice.len(),
    };
    std::mem::forget(uniform_slice);
    std::mem::forget(specialization_slice);
    Ok(reflection)
}

fn stage_from_u32(stage: u32) -> Result<ShaderStage, String> {
    match stage {
        0 => Ok(ShaderStage::Vertex),
        1 => Ok(ShaderStage::Fragment),
        4 => Ok(ShaderStage::Compute),
        _ => Err(format!(
            "Naga bridge does not support Godot shader stage {stage}"
        )),
    }
}

unsafe fn set_string(output: *mut *mut c_char, value: String) {
    if !output.is_null() {
        let sanitized = value.replace('\0', "\\0");
        *output = CString::new(sanitized).unwrap().into_raw();
    }
}

fn ffi_error<T>(output: *mut *mut c_char, result: Result<T, String>) -> Option<T> {
    match result {
        Ok(value) => Some(value),
        Err(error) => {
            unsafe { set_string(output, error) };
            None
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_parse(
    stage: u32,
    source: *const c_char,
    error: *mut *mut c_char,
) -> *mut c_void {
    if source.is_null() {
        set_string(error, "Naga received a null GLSL source".to_owned());
        return ptr::null_mut();
    }

    let result = catch_unwind(AssertUnwindSafe(|| -> Result<ParsedShader, String> {
        let stage = stage_from_u32(stage)?;
        let source = CStr::from_ptr(source)
            .to_str()
            .map_err(|err| format!("GLSL source is not UTF-8: {err}"))?;
        let (source, specialization_constants, combined_samplers) = godot_source(stage, source)?;
        if let Ok(directory) = std::env::var("GODOT_NAGA_DUMP_GLSL_DIR") {
            std::fs::create_dir_all(&directory)
                .map_err(|err| format!("Could not create Naga GLSL dump directory: {err}"))?;
            let index = DUMP_INDEX.fetch_add(1, Ordering::Relaxed);
            std::fs::write(format!("{directory}/naga_{index}_{stage:?}.glsl"), &source)
                .map_err(|err| format!("Could not write Naga GLSL dump: {err}"))?;
        }
        let mut frontend = glsl::Frontend::default();
        let module = frontend
            .parse(&glsl::Options::from(stage), &source)
            .map_err(|errors| {
                format!(
                    "{}{}",
                    errors.emit_to_string(&source),
                    combined_sampler_context(&source)
                )
            })?;
        // The GLSL frontend drops constants that are unused by this entry point.
        // Keep only the declarations that survived so reflection and backend
        // restoration do not require an inactive constant in every shader stage.
        let specialization_constants = specialization_constants
            .into_iter()
            .filter(|constant| {
                module
                    .constants
                    .iter()
                    .any(|(_, value)| value.name.as_deref() == Some(&constant.name))
            })
            .collect();
        let info = Validator::new(ValidationFlags::all(), msl::supported_capabilities())
            .validate(&module)
            .map_err(|err| {
                let spans = err
                    .spans()
                    .filter_map(|(span, description)| {
                        span.to_range()
                            .map(|range| format!("{description}: {}", source[range].trim()))
                    })
                    .collect::<Vec<_>>();
                let context = if spans.is_empty() {
                    String::new()
                } else {
                    format!("\nSource spans:\n{}", spans.join("\n"))
                };
                format!("Naga validation failed: {err:#?}{context}")
            })?;
        Ok(ParsedShader {
            module,
            info,
            stage,
            specialization_constants,
            combined_samplers,
        })
    }));

    match result {
        Ok(result) => ffi_error(error, result)
            .map(|shader| Box::into_raw(Box::new(shader)).cast())
            .unwrap_or(ptr::null_mut()),
        Err(_) => {
            set_string(error, "Naga panicked while parsing GLSL".to_owned());
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_preprocess_for_glslang(
    source: *const c_char,
    error: *mut *mut c_char,
) -> *mut c_char {
    if source.is_null() {
        set_string(error, "Naga received a null GLSL source".to_owned());
        return ptr::null_mut();
    }
    let result = catch_unwind(AssertUnwindSafe(|| -> Result<String, String> {
        let source = CStr::from_ptr(source)
            .to_str()
            .map_err(|err| format!("GLSL source is not UTF-8: {err}"))?;
        godot_glslang_source(source)
    }));
    match result {
        Ok(result) => ffi_error(error, result)
            .map(|source| CString::new(source).unwrap().into_raw())
            .unwrap_or(ptr::null_mut()),
        Err(_) => {
            set_string(error, "Naga panicked while preprocessing GLSL".to_owned());
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_parse_spirv(
    stage: u32,
    spirv: *const u8,
    length: usize,
    error: *mut *mut c_char,
) -> *mut c_void {
    if spirv.is_null() || length == 0 {
        set_string(error, "Naga received empty SPIR-V".to_owned());
        return ptr::null_mut();
    }

    let result = catch_unwind(AssertUnwindSafe(|| -> Result<ParsedShader, String> {
        let stage = stage_from_u32(stage)?;
        let bytes = slice::from_raw_parts(spirv, length);
        let mut module =
            naga::front::spv::parse_u8_slice(bytes, &naga::front::spv::Options::default())
                .map_err(|err| {
                    let context = match err {
                        naga::front::spv::Error::InvalidId(id) => {
                            invalid_spirv_id_context(bytes, id)
                        }
                        _ => String::new(),
                    };
                    format!("Naga SPIR-V parsing failed: {err}{context}")
                })?;
        if !module
            .entry_points
            .iter()
            .any(|entry| entry.stage == stage && entry.name == "main")
        {
            return Err("SPIR-V does not contain the expected 'main' entry point".to_owned());
        }
        let specialization_constants = lower_ir_overrides(&mut module)?;
        let combined_samplers = find_combined_samplers(&module);
        let info = Validator::new(ValidationFlags::all(), msl::supported_capabilities())
            .validate(&module)
            .map_err(|err| format!("Naga validation failed: {err}"))?;
        Ok(ParsedShader {
            module,
            info,
            stage,
            specialization_constants,
            combined_samplers,
        })
    }));

    match result {
        Ok(result) => ffi_error(error, result)
            .map(|shader| Box::into_raw(Box::new(shader)).cast())
            .unwrap_or(ptr::null_mut()),
        Err(_) => {
            set_string(error, "Naga panicked while parsing SPIR-V".to_owned());
            ptr::null_mut()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_reflect(
    shader: *const c_void,
    reflection: *mut GodotNagaReflection,
    error: *mut *mut c_char,
) -> u8 {
    if shader.is_null() || reflection.is_null() {
        set_string(
            error,
            "Naga received invalid reflection arguments".to_owned(),
        );
        return 0;
    }
    *reflection = GodotNagaReflection::default();
    let shader = &*shader.cast::<ParsedShader>();
    match catch_unwind(AssertUnwindSafe(|| reflect_shader(shader))) {
        Ok(result) => match ffi_error(error, result) {
            Some(result) => {
                *reflection = result;
                1
            }
            None => 0,
        },
        Err(_) => {
            set_string(error, "Naga panicked while reflecting shader IR".to_owned());
            0
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_reflection_free(reflection: *mut GodotNagaReflection) {
    if reflection.is_null() {
        return;
    }
    let reflection = &mut *reflection;
    if !reflection.uniforms.is_null() {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            reflection.uniforms,
            reflection.uniform_count,
        )));
    }
    if !reflection.specialization_constants.is_null() {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(
            reflection.specialization_constants,
            reflection.specialization_constant_count,
        )));
    }
    *reflection = GodotNagaReflection::default();
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_write_spirv(
    shader: *const c_void,
    error: *mut *mut c_char,
) -> GodotNagaBytes {
    if shader.is_null() {
        set_string(error, "Naga received a null shader module".to_owned());
        return GodotNagaBytes {
            data: ptr::null_mut(),
            length: 0,
        };
    }
    let shader = &*shader.cast::<ParsedShader>();
    let result = catch_unwind(AssertUnwindSafe(|| {
        let mut options = spv::Options::default();
        options
            .flags
            .remove(spv::WriterFlags::ADJUST_COORDINATE_SPACE);
        options.flags.insert(spv::WriterFlags::DEBUG);
        // Reflection must retain the full declared resource layout. Godot binds
        // complete descriptor sets even when a particular entry point does not
        // access every declaration in the shared forward-shader template.
        let mut words = spv::write_vec(&shader.module, &shader.info, &options, None)
            .map_err(|err| format!("Naga SPIR-V generation failed: {err}"))?;
        restore_spirv_specialization_constants(&mut words, &shader.specialization_constants)?;
        Ok(words)
    }));

    let words = match result {
        Ok(result) => match ffi_error(error, result) {
            Some(words) => words,
            None => {
                return GodotNagaBytes {
                    data: ptr::null_mut(),
                    length: 0,
                }
            }
        },
        Err(_) => {
            set_string(error, "Naga panicked while generating SPIR-V".to_owned());
            return GodotNagaBytes {
                data: ptr::null_mut(),
                length: 0,
            };
        }
    };
    let mut bytes: Vec<u8> = words.into_iter().flat_map(u32::to_le_bytes).collect();
    let result = GodotNagaBytes {
        data: bytes.as_mut_ptr(),
        length: bytes.len(),
    };
    std::mem::forget(bytes);
    result
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_write_msl(
    shader: *const c_void,
    msl_major: u8,
    msl_minor: u8,
    bindings: *const GodotNagaBinding,
    binding_count: usize,
    push_constant_buffer: i32,
    entry_point: *mut *mut c_char,
    error: *mut *mut c_char,
) -> *mut c_char {
    if shader.is_null() || (bindings.is_null() && binding_count != 0) {
        set_string(
            error,
            "Naga received invalid MSL generation arguments".to_owned(),
        );
        return ptr::null_mut();
    }
    let shader = &*shader.cast::<ParsedShader>();
    let bindings = if binding_count == 0 {
        &[]
    } else {
        slice::from_raw_parts(bindings, binding_count)
    };
    let result = catch_unwind(AssertUnwindSafe(|| -> Result<(String, String), String> {
        let mut resources = msl::EntryPointResources::default();
        for binding in bindings {
            let slot = |value: i32, kind: &str| -> Result<Option<u8>, String> {
                if value < 0 {
                    Ok(None)
                } else {
                    u8::try_from(value).map(Some).map_err(|_| {
                        format!("Metal {kind} slot {value} exceeds Naga's limit of 255")
                    })
                }
            };
            let target = msl::BindTarget {
                buffer: slot(binding.buffer, "buffer")?,
                texture: slot(binding.texture, "texture")?,
                sampler: slot(binding.sampler, "sampler")?.map(msl::BindSamplerTarget::Resource),
                external_texture: None,
                mutable: binding.writable != 0,
            };
            resources.resources.insert(
                ResourceBinding {
                    group: binding.group,
                    binding: binding.binding,
                },
                target,
            );
        }
        for combined in &shader.combined_samplers {
            let original = ResourceBinding {
                group: combined.group,
                binding: combined.binding,
            };
            let Some(original_target) = resources.resources.get_mut(&original) else {
                // Reflection only contains resources active in the selected entry
                // point. The forward templates still declare LTC resources in depth
                // variants that never reference them.
                continue;
            };
            let sampler = original_target.sampler.take().ok_or_else(|| {
                format!(
                    "Metal binding map has no sampler slot for combined sampler {}:{}",
                    combined.group, combined.binding
                )
            })?;
            resources.resources.insert(
                ResourceBinding {
                    group: combined.group,
                    binding: combined.synthetic_sampler_binding,
                },
                msl::BindTarget {
                    sampler: Some(sampler),
                    ..Default::default()
                },
            );
        }
        if push_constant_buffer >= 0 {
            resources.immediates_buffer = Some(
                u8::try_from(push_constant_buffer)
                    .map_err(|_| format!("Metal push constant slot {push_constant_buffer} exceeds Naga's limit of 255"))?,
            );
        }
        // Match SPIRV-Cross' reserved Metal buffer slot. Naga requires this for
        // storage-buffer types with runtime-sized trailing arrays.
        resources.sizes_buffer = Some(25);

        let mut options = msl::Options {
            lang_version: (msl_major, msl_minor),
            fake_missing_bindings: false,
            per_entry_point_map: BTreeMap::from([("main".to_owned(), resources)]),
            ..Default::default()
        };
        options.spirv_cross_compatibility = true;
        let pipeline = msl::PipelineOptions {
            entry_point: Some((shader.stage, "main".to_owned())),
            ..Default::default()
        };
        let (source, info) = msl::write_string(&shader.module, &shader.info, &options, &pipeline)
            .map_err(|err| format!("Naga MSL generation failed: {err}"))?;
        let source = restore_specialization_constants(source, &shader.specialization_constants)?;
        // Metal has no portable spelling for GLSL's dont_unroll annotation.
        // The marker keeps Naga's GLSL parser happy and is removed here.
        let source = source.replace(&format!("{DONT_UNROLL_MARKER}();"), "");
        if let Ok(directory) = std::env::var("GODOT_NAGA_DUMP_MSL_DIR") {
            std::fs::create_dir_all(&directory)
                .map_err(|err| format!("Could not create Naga MSL dump directory: {err}"))?;
            let index = DUMP_INDEX.fetch_add(1, Ordering::Relaxed);
            std::fs::write(
                format!("{directory}/naga_{index}_{:?}.metal", shader.stage),
                &source,
            )
            .map_err(|err| format!("Could not write Naga MSL dump: {err}"))?;
        }
        let translated_entry = info
            .entry_point_names
            .into_iter()
            .next()
            .ok_or_else(|| "Naga emitted no Metal entry point".to_owned())?
            .map_err(|err| format!("Naga could not emit the Metal entry point: {err}"))?;
        Ok((source, translated_entry))
    }));

    let (source, translated_entry) = match result {
        Ok(result) => match ffi_error(error, result) {
            Some(value) => value,
            None => return ptr::null_mut(),
        },
        Err(_) => {
            set_string(error, "Naga panicked while generating MSL".to_owned());
            return ptr::null_mut();
        }
    };
    set_string(entry_point, translated_entry);
    CString::new(source.replace('\0', "\\0"))
        .unwrap()
        .into_raw()
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_module_free(shader: *mut c_void) {
    if !shader.is_null() {
        drop(Box::from_raw(shader.cast::<ParsedShader>()));
    }
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_bytes_free(bytes: GodotNagaBytes) {
    if !bytes.data.is_null() {
        drop(Vec::from_raw_parts(bytes.data, bytes.length, bytes.length));
    }
}

#[no_mangle]
pub unsafe extern "C" fn godot_naga_string_free(string: *mut c_char) {
    if !string.is_null() {
        drop(CString::from_raw(string));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const VERTEX: &str = r#"#version 450
layout(location = 0) in vec3 position;
layout(constant_id = 7) const bool flip_x = false;
void main() {
    mat3 transform = inverse(mat3(2.0));
    vec3 transformed = transform * position;
    gl_Position = vec4(flip_x ? -transformed.x : transformed.x, transformed.yz, 1.0);
}
"#;

    #[test]
    fn strips_only_standalone_precision_qualifiers() {
        assert_eq!(
            strip_precision_qualifiers("highp float albedo_highp = mediump_value;"),
            " float albedo_highp = mediump_value;"
        );
    }

    #[test]
    fn translates_godot_vertex_glsl_to_spirv_and_msl() {
        unsafe {
            let source = CString::new(VERTEX).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(0, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let spirv = godot_naga_write_spirv(shader, &mut error);
            assert!(spirv.length >= 20);
            assert_eq!(*(spirv.data.cast::<u32>()), 0x0723_0203);
            let words = slice::from_raw_parts(spirv.data.cast::<u32>(), spirv.length / 4);
            assert!(words.iter().enumerate().any(|(offset, instruction)| {
                instruction & 0xffff == spirv::Op::Decorate as u32
                    && words.get(offset + 2) == Some(&(spirv::Decoration::SpecId as u32))
                    && words.get(offset + 3) == Some(&7)
            }));
            assert!(words.iter().any(|instruction| {
                matches!(
                    spirv::Op::from_u32(instruction & 0xffff),
                    Some(spirv::Op::SpecConstantFalse)
                )
            }));
            godot_naga_bytes_free(spirv);

            let mut reflection = GodotNagaReflection::default();
            assert_eq!(godot_naga_reflect(shader, &mut reflection, &mut error), 1);
            assert_eq!(reflection.stage, 0);
            assert_eq!(reflection.vertex_input_mask, 1);
            assert_eq!(reflection.specialization_constant_count, 1);
            let specialization = *reflection.specialization_constants;
            assert_eq!(specialization.kind, REFLECTION_SPECIALIZATION_BOOL);
            assert_eq!(specialization.constant_id, 7);
            assert_eq!(specialization.default_value, 0);
            godot_naga_reflection_free(&mut reflection);

            let mut entry = ptr::null_mut();
            let msl =
                godot_naga_write_msl(shader, 2, 4, ptr::null(), 0, -1, &mut entry, &mut error);
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            assert!(source.contains("vertex"));
            assert!(source.contains("-"));
            assert!(source.contains("flip_x_tmp [[function_constant(7)]]"));
            assert!(source.contains("is_function_constant_defined(flip_x_tmp)"));
            assert!(!source.contains("metal::inverse("));
            assert!(!CStr::from_ptr(entry).to_bytes().is_empty());

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn restores_renamed_msl_specialization_constants() {
        let source = "constant uint pso_sc_packed_0_ = 0u;\n".to_owned();
        let constants = [SpecializationConstant {
            id: 7,
            name: "pso_sc_packed_0".to_owned(),
        }];
        let restored = restore_specialization_constants(source, &constants).unwrap();
        assert!(restored.contains("pso_sc_packed_0__tmp [[function_constant(7)]]"));
        assert!(restored.contains("constant uint pso_sc_packed_0_ = is_function_constant_defined"));
    }

    #[test]
    fn strips_only_an_unused_ltc_helper() {
        const UNUSED: &str = r#"#version 450
layout(location = 0) out vec4 color;
void ltc_evaluate_specular(sampler2D ltc_lut1, sampler2D ltc_lut2, out vec4 result) {
    result = texture(ltc_lut1, vec2(0.5)) + texture(ltc_lut2, vec2(0.5));
}
void main() {
    color = vec4(1.0);
}
"#;
        let stripped = strip_unused_ltc_helper(UNUSED.to_owned()).unwrap();
        assert!(!stripped.contains("ltc_evaluate_specular"));

        let used = UNUSED.replace(
            "color = vec4(1.0);",
            "ltc_evaluate_specular(ltc_lut1, ltc_lut2, color);",
        );
        assert_eq!(strip_unused_ltc_helper(used.clone()).unwrap(), used);

        unsafe {
            let source = CString::new(UNUSED).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn splits_forward_combined_samplers_and_uses_globals_in_ltc_helper() {
        const FRAGMENT: &str = r#"#version 450
layout(location = 0) out vec4 color;
layout(set = 0, binding = 18) uniform sampler2D ltc_lut1;
layout(set = 0, binding = 19) uniform texture2D unused_lut_texture;
layout(set = 0, binding = 1019) uniform sampler unused_lut_sampler;
void ltc_evaluate_specular(sampler2D ltc_lut1, out vec4 result) {
    result = texture(ltc_lut1, vec2(0.5));
}
void main() {
    ltc_evaluate_specular(ltc_lut1, color);
}
"#;
        unsafe {
            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let mut reflection = GodotNagaReflection::default();
            assert_eq!(godot_naga_reflect(shader, &mut reflection, &mut error), 1);
            let uniforms = slice::from_raw_parts(reflection.uniforms, reflection.uniform_count);
            assert!(uniforms.iter().any(|uniform| {
                uniform.group == 0
                    && uniform.binding == 18
                    && uniform.kind == REFLECTION_UNIFORM_TEXTURE
                    && uniform.active != 0
            }));
            assert!(uniforms.iter().any(|uniform| {
                uniform.group == 0
                    && uniform.binding == 1018
                    && uniform.kind == REFLECTION_UNIFORM_SAMPLER
                    && uniform.active != 0
            }));
            assert!(uniforms.iter().any(|uniform| {
                uniform.group == 0
                    && uniform.binding == 19
                    && uniform.kind == REFLECTION_UNIFORM_TEXTURE
                    && uniform.active == 0
            }));
            assert!(uniforms.iter().any(|uniform| {
                uniform.group == 0
                    && uniform.binding == 1019
                    && uniform.kind == REFLECTION_UNIFORM_SAMPLER
                    && uniform.active == 0
            }));
            godot_naga_reflection_free(&mut reflection);

            let binding = GodotNagaBinding {
                group: 0,
                binding: 18,
                buffer: -1,
                texture: 2,
                sampler: 3,
                writable: 0,
            };
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(shader, 2, 4, &binding, 1, -1, &mut entry, &mut error);
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            assert!(source.contains("[[texture(2)]]"));
            assert!(source.contains("[[sampler(3)]]"));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn splits_general_combined_sampler_dimensions() {
        const FRAGMENT: &str = r#"#version 450
layout(location = 0) out vec4 color;
layout(set = 0, binding = 0) uniform sampler2D source_color;
layout(set = 0, binding = 1) uniform sampler2DArray source_layers;
layout(set = 0, binding = 2) uniform samplerCube source_cube;
layout(set = 0, binding = 3) uniform sampler3D source_volume;
void main() {
    color = texelFetch(source_color, ivec2(0), 0);
    color += textureLod(source_layers, vec3(0.5, 0.5, 0.0), 0.0);
    color += texture(source_cube, vec3(0.0, 0.0, 1.0));
    color += textureLod(source_volume, vec3(0.5), 0.0);
}
"#;
        unsafe {
            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let mut reflection = GodotNagaReflection::default();
            assert_eq!(godot_naga_reflect(shader, &mut reflection, &mut error), 1);
            let uniforms = slice::from_raw_parts(reflection.uniforms, reflection.uniform_count);
            for binding in 0..4 {
                assert!(uniforms.iter().any(|uniform| {
                    uniform.group == 0
                        && uniform.binding == binding
                        && uniform.kind == REFLECTION_UNIFORM_TEXTURE
                }));
                assert!(uniforms.iter().any(|uniform| {
                    uniform.group == 0
                        && uniform.binding == binding + SYNTHETIC_SAMPLER_BINDING_OFFSET
                        && uniform.kind == REFLECTION_UNIFORM_SAMPLER
                }));
            }
            godot_naga_reflection_free(&mut reflection);

            let bindings = (0..4)
                .map(|binding| GodotNagaBinding {
                    group: 0,
                    binding,
                    buffer: -1,
                    texture: binding as i32,
                    sampler: binding as i32,
                    writable: 0,
                })
                .collect::<Vec<_>>();
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(
                shader,
                2,
                4,
                bindings.as_ptr(),
                bindings.len(),
                -1,
                &mut entry,
                &mut error,
            );
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            for binding in 0..4 {
                assert!(source.contains(&format!("[[texture({binding})]]")));
                if binding != 0 {
                    assert!(source.contains(&format!("[[sampler({binding})]]")));
                }
            }

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn translates_array_and_component_texture_gathers() {
        const FRAGMENT: &str = r#"#version 450
layout(location = 0) out vec4 color;
#define USE_ARRAY
#ifdef USE_ARRAY
layout(set = 0, binding = 0) uniform sampler2DArray source_layers;
#else
layout(set = 0, binding = 0) uniform sampler2D inactive_color;
#endif
layout(set = 0, binding = 1) uniform sampler2D source_color;
layout(set = 0, binding = 2) uniform sampler3D unused_volume;
void main() {
    color = textureGather(source_layers, vec3(0.5, 0.5, 0.0));
    color += textureGather(source_color, vec2(0.5), 2);
}
"#;
        unsafe {
            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let mut reflection = GodotNagaReflection::default();
            assert_eq!(godot_naga_reflect(shader, &mut reflection, &mut error), 1);
            let uniforms = slice::from_raw_parts(reflection.uniforms, reflection.uniform_count);
            assert!(uniforms.iter().any(|uniform| {
                uniform.group == 0
                    && uniform.binding == 2
                    && uniform.kind == REFLECTION_UNIFORM_TEXTURE
                    && uniform.image_dimension == REFLECTION_IMAGE_DIMENSION_3D
                    && uniform.active == 0
            }));
            assert!(uniforms.iter().any(|uniform| {
                uniform.group == 0
                    && uniform.binding == 1002
                    && uniform.kind == REFLECTION_UNIFORM_SAMPLER
                    && uniform.active == 0
            }));
            godot_naga_reflection_free(&mut reflection);

            let bindings = [
                GodotNagaBinding {
                    group: 0,
                    binding: 0,
                    buffer: -1,
                    texture: 0,
                    sampler: 0,
                    writable: 0,
                },
                GodotNagaBinding {
                    group: 0,
                    binding: 1,
                    buffer: -1,
                    texture: 1,
                    sampler: 1,
                    writable: 0,
                },
            ];
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(
                shader,
                2,
                4,
                bindings.as_ptr(),
                bindings.len(),
                -1,
                &mut entry,
                &mut error,
            );
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            assert_eq!(source.matches(".gather(").count(), 2);
            assert!(source.contains("metal::component::z"));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn prefers_exact_integer_builtin_overloads() {
        const COMPUTE: &str = r#"#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() {
    uint value = min(gl_GlobalInvocationID.x, 5);
    value = max(1, value);
}
"#;
        unsafe {
            let source = CString::new(COMPUTE).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(4, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn preserves_compute_memory_barrier_scopes() {
        const COMPUTE: &str = r#"#version 450
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
void main() {
    memoryBarrierShared();
    groupMemoryBarrier();
}
"#;
        unsafe {
            let source = CString::new(COMPUTE).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(4, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let parsed = &*shader.cast::<ParsedShader>();
            let barriers = parsed
                .module
                .functions
                .iter()
                .flat_map(|(_, function)| function.body.iter())
                .chain(parsed.module.entry_points[0].function.body.iter())
                .filter_map(|statement| match statement {
                    naga::Statement::MemoryBarrier(flags) => Some(*flags),
                    _ => None,
                })
                .collect::<Vec<_>>();
            assert_eq!(
                barriers,
                vec![
                    naga::Barrier::WORK_GROUP,
                    naga::Barrier::STORAGE | naga::Barrier::WORK_GROUP | naga::Barrier::TEXTURE,
                ]
            );

            let mut entry = ptr::null_mut();
            let msl =
                godot_naga_write_msl(shader, 2, 4, ptr::null(), 0, -1, &mut entry, &mut error);
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            assert_eq!(source.matches("mem_device").count(), 1);
            assert_eq!(source.matches("mem_threadgroup").count(), 2);
            assert_eq!(source.matches("mem_texture").count(), 1);

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn marks_separate_shadow_samplers_as_comparison_samplers() {
        const FRAGMENT: &str = r#"#version 450
#define SPEC_CONSTANT_LOOP_ANNOTATION [[dont_unroll]]
#define SAMPLER_LINEAR_CLAMP linear_sampler
#define shadow_atlas directional_shadow_atlas
layout(set = 0, binding = 2) uniform sampler shadow_sampler;
layout(set = 0, binding = 3) uniform sampler linear_sampler;
layout(set = 1, binding = 5) uniform texture2D directional_shadow_atlas;
layout(set = 1, binding = 6) uniform texture2D lightmaps[2];
layout(location = 0) out float color;
float sample_directional_soft_shadow(texture2D shadow, vec2 suv) {
    float blocker = textureLod(sampler2D(shadow, SAMPLER_LINEAR_CLAMP), suv, 0.0).r;
    return blocker + textureProj(sampler2DShadow(shadow, shadow_sampler), vec4(suv, 0.5, 1.0));
}
void main() {
    color = 0.0;
    float whole;
    color += modf(0.5, whole) + whole;
    uint reduced = subgroupBroadcastFirst(subgroupMin(uint(1)));
    color += float(reduced);
    color += texture(sampler2D(lightmaps[reduced & 1u], linear_sampler), vec2(0.5)).r;
    SPEC_CONSTANT_LOOP_ANNOTATION
    for (int i = 0; i < 1; i++) {
        color += textureProj(sampler2DShadow(directional_shadow_atlas, shadow_sampler), vec4(0.5, 0.5, 0.25, 1.0));
    }
    color += textureLod(sampler2D(directional_shadow_atlas, linear_sampler), vec2(0.5), 0.0).r;
    color += sample_directional_soft_shadow(directional_shadow_atlas, vec2(0.5));
}
"#;
        unsafe {
            let adjusted = mark_comparison_samplers(inline_forward_depth_texture_parameter(
                FRAGMENT.to_owned(),
            ))
            .unwrap();
            assert!(
                !adjusted.contains("0.0).r"),
                "ordinary depth sample was not scalarized:\n{adjusted}"
            );
            assert!(adjusted.contains("_godot_naga_directional_depth_hint"));
            assert!(adjusted.contains("sampler2D(directional_shadow_atlas, SAMPLER_LINEAR_CLAMP)"));
            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let bindings = [
                GodotNagaBinding {
                    group: 0,
                    binding: 2,
                    buffer: -1,
                    texture: -1,
                    sampler: 0,
                    writable: 0,
                },
                GodotNagaBinding {
                    group: 0,
                    binding: 3,
                    buffer: -1,
                    texture: -1,
                    sampler: 1,
                    writable: 0,
                },
                GodotNagaBinding {
                    group: 1,
                    binding: 5,
                    buffer: -1,
                    texture: 0,
                    sampler: -1,
                    writable: 0,
                },
                GodotNagaBinding {
                    group: 1,
                    binding: 6,
                    buffer: -1,
                    texture: 1,
                    sampler: -1,
                    writable: 0,
                },
            ];
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(
                shader,
                2,
                4,
                bindings.as_ptr(),
                bindings.len(),
                -1,
                &mut entry,
                &mut error,
            );
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            assert!(source.contains("depth2d<float"));
            assert!(source.contains("sample_compare"));
            assert!(source.contains("metal::array<metal::texture2d<float"));
            assert!(source.contains("[[texture(1)]]"));
            assert!(!source.contains("[[dont_unroll]]"));
            assert!(!source.contains("_godot_naga_dont_unroll();"));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn preserves_forward_buffer_boolean_abi() {
        const FRAGMENT: &str = r#"#version 450
struct ReflectionData {
    float intensity;
    bool exterior;
    bool box_project;
};
layout(set = 0, binding = 6, std430) restrict readonly buffer ReflectionProbeData {
    ReflectionData data[];
} reflections;
layout(set = 1, binding = 0, std140) uniform MaterialUniforms {
    bool enabled;
    bool enabled_extra;
}
material;
layout(location = 0) out vec4 color;
void main() {
    uint ref_index = 0u;
    color = reflections.data[ref_index].box_project && material.enabled && material.enabled_extra ? vec4(1.0) : vec4(0.0);
}
"#;
        unsafe {
            let adjusted = lower_forward_buffer_booleans(FRAGMENT.to_owned());
            assert!(adjusted.contains("uint box_project;"));
            assert!(adjusted.contains("bool(reflections.data[ref_index].box_project)"));
            assert!(adjusted.contains("uint enabled;"));
            assert!(adjusted.contains("bool(material.enabled)"));
            assert!(adjusted.contains("uint enabled_extra;"));
            assert!(adjusted.contains("bool(material.enabled_extra)"));
            assert!(!adjusted.contains("bool(material.enabled)_extra"));

            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let bindings = [
                GodotNagaBinding {
                    group: 0,
                    binding: 6,
                    buffer: 0,
                    texture: -1,
                    sampler: -1,
                    writable: 0,
                },
                GodotNagaBinding {
                    group: 1,
                    binding: 0,
                    buffer: 1,
                    texture: -1,
                    sampler: -1,
                    writable: 0,
                },
            ];
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(
                shader,
                2,
                4,
                bindings.as_ptr(),
                bindings.len(),
                -1,
                &mut entry,
                &mut error,
            );
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            assert!(source.contains("uint box_project"));
            assert!(source.contains("uint enabled"));
            assert!(source.contains("uint enabled_extra"));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn translates_canvas_gather_and_sky_boolean_layout() {
        const FRAGMENT: &str = r#"#version 450
layout(set = 0, binding = 0) uniform texture2D atlas;
layout(set = 0, binding = 1) uniform sampler atlas_sampler;
layout(set = 0, binding = 2, std140) uniform SkyData {
    uint fog_enabled;
}
sky_scene_data;
struct DirectionalLightData {
    vec4 direction_energy;
    bool enabled;
};
layout(set = 0, binding = 3, std140) uniform DirectionalLights {
    DirectionalLightData data[4];
}
directional_lights;
layout(location = 0) out vec4 color;
void main() {
    vec4 taps = textureGather(sampler2D(atlas, atlas_sampler), vec2(0.5));
    color = sky_scene_data.fog_enabled && directional_lights.data[0].enabled ? taps : vec4(0.0);
}
"#;
        unsafe {
            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let bindings = [
                GodotNagaBinding {
                    group: 0,
                    binding: 0,
                    buffer: -1,
                    texture: 0,
                    sampler: -1,
                    writable: 0,
                },
                GodotNagaBinding {
                    group: 0,
                    binding: 1,
                    buffer: -1,
                    texture: -1,
                    sampler: 0,
                    writable: 0,
                },
                GodotNagaBinding {
                    group: 0,
                    binding: 2,
                    buffer: 0,
                    texture: -1,
                    sampler: -1,
                    writable: 0,
                },
                GodotNagaBinding {
                    group: 0,
                    binding: 3,
                    buffer: 1,
                    texture: -1,
                    sampler: -1,
                    writable: 0,
                },
            ];
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(
                shader,
                2,
                4,
                bindings.as_ptr(),
                bindings.len(),
                -1,
                &mut entry,
                &mut error,
            );
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            assert!(source.contains(".gather("));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn translates_particle_copy_buffer_syntax() {
        const COMPUTE: &str = r#"#version 450
layout(local_size_x = 1) in;
layout(set = 0, binding = 0, std430) buffer restrict writeonly OutputData {
    mat4 data[];
}
output_data;
layout(push_constant, std430) uniform Params {
    bool lifetime_reverse;
}
params;
void main() {
    mat4 txform = mat4(1.0);
    txform[0].xyz = vec3(2.0);
    txform[1].xyz *= 3.0;
    if (params.lifetime_reverse) {
        txform[3] = vec4(-1.0 / 0.0, -1.0 / 0.0, -1.0 / 0.0, 0.0);
    }
    output_data.data[0] = txform;
}
"#;
        let adjusted =
            adjust_builtin_shader_syntax(lower_forward_buffer_booleans(COMPUTE.to_owned()));
        assert!(!adjusted.contains("writeonly"));
        assert!(!adjusted.contains("txform[0].xyz ="));
        assert!(!adjusted.contains("txform[1].xyz *="));
        assert!(adjusted.contains("bool(params.lifetime_reverse)"));
        assert!(adjusted.contains("uintBitsToFloat(0xff800000u)"));

        unsafe {
            let source = CString::new(COMPUTE).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(4, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let binding = GodotNagaBinding {
                group: 0,
                binding: 0,
                buffer: 0,
                texture: -1,
                sampler: -1,
                writable: 1,
            };
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(shader, 2, 4, &binding, 1, 1, &mut entry, &mut error);
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn translates_mobile_fp16_math() {
        const FRAGMENT: &str = r#"#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
layout(location = 0) out vec4 color;
void main() {
    f16vec3 a = f16vec3(1.0, 0.0, 0.0);
    f16vec3 b = f16vec3(0.0, 1.0, 0.0);
    f16vec3 c = cross(a, b);
    float16_t amount = mix(float16_t(0.25), float16_t(0.75), float16_t(0.5));
    float16_t value = float16_t(min(abs(c.z), exp2(amount)));
    float16_t lod = float16_t(log(float16_t(2048.0) * amount) / log(float16_t(3.0)));
    color = vec4(vec3(c), float(value + lod));
}
"#;
        unsafe {
            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let mut entry = ptr::null_mut();
            let msl =
                godot_naga_write_msl(shader, 2, 4, ptr::null(), 0, -1, &mut entry, &mut error);
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            let source = CStr::from_ptr(msl).to_string_lossy();
            assert!(source.contains("metal::half3"));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn translates_multiview_builtin() {
        const VERTEX: &str = r#"#version 450
#extension GL_EXT_multiview : require
layout(location = 0) in vec3 position;
void main() {
    gl_Position = vec4(position + vec3(float(gl_ViewIndex)), 1.0);
}
"#;
        unsafe {
            let source = CString::new(VERTEX).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(0, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let mut entry = ptr::null_mut();
            let msl =
                godot_naga_write_msl(shader, 2, 4, ptr::null(), 0, -1, &mut entry, &mut error);
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            assert!(CStr::from_ptr(msl)
                .to_string_lossy()
                .contains("[[amplification_id]]"));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn translates_storage_image_atomic_or() {
        const FRAGMENT: &str = r#"#version 450
layout(r32ui, set = 0, binding = 0) uniform uimage3D grid;
void main() {
    imageAtomicOr(grid, ivec3(0), 1u);
}
"#;
        unsafe {
            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let mut reflection = GodotNagaReflection::default();
            assert_ne!(godot_naga_reflect(shader, &mut reflection, &mut error), 0);
            let uniforms = slice::from_raw_parts(reflection.uniforms, reflection.uniform_count);
            assert_eq!(uniforms.len(), 1);
            assert_eq!(uniforms[0].kind, REFLECTION_UNIFORM_IMAGE);
            assert_eq!(uniforms[0].image_format, 12);
            assert_eq!(uniforms[0].writable, 1);
            godot_naga_reflection_free(&mut reflection);

            let binding = GodotNagaBinding {
                group: 0,
                binding: 0,
                buffer: -1,
                texture: 0,
                sampler: -1,
                writable: 1,
            };
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(shader, 2, 4, &binding, 1, -1, &mut entry, &mut error);
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            assert!(CStr::from_ptr(msl).to_string_lossy().contains("fetch_or"));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }

    #[test]
    fn wraps_packed_vec3_before_signed_arithmetic_bitcast() {
        const FRAGMENT: &str = r#"#version 450
layout(std430, set = 0, binding = 0) readonly buffer Data {
    ivec3 offset;
    int next_value;
} data;
layout(location = 0) out ivec4 color;
void main() {
    color = ivec4(data.offset + ivec3(1), data.next_value);
}
"#;
        unsafe {
            let source = CString::new(FRAGMENT).unwrap();
            let mut error = ptr::null_mut();
            let shader = godot_naga_parse(1, source.as_ptr(), &mut error);
            assert!(
                !shader.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );

            let binding = GodotNagaBinding {
                group: 0,
                binding: 0,
                buffer: 0,
                texture: -1,
                sampler: -1,
                writable: 0,
            };
            let mut entry = ptr::null_mut();
            let msl = godot_naga_write_msl(shader, 2, 4, &binding, 1, -1, &mut entry, &mut error);
            assert!(
                !msl.is_null(),
                "{}",
                CStr::from_ptr(error).to_string_lossy()
            );
            assert!(CStr::from_ptr(msl)
                .to_string_lossy()
                .contains("as_type<metal::uint3>(metal::int3("));

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }
}
