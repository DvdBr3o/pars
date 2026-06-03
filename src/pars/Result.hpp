#pragma once

#include <tl/expected.hpp>

namespace pars {
template<typename T>
concept Result = requires(T t) {
	value_of(t);
	error_of(t);
};

template<typename T, typename E>
struct Expected : tl::expected<T, E> {
	using tl::expected<T, E>::expected;
};

template<typename T, typename E>
inline constexpr auto value_of(const Expected<T, E>& expected) -> decltype(auto) {
	return expected.value();
}

template<typename T>
using value_type_of_t = std::remove_cvref_t<decltype(value_of(std::declval<T>()))>;

template<typename T>
using error_type_of_t = std::remove_cvref_t<decltype(error_of(std::declval<T>()))>;

template<typename T, typename E>
inline constexpr auto error_of(const Expected<T, E>& expected) -> decltype(auto) {
	return expected.error();
}

}  // namespace pars