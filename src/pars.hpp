#pragma once

#include "fmt/base.h"
#include "pars/Utils.hpp"

#include <tl/expected.hpp>
#include <fmt/format.h>

#include <optional>
#include <algorithm>
#include <stdexcept>
#include <concepts>
#include <format>
#include <map>
#include <utility>
#include <variant>

namespace pars {
	template<typename T>
	concept ParserStateC = requires(T t) {
		{ t.bump() } -> std::convertible_to<char32_t>;
	};

	template<typename T, typename PS>
	concept ParserRuleC = requires(T t, PS& ps) { t.match(ps); };

	struct ParserState : public u8::Cursor {
		// std::map<size_t, size_t> pracket;

		using u8::Cursor::bump;

		struct Restore {
			u8::Cursor cursor;
			// std::size_t pracket_size;
		};

		[[nodiscard]] auto store() const -> Restore {
			return {
				*this,
				// pracket.size(),
			};
		}

		void load(const Restore& rs) { static_cast<Cursor&>(*this) = rs.cursor; }
	};

	template<ParserStateC ParserStateT = ParserState>
	struct ParserKit {
		template<ParserRuleC<ParserStateT> R>
		using RuleResult =
			std::remove_cvref_t<decltype(std::declval<R>().match(std::declval<ParserStateT&>()))>;
		template<ParserRuleC<ParserStateT> R>
		using RuleValue = value_type_of_t<RuleResult<R>>;
		template<ParserRuleC<ParserStateT> R>
		using RuleError = error_type_of_t<RuleResult<R>>;

		struct EarlyEofError {
			inline constexpr auto to_string() const { return "Early EOF."; }
		};

		struct NeverFailError {
			inline constexpr auto to_string() const {
				throw std::logic_error { "this kind of error should never happen!" };
			}
		};

		struct CharRule {
			char32_t c;

			struct UnexpectedCharError {
				char32_t			  expect;
				char32_t			  received;

				inline constexpr auto to_string() const {
					return std::format(
						"expect `{}`, received `{}`.",
						to_utf8(expect),
						to_utf8(received)
					);
				}
			};

			using Error = std::variant<EarlyEofError, UnexpectedCharError>;

			bool		   operator==(const CharRule&) const = default;

			constexpr auto match(ParserStateT& ps) const -> tl::expected<char32_t, Error> {
				if (const auto ch = ps.bump())
					if (ch == c)
						return c;
					else
						return tl::make_unexpected<Error>(UnexpectedCharError {
							.expect	  = c,
							.received = ch,
						});
				else
					return tl::make_unexpected<Error>(EarlyEofError {});
			}
		};

		struct AnyCharRule {
			auto match(ParserStateT& ps) const -> tl::expected<char32_t, NeverFailError> {
				return ps.bump();
			}
		};

		using c = CharRule;

		struct CharRangeRule {
			char32_t lo;
			char32_t hi;

			struct CharOutOfRangeError {
				char32_t			  lo;
				char32_t			  hi;
				char32_t			  received;

				inline constexpr auto to_string() const {
					return std::format(
						"expect char in range [`{}`, `{}`], received `{}`.",
						lo,
						hi,
						received
					);
				}
			};

			using Error = std::variant<CharOutOfRangeError, EarlyEofError>;

			auto match(ParserStateT& ps) const -> tl::expected<char32_t, Error> {
				if (const auto ch = ps.bump())
					if (lo <= ch && ch <= hi)
						return ch;
					else
						return tl::make_unexpected<Error>(CharOutOfRangeError {
							.lo		  = lo,
							.hi		  = hi,
							.received = ch,
						});
				else
					return tl::make_unexpected<Error>(EarlyEofError {});
			}
		};

		template<size_t N>
		struct CharSetRule : std::array<char32_t, N> {
			constexpr CharSetRule(auto&&... cs) :
				std::array<char32_t, N> { std::forward<decltype(cs)>(cs)... } {
				std::ranges::sort(*this);
			}

			struct UnexpectedCharError {
				std::span<const char32_t, N> expect;
				char32_t					 received;

				inline constexpr auto		 to_string() const {
					   std::string s = fmt::format(
						   "received `{}`, which is not among expected chars: {{ ",
						   to_utf8(received)
					   );

					   for (const auto exp : expect) s += fmt::format("`{}`, ", to_utf8(exp));

					   s += '}';

					   return s;
				}
			};

			using Error = std::variant<UnexpectedCharError, EarlyEofError>;

			auto match(ParserStateT& ps) const -> tl::expected<char32_t, Error> {
				if (const auto ch = ps.bump())
					if (_char_match(ch))
						return ch;
					else
						return tl::make_unexpected<Error>(UnexpectedCharError {
							.expect	  = *this,
							.received = ch,
						});
				else
					return tl::make_unexpected<Error>(EarlyEofError {});
			}

		private:
			[[nodiscard]] auto _char_match(char32_t ch) const -> bool {
				return _char_match(ch, 0, N);
			}

			[[nodiscard]] auto _char_match(char32_t ch, size_t lo, size_t hi) const -> bool {
				if (lo == hi)
					return false;
				const auto m = ((lo + hi) >> 1);
				if (ch < (*this)[m])
					return _char_match(ch, lo, m - 1);
				else if (ch > (*this)[m])
					return _char_match(ch, m + 1, hi);
				else  //  ch == (*this)[m]
					return true;
			}
		};

		template<typename... Ts>
		CharSetRule(Ts&&...) -> CharSetRule<sizeof...(Ts)>;

		template<ParserRuleC<ParserStateT>... Rs>
		struct SequentialRule : std::tuple<Rs...> {
			using Value = std::tuple<RuleValue<Rs>...>;
			using Error = unique_variant_t<RuleError<Rs>...>;

			template<ParserRuleC<ParserStateT>... Rs1, ParserRuleC<ParserStateT>... Rs2>
			constexpr SequentialRule(SequentialRule<Rs1...>&& s1, SequentialRule<Rs2...>&& s2) :
				std::tuple<Rs...> { std::tuple_cat(
					static_cast<std::tuple<Rs1...>&&>(std::move(s1)),
					static_cast<std::tuple<Rs2...>&&>(std::move(s2))
				) } {}

			template<ParserRuleC<ParserStateT>... Rs1, ParserRuleC<ParserStateT> R2>
			constexpr SequentialRule(SequentialRule<Rs1...>&& s1, R2&& r2) :
				std::tuple<Rs...> { std::tuple_cat(
					static_cast<std::tuple<Rs1...>&&>(std::move(s1)),
					std::make_tuple(std::forward<R2>(r2))
				) } {}

			template<ParserRuleC<ParserStateT> R1, ParserRuleC<ParserStateT>... Rs2>
			constexpr SequentialRule(R1&& r1, SequentialRule<Rs2...>&& s2) :
				std::tuple<Rs...> { std::tuple_cat(
					std::make_tuple(std::forward<R1>(r1)),
					static_cast<std::tuple<Rs2...>&&>(std::move(s2))
				) } {}

			constexpr SequentialRule(ParserRuleC<ParserStateT> auto&&... rs) :
				std::tuple<Rs...> { std::forward<decltype(rs)>(rs)... } {}

			auto match(ParserStateT& ps) const -> tl::expected<Value, Error> {
				return [&]<size_t... Is>(std::index_sequence<Is...>) {
					tl::expected<Value, Error> res = Value {};
					(... && [&]() {
						auto r = std::get<Is>(*this).match(ps);
						if (r) {
							std::get<Is>(*res) = std::move(*r);
							return true;
						} else {
							res = tl::make_unexpected<Error>(std::move(r.error()));
							return false;
						}
					}());
					return res;
				}(std::make_index_sequence<sizeof...(Rs)>());
			}
		};

		template<ParserRuleC<ParserStateT>... Rs1, ParserRuleC<ParserStateT>... Rs2>
		SequentialRule(SequentialRule<Rs1...>&&, SequentialRule<Rs2...>&&)
			-> SequentialRule<Rs1..., Rs2...>;
		template<ParserRuleC<ParserStateT>... Rs1, ParserRuleC<ParserStateT> R2>
		SequentialRule(SequentialRule<Rs1...>&&, R2&&) -> SequentialRule<Rs1..., R2>;
		template<ParserRuleC<ParserStateT> R1, ParserRuleC<ParserStateT>... Rs2>
		SequentialRule(R1&&, SequentialRule<Rs2...>&&) -> SequentialRule<R1, Rs2...>;
		template<ParserRuleC<ParserStateT> R1, ParserRuleC<ParserStateT> R2>
		SequentialRule(R1&&, R2&&) -> SequentialRule<R1, R2>;

		template<ParserRuleC<ParserStateT>... Rs>
		struct ChoiceRule : std::tuple<Rs...> {
			using Value = unique_variant_t<RuleValue<Rs>...>;
			using Error = std::tuple<RuleError<Rs>...>;

			template<ParserRuleC<ParserStateT>... Rs1, ParserRuleC<ParserStateT>... Rs2>
			constexpr ChoiceRule(ChoiceRule<Rs1...>&& s1, ChoiceRule<Rs2...>&& s2) :
				std::tuple<Rs...> { std::tuple_cat(
					static_cast<std::tuple<Rs1...>&&>(std::move(s1)),
					static_cast<std::tuple<Rs2...>&&>(std::move(s2))
				) } {}

			template<ParserRuleC<ParserStateT>... Rs1, ParserRuleC<ParserStateT> R2>
			constexpr ChoiceRule(ChoiceRule<Rs1...>&& s1, R2&& r2) :
				std::tuple<Rs...> { std::tuple_cat(
					static_cast<std::tuple<Rs1...>&&>(std::move(s1)),
					std::make_tuple(std::forward<R2>(r2))
				) } {}

			template<ParserRuleC<ParserStateT> R1, ParserRuleC<ParserStateT>... Rs2>
			constexpr ChoiceRule(R1&& r1, ChoiceRule<Rs2...>&& s2) :
				std::tuple<Rs...> { std::tuple_cat(
					std::make_tuple(std::forward<R1>(r1)),
					static_cast<std::tuple<Rs2...>&&>(std::move(s2))
				) } {}

			constexpr ChoiceRule(ParserRuleC<ParserStateT> auto&&... rs) :
				std::tuple<Rs...> { std::forward<decltype(rs)>(rs)... } {}

			auto match(ParserStateT& ps) const -> tl::expected<Value, Error> {
				return [&]<size_t... Is>(std::index_sequence<Is...>) -> tl::expected<Value, Error> {
					tl::expected<Value, Error> res = tl::make_unexpected(Error {});
					Error					   err;
					const auto				   store = ps.store();
					if ((... || [&]() {
							auto r = std::get<Is>(*this).match(ps);
							if (r) {
								res = std::move(*r);
								return true;
							} else {
								std::get<Is>(err) = std::move(r.error());
								ps.load(store);
								return false;
							}
						}()))
						return res;
					else
						return tl::make_unexpected(std::move(err));
				}(std::make_index_sequence<sizeof...(Rs)>());
			}
		};

		template<ParserRuleC<ParserStateT>... Rs1, ParserRuleC<ParserStateT>... Rs2>
		ChoiceRule(ChoiceRule<Rs1...>&&, ChoiceRule<Rs2...>&&) -> ChoiceRule<Rs1..., Rs2...>;
		template<ParserRuleC<ParserStateT>... Rs1, ParserRuleC<ParserStateT> R2>
		ChoiceRule(ChoiceRule<Rs1...>&&, R2&&) -> ChoiceRule<Rs1..., R2>;
		template<ParserRuleC<ParserStateT> R1, ParserRuleC<ParserStateT>... Rs2>
		ChoiceRule(R1&&, ChoiceRule<Rs2...>&&) -> ChoiceRule<R1, Rs2...>;
		template<ParserRuleC<ParserStateT> R1, ParserRuleC<ParserStateT> R2>
		ChoiceRule(R1&&, R2&&) -> ChoiceRule<R1, R2>;

		template<ParserRuleC<ParserStateT> R>
		struct OptionalRule : R {
			using Value = std::optional<RuleValue<R>>;
			using Error = NeverFailError;

			constexpr OptionalRule(auto&&... args) : R { std::forward<decltype(args)>(args)... } {}

			auto match(ParserStateT& ps) const -> tl::expected<Value, Error> {
				if (auto m = this->R::match(ps))
					return Value { std::move(*m) };
				else
					return std::nullopt;
			}
		};

		template<ParserRuleC<ParserStateT> R>
		struct OnceOrMoreRule : R {
			using Value = std::vector<RuleValue<R>>;
			using Error = RuleError<R>;

			constexpr OnceOrMoreRule(auto&&... args) :
				R { std::forward<decltype(args)>(args)... } {}

			auto match(ParserStateT& ps) const -> tl::expected<Value, Error> {
				Value	   val;
				const auto restore	   = ps.store();
				auto	   first_match = this->R::match(ps);
				if (first_match) {
					val.emplace_back(std::move(*first_match));
					while (auto m = this->R::match(ps)) val.emplace_back(std::move(*m));
					return std::move(val);
				} else {
					ps.load(restore);
					return tl::make_unexpected(first_match.error());
				}
			}
		};

		template<ParserRuleC<ParserStateT> R>
		struct RepeatableRule : R {
			using Value = std::vector<RuleValue<R>>;
			using Error = NeverFailError;

			constexpr RepeatableRule(auto&&... args) :
				R { std::forward<decltype(args)>(args)... } {}

			auto match(ParserStateT& ps) const -> tl::expected<Value, Error> {
				tl::expected<Value, Error> res = Value {};

				while (auto m = this->R::match(ps)) res->emplace_back(std::move(*m));

				return res;
			}
		};

		template<typename R>
		RepeatableRule(R&&) -> RepeatableRule<R>;

		template<ParserRuleC<ParserStateT> R>
		struct PeekIsRule {
			using Value = std::monostate;
			using Error = RuleError<R>;
		};

		template<ParserRuleC<ParserStateT> R>
		struct PeekNotRule {
			using Value = std::monostate;
			using Error = RuleError<R>;
		};

		template<typename F>
		struct FixRule : F {
			constexpr FixRule(F&& f) : F(std::move(f)) {}

			constexpr bool match(ParserStateT& ps) const { return (*this)(*this).match(ps); }
		};

		template<typename F>
		inline constexpr auto fix(F&& f) {
			return FixRule<std::decay_t<F>>(std::forward<F>(f));
		}

		inline friend constexpr auto operator>>(
			ParserRuleC<ParserStateT> auto&& l, ParserRuleC<ParserStateT> auto&& r
		) {
			return SequentialRule {
				std::forward<decltype(l)>(l),
				std::forward<decltype(r)>(r),
			};
		}

		inline friend constexpr auto operator|(
			ParserRuleC<ParserStateT> auto&& l, ParserRuleC<ParserStateT> auto&& r
		) {
			return ChoiceRule {
				std::forward<decltype(l)>(l),
				std::forward<decltype(r)>(r),
			};
		}

		template<ParserRuleC<ParserStateT> R>
		[[nodiscard]] inline friend constexpr auto operator-(R&& r) noexcept {
			return OptionalRule<R> { std::forward<R>(r) };
		}

		template<ParserRuleC<ParserStateT> R>
		[[nodiscard]] inline friend constexpr auto operator+(R&& r) noexcept {
			return OnceOrMoreRule<R> { std::forward<R>(r) };
		}

		template<ParserRuleC<ParserStateT> R>
		[[nodiscard]] inline friend constexpr auto operator*(R&& r) noexcept {
			return RepeatableRule<R> { std::forward<R>(r) };
		}

		template<ParserRuleC<ParserStateT> R>
		[[nodiscard]] inline friend constexpr auto operator~(R&& r) noexcept {
			return PeekIsRule<R> { std::forward<R>(r) };
		}

		template<ParserRuleC<ParserStateT> R>
		[[nodiscard]] inline friend constexpr auto operator!(R&& r) noexcept {
			return PeekNotRule<R> {};
		}
	};

	using CharRule					  = ParserKit<>::CharRule;
	using c							  = ParserKit<>::CharRule;
	using AnyCharRule				  = ParserKit<>::AnyCharRule;
	inline static constexpr auto cany = ParserKit<>::AnyCharRule {};
	using CharRangeRule				  = ParserKit<>::CharRangeRule;
	using cran						  = ParserKit<>::CharRangeRule;
	template<size_t N>
	using CharSetRule = ParserKit<>::CharSetRule<N>;
	template<size_t N>
	using cset = ParserKit<>::CharSetRule<N>;
	template<typename... Ts>
	using SequentialRule = ParserKit<>::SequentialRule<Ts...>;
	template<typename... Ts>
	using ChoiceRule = ParserKit<>::ChoiceRule<Ts...>;
	template<typename... Ts>
	using OnceOrMoreRule = ParserKit<>::OnceOrMoreRule<Ts...>;
	template<typename... Ts>
	using RepeatableRule = ParserKit<>::RepeatableRule<Ts...>;
	template<typename... Ts>
	using FixRule = ParserKit<>::FixRule<Ts...>;
	template<typename... Ts>
	using fix = ParserKit<>::FixRule<Ts...>;
	template<typename... Ts>
	using PeekNotRule = ParserKit<>::PeekNotRule<Ts...>;
	template<typename... Ts>
	using PeekIsRule = ParserKit<>::PeekIsRule<Ts...>;

}  // namespace pars