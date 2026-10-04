// pars/dsl.hpp — surface language of the pars compiler.
//
// A grammar is an ordinary C++ type.  This is the only thing a user writes.
//
//   struct G {
//       struct RuleTags...;
//       using rules = type_list<Tag0, ...>;   // all rules
//       using start = Tag0;
//       template <class Tag> struct def;      // def<Tag>::type = body
//   };
//
// Nodes:
//   eps, any, lit<'c'>, cls<Cs...>, seq<...>, alt<...>,
//   rep<G,Min,Max>, notp<G>, rule<Tag>
// Aliases: opt, star, plus.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pars {

struct eps {};

struct any {};

template<char C>
struct lit {
	static constexpr char value = C;
};

template<char... Cs>
struct cls {
	static constexpr bool has(char c) noexcept { return ((c == Cs) || ...); }
};

template<class... Gs>
struct seq {};

template<class... Gs>
struct alt {};

template<class G, int Min = 0, int Max = -1>
struct rep {};

template<class G>
struct notp {};

template<class Tag>
struct rule {};

template<class G>
using opt = alt<G, eps>;
template<class G>
using star = rep<G, 0, -1>;
template<class G>
using plus = rep<G, 1, -1>;

// ---------------------------------------------------------------------------
// Type list utilities
// ---------------------------------------------------------------------------
template<class... Ts>
struct type_list {};

template<class List>
struct list_size;

template<class... Ts>
struct list_size<type_list<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};
template<class List>
inline constexpr std::size_t list_size_v = list_size<List>::value;

template<class T, class List>
struct list_contains;

template<class T, class... Ts>
struct list_contains<T, type_list<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};
template<class T, class List>
inline constexpr bool list_contains_v = list_contains<T, List>::value;

template<class List, class T>
struct push_back;

template<class... Ts, class T>
struct push_back<type_list<Ts...>, T> {
	using type = type_list<Ts..., T>;
};
template<class List, class T>
using push_back_t = typename push_back<List, T>::type;

template<class List, std::size_t I>
struct nth;

template<class T, class... Ts>
struct nth<type_list<T, Ts...>, 0> {
	using type = T;
};

template<class T, class... Ts, std::size_t I>
struct nth<type_list<T, Ts...>, I> {
	using type = typename nth<type_list<Ts...>, I - 1>::type;
};
template<class List, std::size_t I>
using nth_t = typename nth<List, I>::type;

template<class T, class List>
struct index_of;

template<class T, class... Ts>
struct index_of<T, type_list<T, Ts...>> : std::integral_constant<std::size_t, 0> {};

template<class T, class U, class... Ts>
struct index_of<T, type_list<U, Ts...>> :
	std::integral_constant<std::size_t, 1 + index_of<T, type_list<Ts...>>::value> {};
template<class T, class List>
inline constexpr std::size_t index_of_v = index_of<T, List>::value;

}  // namespace pars
