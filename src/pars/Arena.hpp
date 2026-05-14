#pragma once

#include <cstdint>
#include <utility>
#include <memory>
#include <type_traits>
#include <concepts>
#include <vector>
#include <string>

namespace pars::Arena {
template<std::size_t size = 512>
class Block {
private:
	char				   _buffer[size] = {};
	char*				   _index		 = _buffer;
	std::unique_ptr<Block> _trailing	 = nullptr;

public:
	template<typename T>
	struct AllocRes {
		T*	   obj;
		Block* block;
	};

	friend class Pool;

public:
	Block() = default;

public:
	template<typename T, typename... Args>
	auto alloc(Args&&... args) -> AllocRes<T> {
		if (_buffer + size - _index >= sizeof(T)) {
			auto* ptr = new (_index) T {std::forward<Args>(args)...};
			_index += sizeof(T);
			return {ptr, this};
		} else {
			_trailing = std::make_unique<Block>();
			auto* ptr = new (_trailing->_index) T {std::forward<Args>(args)...};
			_trailing->_index += sizeof(T);
			return {ptr, _trailing.get()};
		}
	}

	template<typename T>
	auto allocate(size_t n) -> AllocRes<T> {
		const auto alloc_size = n * sizeof(T);
		if (_buffer + size - _index >= alloc_size) {
			auto* ptr = new (_index) char[alloc_size] {};
			_index += alloc_size;
			return {reinterpret_cast<T*>(ptr), this};
		} else {
			_trailing = std::make_unique<Block>();
			auto* ptr = new (_trailing->_index) char[alloc_size] {};
			_trailing->_index += alloc_size;
			return {reinterpret_cast<T*>(ptr), _trailing.get()};
		}
	}
};

class Pool;

template<typename T>
struct Allocator {
	using value_type = T;
	using char_type	 = T;

	Pool&						 pool;

	auto						 allocate(size_t n) -> T*;

	auto						 deallocate(T* p, size_t n) -> void;

	inline friend constexpr auto operator==(const Allocator& lhs, const Allocator& rhs) {
		return true;
	}
};

template<typename T>
using Vector = std::vector<T, Allocator<T>>;

template<typename CharT>
using BasicString = std::basic_string<CharT, std::char_traits<CharT>, Allocator<CharT>>;

using String	  = BasicString<char>;

class Pool {
private:
	std::unique_ptr<Block<>> _head;
	Block<>*				 _block = _head.get();

public:
	using value_type = void;

public:
	template<typename T, typename... Args>
		requires std::is_trivially_destructible_v<T>
	auto alloc(Args&&... args) -> T* {
		auto [ptr, block] = _block->alloc(std::forward<Args>(args)...);
		_block			  = block;
		return ptr;
	}

	template<typename T>
	auto allocate(size_t n) -> T* {
		auto [ptr, block] = _block->allocate<T>(n);
		_block			  = block;
		return static_cast<T*>(ptr);
	}

	void						 deallocate(void* p, size_t n) {}

	inline friend constexpr auto operator==(const Pool& lhs, const Pool& rhs) { return true; }

	template<typename T>
	constexpr operator Allocator<T>() {
		return {*this};
	}

	template<typename T, typename... Args>
	auto vector(Args&&... args) -> Vector<T> {
		return Vector<T> {std::forward<Args>(args)..., *this};
	}

	// template<typename T>
	// auto make_vector(std::initializer_list<T> elems) -> Vector<T> {
	// 	return Vector<T> {std::move(elems), *this};
	// }

	template<typename T, size_t N>
	auto make_vector(const T (&elems)[N]) -> Vector<T> {
		return Vector<T> {elems, elems + N, *this};
	}

	template<typename... Args>
	auto string(Args&&... args) -> String {
		return String {std::forward<Args>(args)..., *this};
	}
};

template<typename T>
auto Allocator<T>::allocate(size_t n) -> T* {
	return pool.allocate<T>(n);
}

template<typename T>
auto Allocator<T>::deallocate(T* p, size_t n) -> void {
	pool.deallocate(p, n);
}

}  // namespace pars::Arena
