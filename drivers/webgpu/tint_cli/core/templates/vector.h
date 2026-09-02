// Minimal shim for Godot's Vector<T> — standalone CLI builds only.
// Maps to std::vector with Godot-compatible API surface.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

template <typename T>
class Vector {
	std::vector<T> _data;

public:
	Vector() = default;
	Vector(const Vector &) = default;
	Vector(Vector &&) = default;
	Vector &operator=(const Vector &) = default;
	Vector &operator=(Vector &&) = default;

	int64_t size() const { return (int64_t)_data.size(); }
	bool is_empty() const { return _data.empty(); }

	void resize(int64_t p_size) { _data.resize((size_t)p_size); }
	void clear() { _data.clear(); }

	void push_back(const T &p_val) { _data.push_back(p_val); }
	void append(const T &p_val) { _data.push_back(p_val); }

	// Godot's find() returns index or -1.
	int64_t find(const T &p_val) const {
		for (size_t i = 0; i < _data.size(); i++) {
			if (_data[i] == p_val) return (int64_t)i;
		}
		return -1;
	}

	const T *ptr() const { return _data.data(); }
	T *ptrw() { return _data.data(); }

	const T &operator[](int64_t p_idx) const { return _data[(size_t)p_idx]; }
	T &operator[](int64_t p_idx) { return _data[(size_t)p_idx]; }

	// Range-based for loop support.
	typename std::vector<T>::iterator begin() { return _data.begin(); }
	typename std::vector<T>::iterator end() { return _data.end(); }
	typename std::vector<T>::const_iterator begin() const { return _data.begin(); }
	typename std::vector<T>::const_iterator end() const { return _data.end(); }
};
