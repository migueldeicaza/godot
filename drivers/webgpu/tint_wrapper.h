/**************************************************************************/
/*  tint_wrapper.h                                                        */
/**************************************************************************/
/*                       This file is part of:                            */
/*                           GODOT ENGINE                                 */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* Thin wrapper around Google Tint (C++20) so that Godot driver code      */
/* (compiled as C++17) never includes Tint headers directly.  This avoids */
/* namespace collisions (e.g. Godot's Span vs std::span) and C++20        */
/* feature requirements leaking into the main build.                      */
/**************************************************************************/

#ifndef TINT_WRAPPER_H
#define TINT_WRAPPER_H

#include <cstddef>
#include <cstdint>

// Initialize the Tint library. Call once at startup.
void tint_wrapper_initialize();

// Convert SPIR-V binary to WGSL text.
// On success: returns a malloc'd null-terminated string (caller must free).
// On failure: returns nullptr and writes an error message to *r_error
//             (also malloc'd, caller must free).
char *tint_wrapper_spirv_to_wgsl(const uint32_t *p_spirv_words, size_t p_word_count, char **r_error);

#endif // TINT_WRAPPER_H
