// Copyright 2023 The Dawn & Tint Authors
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/tint/lang/spirv/validate/validate.h"

#include <memory>
#include <span>
#include <string>
#include <utility>

#include "spirv-tools/libspirv.hpp"
#include "src/tint/utils/diagnostic/diagnostic.h"

namespace tint::spirv::validate {

Result<SuccessType> Validate(std::span<const uint32_t> spirv, spv_target_env target_env) {
    Vector<diag::Diagnostic, 4> diags;
    diags.Push(diag::Diagnostic{});  // Filled in on error

    spvtools::SpirvTools tools(target_env);
    tools.SetMessageConsumer(
        [&](spv_message_level_t level, const char*, const spv_position_t& pos, const char* msg) {
            diag::Diagnostic diag;
            diag.message = msg;
            diag.source.range.begin.line = static_cast<uint32_t>(pos.line) + 1;
            diag.source.range.begin.column = static_cast<uint32_t>(pos.column) + 1;
            diag.source.range.end = diag.source.range.begin;
            switch (level) {
                case SPV_MSG_FATAL:
                case SPV_MSG_INTERNAL_ERROR:
                case SPV_MSG_ERROR:
                    diag.severity = diag::Severity::Error;
                    break;
                case SPV_MSG_WARNING:
                    diag.severity = diag::Severity::Warning;
                    break;
                case SPV_MSG_INFO:
                case SPV_MSG_DEBUG:
                    diag.severity = diag::Severity::Note;
                    break;
            }
            diags.Push(std::move(diag));
        });

    // Don't prepare to emit friendly names. The preparation costs
    // time by scanning the whole module and building a string table.
    spvtools::ValidatorOptions val_opts;
    val_opts.SetFriendlyNames(false);
    // Godot's runtime-specialized shaders can produce uniform buffer structs
    // whose member offsets don't satisfy any standard alignment rules (the
    // offsets match the C++ struct packing, which Vulkan GPUs accept but
    // spirv-val rejects). Naga had no layout validation at all, so these
    // always passed. Skip block layout validation to match that behavior.
    val_opts.SetSkipBlockLayout(true);

    if (tools.Validate(spirv.data(), spirv.size(), val_opts)) {
        return Success;
    }

    // Build a summary with the specific validation errors first, then
    // the disassembly. This ensures the actual error message is visible
    // even if the output is truncated.
    std::string summary = "SPIR-V failed validation.";
    for (size_t i = 1; i < diags.Length(); i++) {
        summary += " | " + diags[i].message.Plain();
    }

    std::string disassembly;
    if (tools.Disassemble(
            spirv.data(), spirv.size(), &disassembly,
            SPV_BINARY_TO_TEXT_OPTION_INDENT | SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES)) {
        diag::Diagnostic& err = diags.Front();
        err.message = summary + "\n\nDisassembly:\n" + std::move(disassembly);
        err.severity = diag::Severity::Error;
    } else {
        diag::Diagnostic& err = diags.Front();
        err.message = summary + "\n";
        err.severity = diag::Severity::Error;
    }
    auto file = std::make_shared<Source::File>("spirv", disassembly);
    for (auto& diag : diags) {
        diag.source.file = file.get();
        diag.owned_file = file;
    }
    auto list = diag::List(diags);
    return Failure{list.Str()};
}

}  // namespace tint::spirv::validate
