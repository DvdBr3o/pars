#pragma once

#include "pars/Rule.hpp"
#include "pars/Result.hpp"

#include <type_traits>
#include <variant>
#include <string>

#if defined _MSC_VER  // && not defined __clang__
#	define PARS_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#	define PARS_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

namespace pars {
template<typename... Ts>
struct overload : Ts... {
	using Ts::operator()...;
};

template<typename StateT, RuleC<StateT> RuleT>
using rule_result_t = decltype(std::declval<RuleT>().match(std::declval<StateT>()));

template<typename StateT, RuleC<StateT> RuleT>
using rule_value_t = value_type_of_t<rule_result_t<StateT, RuleT>>;

template<typename StateT, RuleC<StateT> RuleT>
using rule_error_t = error_type_of_t<rule_result_t<StateT, RuleT>>;

template<typename T>
struct fn_sig : std::false_type {};

template<typename R, typename... Args>
struct fn_sig<R(Args...)> : std::true_type {
	using result_type				= R;
	using args_tuple				= std::tuple<Args...>;
	static constexpr auto args_size = sizeof...(Args);

	template<size_t Index>
	struct arg {
		using type = std::tuple_element_t<Index, args_tuple>;
	};

	template<size_t Index>
	using arg_t = typename arg<Index>::type;

	template<typename F>
	static constexpr auto permit_fn_v = std::is_invocable_v<F, Args...>
									 && std::is_convertible_v<std::invoke_result_t<F, Args...>, R>;

	template<typename... ArgTs>
	static constexpr auto permit_args_v = (std::is_convertible_v<ArgTs, Args> && ...);
};

template<typename T>
struct fn_tag {
	using signature_type = T::signature_type;
};

template<typename T>
concept FnSigC = fn_sig<T>::value;

template<typename F, typename T>
concept FnSigFnC = FnSigC<T> && fn_sig<T>::template permit_fn_v<F>;

template<typename T>
concept FnTagC = requires { typename T::signature_type; };

template<FnSigC FnSigT>
struct FnTag {
	using signature_type = FnSigT;
};

template<typename FnTagT, typename FnT>
struct FnImpl {
	FnT							 _fn;

	inline friend constexpr auto operator==(const FnImpl&, const FnImpl&) -> bool { return true; }

	constexpr auto				 fn(FnTagT) const -> const FnT& { return _fn; }

	// clang-format on
};

template<typename T, typename... CandTs>
struct is_among {};

template<typename T, typename CandT0, typename... CandTs>
struct is_among<T, CandT0, CandTs...> {
	static constexpr auto value = is_among<T, CandTs...>::value;
};

template<typename T, typename... CandTs>
struct is_among<T, T, CandTs...> {
	static constexpr auto value = true;
};

template<typename T>
struct is_among<T> {
	static constexpr auto value = false;
};

template<typename T, typename... CandTs>
static constexpr auto is_among_v = is_among<T, CandTs...>::value;

template<typename Tuple0T, typename Tuple1T>
struct tuple_cup_two {};

template<typename... Tuple0Ts, typename Tuple1T0, typename... Tuple1Ts>
struct tuple_cup_two<std::tuple<Tuple0Ts...>, std::tuple<Tuple1T0, Tuple1Ts...>> {
	using type = std::conditional_t<
		is_among_v<Tuple1T0, Tuple0Ts...>,
		typename tuple_cup_two<std::tuple<Tuple0Ts...>, std::tuple<Tuple1Ts...>>::type,
		typename tuple_cup_two<std::tuple<Tuple0Ts..., Tuple1T0>, std::tuple<Tuple1Ts...>>::type>;
};

template<typename... Tuple0Ts>
struct tuple_cup_two<std::tuple<Tuple0Ts...>, std::tuple<>> {
	using type = std::tuple<Tuple0Ts...>;
};

template<typename T0, typename T1>
using tuple_cup_two_t = tuple_cup_two<T0, T1>::type;

template<typename... TupleTs>
struct tuple_cup {};

template<typename TupleT0, typename TupleT1, typename... TupleTs>
struct tuple_cup<TupleT0, TupleT1, TupleTs...> {
	using type = tuple_cup<tuple_cup_two_t<TupleT0, TupleT1>, TupleTs...>::type;
};

template<typename TupleT>
struct tuple_cup<TupleT> {
	using type = TupleT;
};

template<typename... TupleTs>
using tuple_cup_t = tuple_cup<TupleTs...>::type;

template<template<typename...> class Templ, typename TupleT>
struct templ_from_type_tuple {};

template<template<typename...> class Templ, typename... TupleTs>
struct templ_from_type_tuple<Templ, std::tuple<TupleTs...>> {
	using type = Templ<TupleTs...>;
};

template<template<typename...> class Templ, typename TupleT>
using templ_from_type_tuple_t = templ_from_type_tuple<Templ, TupleT>::type;

template<template<typename> class TransformT, typename TupleT>
struct transform_tuple_type {};

template<template<typename> class TransformT, typename... TupleTs>
struct transform_tuple_type<TransformT, std::tuple<TupleTs...>> {
	using type = std::tuple<typename TransformT<TupleTs>::type...>;
};

template<template<typename> class TransformT, typename TupleT>
using transform_type_type_t = transform_tuple_type<TransformT, TupleT>::type;

template<typename T, template<typename...> class TemplT>
struct is_like : std::false_type {};

template<template<typename...> class TemplT, typename... Ts>
struct is_like<TemplT<Ts...>, TemplT> : std::true_type {};

template<typename T, template<typename...> class TemplT>
static constexpr auto is_like_v = is_like<T, TemplT>::value;

template<typename T, template<typename...> class TemplT>
concept Like = is_like_v<T, TemplT>;

template<typename... Ts>
inline auto to_string(const std::variant<Ts...>& v) -> std::string {
	return std::visit(overload {[](const auto& o) { return to_string(o); }}, v);
}

/// @brief `std::variant<Ts...>` but with duplicated types in `Ts...` uniqued.
///
/// e.g. `unique_type_variant_t<int, int, int, double, int> == std::variant<int, double>`
template<typename... Ts>
using unique_type_variant_t = templ_from_type_tuple_t<std::variant, tuple_cup_t<std::tuple<Ts>...>>;

template<typename FnT, Like<std::tuple> TupleT>
struct apply_result {};

template<typename FnT, typename... Ts>
struct apply_result<FnT, std::tuple<Ts...>> {
	using type = std::invoke_result_t<FnT, Ts...>;
};

template<typename FnT, Like<std::tuple> TupleT>
using apply_result_t = apply_result<FnT, TupleT>::type;

}  // namespace pars