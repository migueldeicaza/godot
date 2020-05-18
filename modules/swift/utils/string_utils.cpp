/*************************************************************************/
/*  string_utils.cpp                                                     */
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

#include "string_utils.h"

#include "core/os/file_access.h"

#include <stdio.h>
#include <stdlib.h>


namespace {

int ssfind(const String &p_text, int p_from) {
	if (p_from < 0)
		return -1;

	int src_len = 2;
	int len = p_text.length();

	if (len == 0)
		return -1;

	const CharType *src = p_text.c_str();

	for (int i = p_from; i <= (len - src_len); i++) {
		bool found = true;

		for (int j = 0; j < src_len; j++) {
			int read_pos = i + j;

			ERR_FAIL_COND_V(read_pos >= len, -1);

			switch (j) {
				case 0:
					found = src[read_pos] == '%';
					break;
				case 1: {
					CharType c = src[read_pos];
					found = src[read_pos] == 's' || (c >= '0' && c <= '4');
					break;
				}
				default:
					found = false;
			}

			if (!found) {
				break;
			}
		}

		if (found)
			return i;
	}

	return -1;
}
} // namespace

String ssformat(const String &p_text, const Variant &p1, const Variant &p2, const Variant &p3, const Variant &p4, const Variant &p5) {
	if (p_text.length() < 2)
		return p_text;

	Array args;

	if (p1.get_type() != Variant::NIL) {
		args.push_back(p1);

		if (p2.get_type() != Variant::NIL) {
			args.push_back(p2);

			if (p3.get_type() != Variant::NIL) {
				args.push_back(p3);

				if (p4.get_type() != Variant::NIL) {
					args.push_back(p4);

					if (p5.get_type() != Variant::NIL) {
						args.push_back(p5);
					}
				}
			}
		}
	}

	String new_string;

	int findex = 0;
	int search_from = 0;
	int result = 0;

	while ((result = ssfind(p_text, search_from)) >= 0) {
		CharType c = p_text[result + 1];

		int req_index = (c == 's' ? findex++ : c - '0');

		new_string += p_text.substr(search_from, result - search_from);
		new_string += args[req_index].operator String();
		search_from = result + 2;
	}

	new_string += p_text.substr(search_from, p_text.length() - search_from);

	return new_string;
}

#ifdef TOOLS_ENABLED
bool is_swift_keyword(const String &p_name) {

	// Reserved keywords

	return p_name == "Any" ||
		   p_name == "Protocol" ||
		   p_name == "Self" ||
		   p_name == "Type" ||
		   p_name == "_" ||
		   p_name == "as" ||
		   p_name == "associatedtype" ||
		   p_name == "associativity" ||
		   p_name == "break" ||
		   p_name == "case" ||
		   p_name == "catch" ||
		   p_name == "class" ||
		   p_name == "continue" ||
		   p_name == "convenience" ||
		   p_name == "default" ||
		   p_name == "defer" ||
		   p_name == "deinit" ||
		   p_name == "didSet" ||
		   p_name == "do" ||
		   p_name == "dynamic" ||
		   p_name == "else" ||
		   p_name == "enum" ||
		   p_name == "extension" ||
		   p_name == "fallthrough" ||
		   p_name == "false" ||
		   p_name == "fileprivate" ||
		   p_name == "final" ||
		   p_name == "for" ||
		   p_name == "func" ||
		   p_name == "get" ||
		   p_name == "guard" ||
		   p_name == "if" ||
		   p_name == "import" ||
		   p_name == "in" ||
		   p_name == "indirect" ||
		   p_name == "infix" ||
		   p_name == "init" ||
		   p_name == "inout" ||
		   p_name == "internal" ||
		   p_name == "is" ||
		   p_name == "lazy" ||
		   p_name == "left" ||
		   p_name == "let" ||
		   p_name == "mutating" ||
		   p_name == "nil" ||
		   p_name == "none" ||
		   p_name == "nonmutating" ||
		   p_name == "open" ||
		   p_name == "operator" ||
		   p_name == "optional" ||
		   p_name == "override" ||
		   p_name == "postfix" ||
		   p_name == "precedence" ||
		   p_name == "prefix" ||
		   p_name == "private" ||
		   p_name == "protocol" ||
		   p_name == "public" ||
		   p_name == "repeat" ||
		   p_name == "required" ||
		   p_name == "rethrows" ||
		   p_name == "return" ||
		   p_name == "right" ||
		   p_name == "self" ||
		   p_name == "set" ||
		   p_name == "static" ||
		   p_name == "struct" ||
		   p_name == "subscript" ||
		   p_name == "super" ||
		   p_name == "switch" ||
		   p_name == "throw" ||
		   p_name == "throws" ||
		   p_name == "true" ||
		   p_name == "try" ||
		   p_name == "typealias" ||
		   p_name == "unowned" ||
		   p_name == "var" ||
		   p_name == "weak" ||
		   p_name == "where" ||
		   p_name == "while";
}

String escape_swift_keyword(const String &p_name) {
	return is_swift_keyword(p_name) ? "`" + p_name + "`" : p_name;
}
#endif

Error sread_all_file_utf8(const String &p_path, String &r_content) {
	PoolVector<uint8_t> sourcef;
	Error err;
	FileAccess *f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, "Cannot open file '" + p_path + "'.");

	int len = f->get_len();
	sourcef.resize(len + 1);
	PoolVector<uint8_t>::Write w = sourcef.write();
	int r = f->get_buffer(w.ptr(), len);
	f->close();
	memdelete(f);
	ERR_FAIL_COND_V(r != len, ERR_CANT_OPEN);
	w[len] = 0;

	String source;
	if (source.parse_utf8((const char *)w.ptr())) {
		ERR_FAIL_V(ERR_INVALID_DATA);
	}

	r_content = source;
	return OK;
}

// TODO: Move to variadic templates once we upgrade to C++11

String sstr_format(const char *p_format, ...) {
	va_list list;

	va_start(list, p_format);
	String res = sstr_format(p_format, list);
	va_end(list);

	return res;
}
// va_copy was defined in the C99, but not in C++ standards before C++11.
// When you compile C++ without --std=c++<XX> option, compilers still define
// va_copy, otherwise you have to use the internal version (__va_copy).
#if !defined(va_copy)
#if defined(__GNUC__)
#define va_copy(d, s) __va_copy((d), (s))
#else
#define va_copy(d, s) ((d) = (s))
#endif
#endif

#if defined(MINGW_ENABLED) || defined(_MSC_VER) && _MSC_VER < 1900
#define gd_vsnprintf(m_buffer, m_count, m_format, m_args_copy) vsnprintf_s(m_buffer, m_count, _TRUNCATE, m_format, m_args_copy)
#define gd_vscprintf(m_format, m_args_copy) _vscprintf(m_format, m_args_copy)
#else
#define gd_vsnprintf(m_buffer, m_count, m_format, m_args_copy) vsnprintf(m_buffer, m_count, m_format, m_args_copy)
#define gd_vscprintf(m_format, m_args_copy) vsnprintf(NULL, 0, p_format, m_args_copy)
#endif

String sstr_format(const char *p_format, va_list p_list) {
	char *buffer = sstr_format_new(p_format, p_list);

	String res(buffer);
	memdelete_arr(buffer);

	return res;
}

char *sstr_format_new(const char *p_format, ...) {
	va_list list;

	va_start(list, p_format);
	char *res = sstr_format_new(p_format, list);
	va_end(list);

	return res;
}

char *sstr_format_new(const char *p_format, va_list p_list) {
	va_list list;

	va_copy(list, p_list);
	int len = gd_vscprintf(p_format, list);
	va_end(list);

	len += 1; // for the trailing '/0'

	char *buffer(memnew_arr(char, len));

	va_copy(list, p_list);
	gd_vsnprintf(buffer, len, p_format, list);
	va_end(list);

	return buffer;
}
