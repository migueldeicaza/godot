// Minimal shim for Godot's HashSet<T> — standalone CLI builds only.
// Maps to std::unordered_set with Godot-compatible API surface.
#pragma once

#include <unordered_set>

template <typename T, typename H = std::hash<T>, typename E = std::equal_to<T>>
class HashSet {
	std::unordered_set<T, H, E> _set;

public:
	HashSet() = default;

	void insert(const T &p_val) { _set.insert(p_val); }
	bool has(const T &p_val) const { return _set.count(p_val) > 0; }
	void clear() { _set.clear(); }
	int64_t size() const { return (int64_t)_set.size(); }
	bool is_empty() const { return _set.empty(); }

	// Range-based for loop support.
	typename std::unordered_set<T, H, E>::iterator begin() { return _set.begin(); }
	typename std::unordered_set<T, H, E>::iterator end() { return _set.end(); }
	typename std::unordered_set<T, H, E>::const_iterator begin() const { return _set.begin(); }
	typename std::unordered_set<T, H, E>::const_iterator end() const { return _set.end(); }
};
