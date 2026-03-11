#pragma once

#include <range/v3/range.hpp>
#include <tl/expected.hpp>
#include <utf8cpp/utf8.h>
#include <fmt/format.h>

#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace pars {
namespace u8 {
class Cursor {
public:
	inline static constexpr char32_t eof = 0;

public:
	constexpr Cursor(std::u8string_view sv) :
		_cursor {(char*)sv.data()}, _begin {(char*)sv.data()}, _size {sv.size()} {}

	constexpr explicit Cursor(std::string_view sv) :
		_cursor {const_cast<char*>(sv.data())}, _begin {sv.data()}, _size {sv.size()} {}

	Cursor(const Cursor&)				 = default;
	Cursor(Cursor&&) noexcept			 = default;
	Cursor& operator=(const Cursor&)	 = default;
	Cursor& operator=(Cursor&&) noexcept = default;

public:
	[[nodiscard]] constexpr auto begin() const { return _begin; }

	[[nodiscard]] constexpr auto size() const { return _size; }

	[[nodiscard]] constexpr auto end() const { return begin() + size(); }

	auto						 bump() -> char32_t {
		try {
			return utf8::next(_cursor, const_cast<char*>(end()));
		} catch (const utf8::not_enough_room&) { return eof; }
	}

	[[nodiscard]] auto peek() const -> char32_t {
		try {
			return utf8::peek_next(_cursor, const_cast<char*>(end()));
		} catch (const utf8::not_enough_room&) { return eof; }
	}

	auto prior() { return utf8::prior(_cursor, const_cast<char*>(begin())); }

private:
	char*		_cursor;
	const char* _begin;
	size_t		_size;
};
}  // namespace u8

constexpr auto to_utf8(char32_t cp) -> std::string {
	std::string out;
	if (cp <= 0x7F) {
		out.push_back(static_cast<char>(cp));
	} else if (cp <= 0x7FF) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0xFFFF) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0x10FFFF) {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	return out;
}

template<typename T>
struct value_type_of {
	static_assert(false, "Value type undefined for `T`");
};

template<typename T, typename E>
struct value_type_of<tl::expected<T, E>> {
	using type = T;
};

template<typename T>
using value_type_of_t = value_type_of<T>::type;

template<typename T>
struct error_type_of {
	static_assert(false, "Error type undefined for `T`");
};

template<typename T, typename E>
struct error_type_of<tl::expected<T, E>> {
	using type = E;
};

template<typename T>
using error_type_of_t = error_type_of<T>::type;

template<typename... Ts>
struct overload : Ts... {
	using Ts::operator()...;

	inline friend constexpr auto operator|(overload&& ov, auto&& v) {
		return std::visit(ov, std::forward<decltype(v)>(v));
	}

	inline friend constexpr auto operator|(auto&& v, overload&& ov) {
		return std::visit(ov, std::forward<decltype(v)>(v));
	}
};

template<typename T, typename... Ts>
struct is_among {};

template<typename T>
struct is_among<T> : public std::false_type {};

template<typename T, typename... Ts>
struct is_among<T, T, Ts...> : public std::true_type {};

template<typename T, typename Telse, typename... Ts>
struct is_among<T, Telse, Ts...> : public is_among<T, Ts...> {};

template<typename T, typename... Ts>
inline static constexpr auto is_among_v = is_among<T, Ts...>::value;

template<typename... T>
struct unique_variant_helper {};

template<typename... ChosenTs, typename CandT0, typename... CandTs>
	requires is_among_v<CandT0, ChosenTs...>
struct unique_variant_helper<std::tuple<ChosenTs...>, std::tuple<CandT0, CandTs...>> :
	public unique_variant_helper<std::tuple<ChosenTs...>, std::tuple<CandTs...>> {};

template<typename... ChosenTs, typename CandT0, typename... CandTs>
	requires(!is_among_v<CandT0, ChosenTs...>)
struct unique_variant_helper<std::tuple<ChosenTs...>, std::tuple<CandT0, CandTs...>> :
	public unique_variant_helper<std::tuple<ChosenTs..., CandT0>, std::tuple<CandTs...>> {};

template<typename... ChosenTs>
struct unique_variant_helper<std::tuple<ChosenTs...>, std::tuple<>> {
	using type = std::variant<ChosenTs...>;
};

template<typename... Ts>
struct unique_variant_helper<std::variant<Ts...>> :
	public unique_variant_helper<std::tuple<>, std::tuple<Ts...>> {};

template<typename... Ts>
struct unique_variant : public unique_variant_helper<std::variant<Ts...>> {};

template<typename... Ts>
using unique_variant_t = unique_variant<Ts...>::type;

template<typename T>
struct tuple_like : std::false_type {};

template<typename... Ts>
struct tuple_like<std::tuple<Ts...>> : public std::true_type {};

template<typename... Ts>
struct tuple_like<std::tuple<Ts...>&> : public std::true_type {};

template<typename... Ts>
struct tuple_like<const std::tuple<Ts...>&> : public std::true_type {};

template<typename... Ts>
struct tuple_like<std::tuple<Ts...>&&> : public std::true_type {};

template<typename T>
inline static constexpr auto tuple_like_v = tuple_like<T>::value;

template<typename T>
concept tuple_like_c = tuple_like_v<T>;

template<typename F, typename T>
struct tuple_apply_result {
	using type = decltype(std::apply(std::declval<F>(), std::declval<T>()));
};

template<typename F, typename T>
using tuple_apply_result_t = tuple_apply_result<F, T>::type;

template<typename F, typename T>
struct auto_tuple_apply_result {
	using type = std::invoke_result_t<F, T&&>;
};

template<typename F, tuple_like_c T>
struct auto_tuple_apply_result<F, T> {
	using type = decltype(std::apply(std::declval<F>(), std::declval<T>()));
};

template<typename F, typename T>
using auto_tuple_apply_result_t = auto_tuple_apply_result<F, T>::type;

template<typename F, typename T>
struct tuple_applyable : public std::false_type {};

template<typename F, tuple_like_c T>
	requires requires(F f, T t) { std::apply(f, t); }
struct tuple_applyable<F, T> : public std::true_type {};

static_assert(tuple_applyable<decltype([](int, double) {}), std::tuple<int, double>>::value);

template<typename F, typename T>
inline static constexpr auto tuple_applyable_v = tuple_applyable<F, T>::value;

template<typename F, typename T>
concept tuple_applyable_c = tuple_applyable_v<F, T>;

template<typename T, template<typename...> class TemplT>
struct is_like : std::false_type {};

template<typename... Args, template<typename...> class TemplT>
struct is_like<TemplT<Args...>, TemplT> : std::true_type {};

template<typename T, template<typename...> class TemplT>
inline constexpr auto is_like_v = is_like<T, TemplT>::value;

template<typename T, template<typename...> class TemplT>
concept Like = is_like<T, TemplT>::value;

template<size_t N>
struct U32StringLiteral {
	inline static constexpr auto size = N;

	std::array<char32_t, N>		 str;

	constexpr U32StringLiteral(const char32_t (&arr)[N]) : str(arr, N) {}
};

template<size_t N>
struct NoEndU32StringLiteral {
	inline static constexpr auto size = N - 1;

	std::array<char32_t, size>	 str;

	constexpr NoEndU32StringLiteral(const char32_t (&arr)[N]) :
		str([&]<size_t... Is>(std::index_sequence<Is...>) -> std::array<char32_t, size> {
			return {arr[Is]...};
		}(std::make_index_sequence<size>())) {}
};

template<typename TupToT, typename TupFromT>
struct tuple_cup_two {};

template<typename... TupToTs, typename TupFromT0, typename... TupFromTs>
struct tuple_cup_two<std::tuple<TupToTs...>, std::tuple<TupFromT0, TupFromTs...>> {
	using type = std::conditional_t<
		is_among_v<TupFromT0, TupToTs...>,
		typename tuple_cup_two<std::tuple<TupToTs...>, std::tuple<TupFromTs...>>::type,
		typename tuple_cup_two<std::tuple<TupToTs..., TupFromT0>, std::tuple<TupFromTs...>>::type>;
};

template<typename... TupToTs>
struct tuple_cup_two<std::tuple<TupToTs...>, std::tuple<>> {
	using type = std::tuple<TupToTs...>;
};

template<Like<std::tuple>... TupTs>
struct tuple_cup {};

template<Like<std::tuple> TupT0, Like<std::tuple> TupT1, Like<std::tuple>... TupTs>
struct tuple_cup<TupT0, TupT1, TupTs...> {
	using type = tuple_cup<typename tuple_cup_two<TupT0, TupT1>::type, TupTs...>::type;
};

template<Like<std::tuple> TupT>
struct tuple_cup<TupT> {
	using type = TupT;
};

template<Like<std::tuple>... TupTs>
using tuple_cup_t = tuple_cup<TupTs...>::type;

template<typename InvokerT, typename TagT, typename... Args>
concept MemTagInvocable = requires(InvokerT invoker, Args&&... args) {
	std::forward<InvokerT>(invoker).tag_invoke(TagT(), std::forward<Args>(args)...);
};
template<typename InvokerT, typename TagT, typename... Args>
concept FreeTagInvocable = requires(InvokerT invoker, Args&&... args) {
	tag_invoke(TagT(), std::forward<InvokerT>(invoker), std::forward<Args>(args)...);
};

template<typename InvokerT, typename TagT, typename... Args>
concept MemTagInvocableOnly =
	MemTagInvocable<InvokerT, TagT, Args...> && !FreeTagInvocable<InvokerT, TagT, Args...>;
template<typename InvokerT, typename TagT, typename... Args>
concept FreeTagInvocableOnly =
	FreeTagInvocable<InvokerT, TagT, Args...> && !MemTagInvocable<InvokerT, TagT, Args...>;

template<typename InvokerT, typename TagT, typename... Args>
concept TagInvocableAmbiguously =
	MemTagInvocable<InvokerT, TagT, Args...> && FreeTagInvocable<InvokerT, TagT, Args...>;

template<typename InvokerT, typename TagT, typename... Args>
concept NoThrowMemTagInvocable = requires(InvokerT invoker, Args&&... args) {
	{ std::forward<InvokerT>(invoker).tag_invoke(TagT(), std::forward<Args>(args)...) } noexcept;
};
template<typename InvokerT, typename TagT, typename... Args>
concept NoThrowFreeTagInvocable = requires(InvokerT invoker, Args&&... args) {
	{ tag_invoke(TagT(), std::forward<InvokerT>(invoker), std::forward<Args>(args)...) } noexcept;
};

template<typename InvokerT, typename TagT, typename... Args>
concept TagInvocable =
	FreeTagInvocable<InvokerT, TagT, Args...> || MemTagInvocable<InvokerT, TagT, Args...>;

template<typename TagT, typename... Args>
struct UnifiedCallOpClosure : public std::tuple<Args&&...> {
	explicit constexpr UnifiedCallOpClosure(Args&&... args) :
		std::tuple<Args&&...> {std::forward<Args>(args)...} {}

	template<TagInvocableAmbiguously<TagT, Args...> InvokerT>
	inline friend constexpr auto operator|(
		InvokerT&& invoker, const UnifiedCallOpClosure& op
	) noexcept(NoThrowFreeTagInvocable<InvokerT, TagT, Args...>) {
		return std::apply(
			[&](auto&&... args) {
				return tag_invoke(
					TagT(),
					std::forward<InvokerT>(invoker),
					std::forward<Args>(args)...
				);
			},
			static_cast<const std::tuple<Args&&...>&>(op)
		);
	}

	template<FreeTagInvocableOnly<TagT, Args...> InvokerT>
	inline friend constexpr auto operator|(
		InvokerT&& invoker, const UnifiedCallOpClosure& op
	) noexcept(NoThrowFreeTagInvocable<InvokerT, TagT, Args...>) {
		return std::apply(
			[&](auto&&... args) {
				return tag_invoke(
					TagT(),
					std::forward<InvokerT>(invoker),
					std::forward<Args>(args)...
				);
			},
			static_cast<const std::tuple<Args&&...>&>(op)
		);
	}

	template<MemTagInvocableOnly<TagT, Args...> InvokerT>
	inline friend constexpr auto operator|(
		InvokerT&& invoker, const UnifiedCallOpClosure& op
	) noexcept(NoThrowMemTagInvocable<InvokerT, TagT, Args...>) {
		return std::apply(
			[&](auto&&... args) {
				return std::forward<InvokerT>(invoker).tag_invoke(
					TagT(),
					std::forward<Args>(args)...
				);
			},
			static_cast<const std::tuple<Args&&...>&>(op)
		);
	}
};

template<typename TagT>
struct UnifiedCallOp {
	template<typename InvokerT, typename... Args>
		requires TagInvocableAmbiguously<InvokerT, TagT, Args...>
	inline constexpr auto operator()(
		InvokerT&& invoker, Args&&... args
	) const noexcept(NoThrowFreeTagInvocable<InvokerT, TagT, Args...>) -> decltype(auto) {
		return tag_invoke(TagT(), std::forward<InvokerT>(invoker), std::forward<Args>(args)...);
	}

	template<typename InvokerT, typename... Args>
		requires FreeTagInvocableOnly<InvokerT, TagT, Args...>
	inline constexpr auto operator()(
		InvokerT&& invoker, Args&&... args
	) const noexcept(NoThrowFreeTagInvocable<InvokerT, TagT, Args...>) -> decltype(auto) {
		return tag_invoke(TagT(), std::forward<InvokerT>(invoker), std::forward<Args>(args)...);
	}

	template<typename InvokerT, typename... Args>
		requires MemTagInvocableOnly<InvokerT, TagT, Args...>
	inline constexpr auto operator()(
		InvokerT&& invoker, Args&&... args
	) const noexcept(NoThrowMemTagInvocable<InvokerT, TagT, Args...>) -> decltype(auto) {
		return std::forward<InvokerT>(invoker).tag_invoke(TagT(), std::forward<Args>(args)...);
	}

	template<typename... Args>
	inline constexpr auto operator()(Args&&... args) const noexcept
		-> UnifiedCallOpClosure<TagT, Args...> {
		return UnifiedCallOpClosure<TagT, Args...> {std::forward<Args>(args)...};
	}
};

}  // namespace pars
