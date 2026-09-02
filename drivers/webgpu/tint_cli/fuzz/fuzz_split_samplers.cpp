// Fuzz split_combined_samplers in isolation.
//
// This is the most complex preprocessing pass (~500 lines) — it allocates
// new IDs, rewrites entry points, manipulates bindings, and injects new
// instructions. Focused fuzzing here has the highest chance of finding bugs.

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

	spirv_preprocess::split_combined_samplers(spv);
	return 0;
}
