/*************************************************************************/
/*  gd_swift.cpp                                                         */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2020 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2020 Miguel de Icaza                                    */
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

#include "gd_swift.h"

#include "core/os/dir_access.h"
#include "core/os/file_access.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/project_settings.h"

#include "../swift_script.h"

_GodotSwift *_GodotSwift::singleton = NULL;
GDSwift *GDSwift::singleton = NULL;

GDSwift::GDSwift () 
{
    singleton = this;
    api_core_hash = 0;

#ifdef TOOLS_ENABLED
	api_editor_hash = 0;
#endif
}

GDSwift::~GDSwift()
{

}

void GDSwift::_init_godot_api_hashes() {
#if defined(SWIFT_GLUE_ENABLED) && defined(DEBUG_METHODS_ENABLED)
	if (get_api_core_hash() != GodotSwiftBindings::get_core_api_hash()) {
		ERR_PRINT("Swift: Core API hash mismatch.");
	}

#ifdef TOOLS_ENABLED
	if (get_api_editor_hash() != GodotSwiftBindings::get_editor_api_hash()) {
		ERR_PRINT("Swift: Editor API hash mismatch.");
	}
#endif // TOOLS_ENABLED
#endif // MONO_GLUE_ENABLED && DEBUG_METHODS_ENABLED
}

void GDSwift::initialize()
{
    _init_godot_api_hashes();
}