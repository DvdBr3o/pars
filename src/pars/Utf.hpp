#pragma once

#include "pars/Query.hpp"

#include <utf8.h>

namespace pars::u8 {
class Cursor {
public:
	inline static constexpr char32_t eof = 0;
	using value_type					 = char32_t;

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

struct QueryTextCursor final : QueryTag<QueryTextCursor, u8::Cursor> {};

inline constexpr auto query_text_cursor = QueryTextCursor {};
}  // namespace pars::u8