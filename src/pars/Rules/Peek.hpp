#pragma once

namespace pars {
template<typename... QueryTs, typename FnT>
inline constexpr auto query_peek(FnT&& fn) {}
}  // namespace pars