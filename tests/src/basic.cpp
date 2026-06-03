#include "pars/Rules/Char.hpp"
#include "pars/Rules/Repeat.hpp"
#include "pars/Utf.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace pars::tests::basic {
struct TextState : QueryState<u8::QueryTextCursor> {
	constexpr explicit TextState(std::u8string_view sv) : QueryState<u8::QueryTextCursor> {sv} {}
};

inline constexpr auto match_text(auto&& rule, std::u8string_view sv) {
	return rule.match(TextState {sv});
}

TEST_CASE("can parse") {
	REQUIRE(match_text(*c('a'), u8"aaa"));
}
}  // namespace pars::tests::basic