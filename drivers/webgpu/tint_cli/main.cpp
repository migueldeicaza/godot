// tint_convert_cli — Standalone SPIR-V → WGSL converter for build-time precompilation.
//
// Runs the same 13 preprocessing passes as the Godot WebGPU runtime driver,
// then converts to WGSL via Tint. Produces output identical to what the engine
// generates at runtime, enabling precompilation of ubershader and specialized
// shader variants at build time.
//
// Usage:
//   tint_convert_cli <file.spv>                       # single file → WGSL to stdout
//   tint_convert_cli --batch <file1.spv> <file2.spv>  # batch → JSON to stdout

#include "../spirv_preprocess.h"
#include "../tint_wrapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

// Read a binary file into a byte vector.
static std::vector<uint8_t> read_file(const char *p_path) {
	std::ifstream f(p_path, std::ios::binary | std::ios::ate);
	if (!f.is_open()) {
		return {};
	}
	auto size = f.tellg();
	if (size <= 0) {
		return {};
	}
	std::vector<uint8_t> buf((size_t)size);
	f.seekg(0);
	f.read(reinterpret_cast<char *>(buf.data()), size);
	return buf;
}

// Run only the write-only storage buffer pass and return SPIR-V bytes.
// This mode supports byte-level preprocessing tests.
static Vector<uint8_t> preprocess_writeonly_storage_buffers(const std::vector<uint8_t> &p_spv_bytes) {
	Vector<uint8_t> spv;
	spv.resize((int64_t)p_spv_bytes.size());
	memcpy(spv.ptrw(), p_spv_bytes.data(), p_spv_bytes.size());
	return spirv_preprocess::promote_writeonly_storage_buffers(spv);
}


// Debug aid: when GODOT_WEBGPU_DUMP_PASSES names a directory, write the SPIR-V
// after every preprocessing pass as <dir>/NN_<pass>.spv. Run spirv-val over the
// results to find which pass first produces invalid SPIR-V. Off unless set.
static void dump_pass(const char *p_name, int p_index, const Vector<uint8_t> &p_spv) {
	const char *dir = getenv("GODOT_WEBGPU_DUMP_PASSES");
	if (dir == nullptr || *dir == '\0') {
		return;
	}
	char path[1024];
	snprintf(path, sizeof(path), "%s/%02d_%s.spv", dir, p_index, p_name);
	FILE *f = fopen(path, "wb");
	if (f == nullptr) {
		fprintf(stderr, "dump_pass: cannot open %s\n", path);
		return;
	}
	fwrite(p_spv.ptr(), 1, (size_t)p_spv.size(), f);
	fclose(f);
	fprintf(stderr, "dump_pass: wrote %s (%lld bytes)\n", path, (long long)p_spv.size());
}

// Debug aid: convert already-preprocessed SPIR-V straight through Tint with no
// preprocessing at all. Combined with GODOT_WEBGPU_DUMP_PASSES this identifies
// which pass first produces SPIR-V that Tint cannot model.
static std::string convert_spirv_to_wgsl_raw(const std::vector<uint8_t> &p_spv_bytes, std::string &r_error) {
	if (p_spv_bytes.size() < 20 || (p_spv_bytes.size() % 4) != 0) {
		r_error = "Invalid SPIR-V: too small or not aligned to 4 bytes";
		return {};
	}
	std::vector<uint32_t> words(p_spv_bytes.size() / 4);
	memcpy(words.data(), p_spv_bytes.data(), p_spv_bytes.size());

	char *error_msg = nullptr;
	char *wgsl = tint_wrapper_spirv_to_wgsl(words.data(), words.size(), &error_msg);
	if (!wgsl) {
		r_error = error_msg ? error_msg : "Tint conversion failed (unknown error)";
		free(error_msg);
		return {};
	}
	std::string result(wgsl);
	free(wgsl);
	return result;
}

// Run the full SPIR-V preprocessing pipeline + Tint conversion.
// Returns WGSL string on success, empty string on failure (error written to r_error).
static std::string convert_spirv_to_wgsl(const std::vector<uint8_t> &p_spv_bytes, std::string &r_error) {
	if (p_spv_bytes.size() < 20 || (p_spv_bytes.size() % 4) != 0) {
		r_error = "Invalid SPIR-V: too small or not aligned to 4 bytes";
		return {};
	}

	// Wrap in Godot-compatible Vector for the preprocessing API.
	Vector<uint8_t> spv;
	spv.resize((int64_t)p_spv_bytes.size());
	memcpy(spv.ptrw(), p_spv_bytes.data(), p_spv_bytes.size());

	// 13 preprocessing passes (same order as rendering_device_driver_webgpu.cpp).
	dump_pass("input", 0, spv);
	spv = spirv_preprocess::freeze_spec_constant_ops(spv);
	dump_pass("freeze_spec_constant_ops", 1, spv);
	spv = spirv_preprocess::rewrite_copy_logical(spv);
	dump_pass("rewrite_copy_logical", 2, spv);
	spv = spirv_preprocess::rewrite_terminate_invocation(spv);
	dump_pass("rewrite_terminate_invocation", 3, spv);
	spv = spirv_preprocess::convert_push_constants_to_uniforms(spv);
	dump_pass("convert_push_constants_to_uniforms", 4, spv);
	spv = spirv_preprocess::split_combined_samplers(spv);
	dump_pass("split_combined_samplers", 5, spv);
	auto depth_result = spirv_preprocess::fix_depth2_images(spv);
	spv = depth_result.bytes;
	dump_pass("fix_depth2_images", 6, spv);
	spv = spirv_preprocess::negate_position_y(spv);
	dump_pass("negate_position_y", 7, spv);
	spv = spirv_preprocess::strip_restrict_decoration(spv);
	dump_pass("strip_restrict_decoration", 8, spv);
	spv = spirv_preprocess::strip_memory_barrier(spv);
	dump_pass("strip_memory_barrier", 9, spv);
	spv = spirv_preprocess::fix_nonfinite_literals(spv);
	dump_pass("fix_nonfinite_literals", 10, spv);
	spv = spirv_preprocess::flatten_binding_arrays(spv);
	dump_pass("flatten_binding_arrays", 11, spv);
	spv = spirv_preprocess::promote_writeonly_storage_buffers(spv);
	dump_pass("promote_writeonly_storage_buffers", 12, spv);
	spv = spirv_preprocess::infer_readonly_storage(spv);
	dump_pass("infer_readonly_storage", 13, spv);

	// Ensure SPIR-V version is at least 1.3 (0x00010300). The preprocessing
	// passes produce constructs (StorageBuffer storage class) that require 1.3,
	// but input SPIR-V may declare an older version in its header.
	if (spv.size() >= 20) {
		uint32_t version;
		memcpy(&version, spv.ptr() + 4, 4);
		if (version < 0x00010300) {
			version = 0x00010300;
			memcpy(spv.ptrw() + 4, &version, 4);
		}
	}

	// Convert to uint32_t words for Tint.
	size_t word_count = (size_t)spv.size() / 4;
	const uint32_t *words = reinterpret_cast<const uint32_t *>(spv.ptr());

	char *error_msg = nullptr;
	char *wgsl = tint_wrapper_spirv_to_wgsl(words, word_count, &error_msg);
	if (!wgsl) {
		r_error = error_msg ? error_msg : "Tint conversion failed (unknown error)";
		free(error_msg);
		return {};
	}

	std::string result(wgsl);
	free(wgsl);
	return result;
}

// Escape a string for JSON output (handles \, ", newlines, tabs).
static std::string json_escape(const std::string &p_str) {
	std::string out;
	out.reserve(p_str.size() + p_str.size() / 8);
	for (char c : p_str) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += c; break;
		}
	}
	return out;
}

// Convert a single file in a forked child process. Tint can abort() on
// unhandled SPIR-V features (TINT_UNIMPLEMENTED); fork isolation prevents
// one bad shader from killing the entire batch.
//
// Returns WGSL on success, or sets r_error on failure.
static std::string convert_isolated(const std::vector<uint8_t> &p_spv_bytes, std::string &r_error) {
	// Create a pipe for the child to send results back.
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		// Fallback: convert in-process if pipe fails.
		return convert_spirv_to_wgsl(p_spv_bytes, r_error);
	}

	// Flush parent's stdout before forking so the child doesn't
	// inherit any buffered data.
	fflush(stdout);
	std::cout.flush();

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return convert_spirv_to_wgsl(p_spv_bytes, r_error);
	}

	if (pid == 0) {
		// Child process.
		close(pipefd[0]); // Close read end.

		// Redirect stdout/stderr to /dev/null so Tint crash messages and
		// C++ runtime flush on abort() don't corrupt the parent's JSON
		// output stream. Don't use fclose() — it flushes the parent's
		// buffered cout data (copied on fork), duplicating output.
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}

		std::string err;
		std::string wgsl = convert_spirv_to_wgsl(p_spv_bytes, err);

		// Protocol: first byte is status ('W' = wgsl, 'E' = error).
		if (!wgsl.empty()) {
			char status = 'W';
			write(pipefd[1], &status, 1);
			write(pipefd[1], wgsl.data(), wgsl.size());
		} else {
			char status = 'E';
			write(pipefd[1], &status, 1);
			write(pipefd[1], err.data(), err.size());
		}
		close(pipefd[1]);
		_exit(0);
	}

	// Parent process.
	close(pipefd[1]); // Close write end.

	// Read all data from child.
	std::string data;
	char buf[4096];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
		data.append(buf, (size_t)n);
	}
	close(pipefd[0]);

	int status;
	waitpid(pid, &status, 0);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || data.empty()) {
		r_error = "Tint crashed (likely TINT_UNIMPLEMENTED on unsupported SPIR-V feature)";
		return {};
	}

	if (data[0] == 'W') {
		return data.substr(1);
	} else {
		r_error = data.substr(1);
		return {};
	}
}

static void print_usage() {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "  tint_convert_cli <file.spv>                       Single file → WGSL to stdout\n");
	fprintf(stderr, "  tint_convert_cli --batch <file1.spv> [file2.spv]  Batch → JSON to stdout\n");
	fprintf(stderr, "  tint_convert_cli --promote-writeonly <file.spv>   Run one pass → SPIR-V to stdout\n");
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		print_usage();
		return 1;
	}

	bool batch_mode = (strcmp(argv[1], "--batch") == 0);
	bool promote_writeonly_mode = (strcmp(argv[1], "--promote-writeonly") == 0);
	bool raw_mode = (strcmp(argv[1], "--raw") == 0);

	if (promote_writeonly_mode) {
		if (argc != 3) {
			fprintf(stderr, "Error: --promote-writeonly requires one file argument.\n");
			return 1;
		}

		auto spv_bytes = read_file(argv[2]);
		if (spv_bytes.empty()) {
			fprintf(stderr, "Error: Failed to read '%s'\n", argv[2]);
			return 1;
		}

		Vector<uint8_t> result = preprocess_writeonly_storage_buffers(spv_bytes);
		std::cout.write(reinterpret_cast<const char *>(result.ptr()), result.size());
		return 0;
	}

	tint_wrapper_initialize();

	if (batch_mode) {
		if (argc < 3) {
			fprintf(stderr, "Error: --batch requires at least one file argument.\n");
			return 1;
		}

		// Batch mode: output JSON { "path": "wgsl" | {"error": "msg"}, ... }
		std::cout << "{" << std::endl;
		for (int i = 2; i < argc; i++) {
			const char *path = argv[i];
			auto spv_bytes = read_file(path);

			std::cout << "  \"" << json_escape(path) << "\": ";

			if (spv_bytes.empty()) {
				std::cout << "{\"error\": \"Failed to read file\"}";
			} else {
				std::string error;
				std::string wgsl = convert_isolated(spv_bytes, error);
				if (wgsl.empty()) {
					std::cout << "{\"error\": \"" << json_escape(error) << "\"}";
				} else {
					std::cout << "\"" << json_escape(wgsl) << "\"";
				}
			}

			if (i + 1 < argc) {
				std::cout << ",";
			}
			std::cout << std::endl;
		}
		std::cout << "}" << std::endl;
		return 0;

	} else if (raw_mode) {
		// Debug mode: skip all preprocessing, hand the SPIR-V straight to Tint.
		if (argc < 3) {
			fprintf(stderr, "Error: --raw requires a file argument.\n");
			return 1;
		}
		auto spv_bytes = read_file(argv[2]);
		if (spv_bytes.empty()) {
			fprintf(stderr, "Error: Failed to read '%s'\n", argv[2]);
			return 1;
		}
		std::string error;
		std::string wgsl = convert_spirv_to_wgsl_raw(spv_bytes, error);
		if (wgsl.empty()) {
			fprintf(stderr, "Error: %s\n", error.c_str());
			return 1;
		}
		std::cout << wgsl;
		return 0;

	} else {
		// Single file mode: output WGSL to stdout.
		const char *path = argv[1];
		auto spv_bytes = read_file(path);
		if (spv_bytes.empty()) {
			fprintf(stderr, "Error: Failed to read '%s'\n", path);
			return 1;
		}

		std::string error;
		std::string wgsl = convert_spirv_to_wgsl(spv_bytes, error);
		if (wgsl.empty()) {
			fprintf(stderr, "Error: %s\n", error.c_str());
			return 1;
		}

		std::cout << wgsl;
		return 0;
	}
}
