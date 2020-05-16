/*************************************************************************/
/*  gd_swift.h                                                           */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2020 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef GD_SWIFT_H
#define GD_SWIFT_H
#include "core/io/config_file.h"
#include "../godotswift_defs.h"

class GDSwift {
	uint64_t api_core_hash;
#ifdef TOOLS_ENABLED
	uint64_t api_editor_hash;
#endif

public:
#ifdef DEBUG_METHODS_ENABLED
	uint64_t get_api_core_hash() {
		if (api_core_hash == 0)
			api_core_hash = ClassDB::get_api_hash(ClassDB::API_CORE);
		return api_core_hash;
	}
#ifdef TOOLS_ENABLED
	uint64_t get_api_editor_hash() {
		if (api_editor_hash == 0)
			api_editor_hash = ClassDB::get_api_hash(ClassDB::API_EDITOR);
		return api_editor_hash;
	}
#endif // TOOLS_ENABLED
#endif
protected:
	static GDSwift *singleton;
public:
	static GDSwift *get_singleton() { return singleton; }

};

class _GodotSwift : public Object {
        GDCLASS(
            _GodotSwift, Object);

        friend class GDMono;
protected:
    static _GodotSwift *singleton;
public:
    static _GodotSwift *get_singleton() { return NULL; }
};
#endif
