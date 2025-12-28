#pragma once

#include <range/v3/range.hpp>
#include <tl/expected.hpp>
#include <utf8cpp/utf8.h>

#include <string_view>
#include <variant>

namespace pars {
	namespace u8 {
		class Cursor {
		public:
			inline static constexpr char32_t eof = 0;

		public:
			constexpr Cursor(std::u8string_view sv) :
				_cursor { (char*)sv.data() }, _begin { (char*)sv.data() }, _size { sv.size() } {}

			constexpr explicit Cursor(std::string_view sv) :
				_cursor { const_cast<char*>(sv.data()) },
				_begin { sv.data() },
				_size { sv.size() } {}

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

			auto peek() { return utf8::peek_next(_cursor, const_cast<char*>(end())); }

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

	template<typename T>
	struct unique_variant {};

	template<typename... Ts>
	struct unique_variant<std::variant<Ts...>> {};

}  // namespace pars