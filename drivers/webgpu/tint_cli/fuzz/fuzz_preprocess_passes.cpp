// Fuzz all 12 SPIR-V preprocessing passes in sequence.
//
// Exercises the passes without Tint parsing, catching crashes in the
// raw SPIR-V manipulation code (out-of-bounds, integer overflow, etc.).

#include "../../spirv_preprocess.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	Vector<uint8_t> spv;
	spv.resize((int64_t)size);
	if (size > 0) {
		memcpy(spv.ptrw(), data, size);
	}

	// Run each pass in the same order as tint_convert_cli/main.cpp.
	// Each pass must handle arbitrary byte sequences without crashing.
	spv = spirv_preprocess::freeze_spec_constant_ops(spv);
	spv = spirv_preprocess::rewrite_copy_logical(spv);
	spv = spirv_preprocess::rewrite_terminate_invocation(spv);
	spv = spirv_preprocess::convert_push_constants_to_uniforms(spv);
	spv = spirv_preprocess::split_combined_samplers(spv);
	auto depth_result = spirv_preprocess::fix_depth2_images(spv);
	spv = depth_result.bytes;
	spv = spirv_preprocess::negate_position_y(spv);
	spv = spirv_preprocess::strip_restrict_decoration(spv);
	spv = spirv_preprocess::strip_memory_barrier(spv);
	spv = spirv_preprocess::fix_nonfinite_literals(spv);
	spv = spirv_preprocess::flatten_binding_arrays(spv);
	spv = spirv_preprocess::infer_readonly_storage(spv);

	return 0;
}
