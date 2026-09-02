// Fuzz the complete SPIR-V -> WGSL conversion pipeline.
//
// Exercises all 12 SPIR-V preprocessing passes followed by Tint parsing,
// validation, and WGSL code generation.
//
// Tint can abort() on malformed SPIR-V (TINT_UNIMPLEMENTED, TINT_ICE).
// Fork isolation around the Tint call prevents one crash from killing the
// fuzzer — the same approach used by tint_convert_cli batch mode.
// Crashes in the preprocessing passes (our code) are NOT fork-isolated
// and will correctly terminate the fuzzer as bugs.

#include "../../spirv_preprocess.h"
#include "../../tint_wrapper.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static bool initialized = false;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	if (!initialized) {
		tint_wrapper_initialize();
		initialized = true;
	}

	// Skip inputs that are obviously not SPIR-V.
	if (size < 20 || (size % 4) != 0) {
		return 0;
	}

	Vector<uint8_t> spv;
	spv.resize((int64_t)size);
	memcpy(spv.ptrw(), data, size);

	// 11 preprocessing passes run in-process (crashes here ARE bugs).
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

	// Bump version to 1.3 (same as tint_convert_cli).
	if (spv.size() >= 20) {
		uint32_t version;
		memcpy(&version, spv.ptr() + 4, 4);
		if (version < 0x00010300) {
			version = 0x00010300;
			memcpy(spv.ptrw() + 4, &version, 4);
		}
	}

	// Tint conversion in a forked child — Tint may abort() on malformed SPIR-V.
	size_t word_count = (size_t)spv.size() / 4;
	const uint32_t *words = reinterpret_cast<const uint32_t *>(spv.ptr());

	pid_t pid = fork();
	if (pid == 0) {
		// Child: suppress stdout/stderr, try conversion, exit.
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		char *error_msg = nullptr;
		char *wgsl = tint_wrapper_spirv_to_wgsl(words, word_count, &error_msg);
		free(wgsl);
		free(error_msg);
		_exit(0);
	} else if (pid > 0) {
		int status;
		waitpid(pid, &status, 0);
		// Parent continues regardless — Tint crashes are expected on random data.
	}
	// If fork() failed, skip Tint conversion (preprocessing was still tested).

	return 0;
}
