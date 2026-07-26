use naga::{
    back::{msl, spv},
    front::glsl,
    valid::{ValidationFlags, Validator},
    ResourceBinding, ShaderStage,
};
use std::{
    collections::BTreeMap,
    ffi::{c_char, c_void, CStr, CString},
    panic::{catch_unwind, AssertUnwindSafe},
    ptr, slice,
};

struct ParsedShader {
    module: naga::Module,
    info: naga::valid::ModuleInfo,
    stage: ShaderStage,
}

fn godot_source(stage: ShaderStage, source: &str) -> Result<String, String> {
    let mut adjusted = source
        .replace("highp ", "")
        .replace("mediump ", "")
        .replace("lowp ", "");
    if stage != ShaderStage::Vertex {
        return Ok(adjusted);
    }

    let main_end = adjusted
        .rfind('}')
        .ok_or_else(|| "Godot vertex shader has no closing main brace".to_owned())?;
    adjusted.insert_str(main_end, "\n    gl_Position.y = -gl_Position.y;\n");
    Ok(adjusted)
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
        let source = godot_source(stage, source)?;
        let mut frontend = glsl::Frontend::default();
        let module = frontend
            .parse(&glsl::Options::from(stage), &source)
            .map_err(|errors| errors.emit_to_string(&source))?;
        let info = Validator::new(ValidationFlags::all(), msl::supported_capabilities())
            .validate(&module)
            .map_err(|err| format!("Naga validation failed: {err}"))?;
        Ok(ParsedShader {
            module,
            info,
            stage,
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
        let module = naga::front::spv::parse_u8_slice(bytes, &naga::front::spv::Options::default())
            .map_err(|err| format!("Naga SPIR-V parsing failed: {err}"))?;
        if !module
            .entry_points
            .iter()
            .any(|entry| entry.stage == stage && entry.name == "main")
        {
            return Err("SPIR-V does not contain the expected 'main' entry point".to_owned());
        }
        let info = Validator::new(ValidationFlags::all(), msl::supported_capabilities())
            .validate(&module)
            .map_err(|err| format!("Naga validation failed: {err}"))?;
        Ok(ParsedShader {
            module,
            info,
            stage,
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
        let pipeline = spv::PipelineOptions {
            shader_stage: shader.stage,
            entry_point: "main".to_owned(),
        };
        let mut options = spv::Options::default();
        options
            .flags
            .remove(spv::WriterFlags::ADJUST_COORDINATE_SPACE);
        spv::write_vec(&shader.module, &shader.info, &options, Some(&pipeline))
            .map_err(|err| format!("Naga SPIR-V generation failed: {err}"))
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
    let bindings = slice::from_raw_parts(bindings, binding_count);
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
        if push_constant_buffer >= 0 {
            resources.immediates_buffer = Some(
                u8::try_from(push_constant_buffer)
                    .map_err(|_| format!("Metal push constant slot {push_constant_buffer} exceeds Naga's limit of 255"))?,
            );
        }

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
void main() { gl_Position = vec4(position, 1.0); }
"#;

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
            godot_naga_bytes_free(spirv);

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
            assert!(!CStr::from_ptr(entry).to_bytes().is_empty());

            godot_naga_string_free(msl);
            godot_naga_string_free(entry);
            godot_naga_module_free(shader);
        }
    }
}
