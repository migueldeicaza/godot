// Minimal shim for Godot's HashMap<K, V> — standalone CLI builds only.
// Maps to std::unordered_map with Godot-compatible API surface.
#pragma once

#include <unordered_map>

// Godot's KeyValue is used in range-for: `for (const KeyValue<K, V> &kv : map)`
template <typename K, typename V>
struct KeyValue {
	const K &key;
	V &value;
};

template <typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>>
class HashMap {
	std::unordered_map<K, V, H, E> _map;

public:
	HashMap() = default;

	V &operator[](const K &p_key) { return _map[p_key]; }
	const V &operator[](const K &p_key) const { return _map.at(p_key); }

	bool has(const K &p_key) const { return _map.count(p_key) > 0; }

	V *getptr(const K &p_key) {
		auto it = _map.find(p_key);
		return it != _map.end() ? &it->second : nullptr;
	}
	const V *getptr(const K &p_key) const {
		auto it = _map.find(p_key);
		return it != _map.end() ? &it->second : nullptr;
	}

	void insert(const K &p_key, const V &p_val) { _map[p_key] = p_val; }
	void clear() { _map.clear(); }
	int64_t size() const { return (int64_t)_map.size(); }
	bool is_empty() const { return _map.empty(); }

	// Iterator that yields KeyValue<K, V> for range-for compatibility.
	class Iterator {
		using MapIt = typename std::unordered_map<K, V, H, E>::iterator;
		MapIt _it;

	public:
		Iterator(MapIt p_it) :
				_it(p_it) {}
		KeyValue<K, V> operator*() { return { _it->first, _it->second }; }
		Iterator &operator++() {
			++_it;
			return *this;
		}
		bool operator!=(const Iterator &p_other) const { return _it != p_other._it; }
	};

	class ConstIterator {
		using MapIt = typename std::unordered_map<K, V, H, E>::const_iterator;
		MapIt _it;

	public:
		ConstIterator(MapIt p_it) :
				_it(p_it) {}
		KeyValue<K, const V> operator*() const {
			return { _it->first, const_cast<V &>(_it->second) };
		}
		ConstIterator &operator++() {
			++_it;
			return *this;
		}
		bool operator!=(const ConstIterator &p_other) const { return _it != p_other._it; }
	};

	Iterator begin() { return Iterator(_map.begin()); }
	Iterator end() { return Iterator(_map.end()); }
	ConstIterator begin() const { return ConstIterator(_map.begin()); }
	ConstIterator end() const { return ConstIterator(_map.end()); }
};
