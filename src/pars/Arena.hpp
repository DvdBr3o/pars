#pragma once

#include <memory_resource>
#include <new>
#include "pars/Query.hpp"

namespace pars {
template<typename T>
class Arena {
public:
	constexpr Arena(T* ptr) : _ptr(ptr) {}

	constexpr Arena(const Arena&)				 = default;
	constexpr Arena(Arena&&) noexcept			 = default;
	constexpr Arena& operator=(const Arena&)	 = default;
	constexpr Arena& operator=(Arena&&) noexcept = default;
	constexpr ~Arena()							 = default;

	constexpr explicit operator T&() & { return *_ptr; }

	constexpr explicit operator T&&() && { return *_ptr; }

	constexpr explicit operator const T&() const& { return *_ptr; }

	constexpr auto	   operator*() -> T& { return *_ptr; }

	constexpr auto	   operator*() const -> const T& { return *_ptr; }

	constexpr auto	   operator->() -> T* { return _ptr; }

	constexpr auto	   operator->() const -> const T* { return _ptr; }

	//
	inline friend constexpr auto operator==(const Arena& l, const Arena& r) -> bool {
		return l._ptr == r._ptr || *l._ptr == *r._ptr;
	}

	template<typename H>
	inline friend constexpr auto AbslHashValue(H h, const Arena& arena) {
		return H::combine(std::move(h), *arena._ptr);
	}

private:
	T* _ptr;
};

template<size_t BasicSize = 4096>
class BasicArenaAllocator {
public:
	BasicArenaAllocator() : _arena(_buffer, BasicSize, std::pmr::new_delete_resource()) {}

public:
	template<typename T>
	auto allocate(size_t n = 1) & -> Arena<T> {
		return reinterpret_cast<T*>(_arena.allocate(sizeof(T) * n));
	}

	template<typename T, typename... Args>
	auto create(Args&&... args) & -> Arena<T> {
		auto ptr = _arena.allocate(sizeof(T));
		new (ptr) T {std::forward<Args>(args)...};
		return reinterpret_cast<T*>(ptr);
	}

	template<typename T>
		requires std::is_trivially_copyable_v<T>
	auto cpy(T* t, size_t n = 1) & -> Arena<T> {
		auto ptr = _arena.allocate(sizeof(T) * n);
		std::memcpy(ptr, t, sizeof(T) * n);
		return reinterpret_cast<T*>(ptr);
	}

private:
	char								_buffer[BasicSize] {};
	std::pmr::monotonic_buffer_resource _arena;
};

using ArenaAllocator		= BasicArenaAllocator<4096>;
using ScratchArenaAllocator = BasicArenaAllocator<128>;

template<size_t Size = 4096>
struct ArenaTag : QueryTag<ArenaTag<Size>, BasicArenaAllocator<Size>> {};

template<size_t Size = 4096>
struct ArenaState : QueryState<ArenaTag<Size>> {};

}  // namespace pars
