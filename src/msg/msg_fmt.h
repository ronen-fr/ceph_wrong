// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
#pragma once

/**
 * \file fmtlib formatters for some msg_types.h classes
 */

#include "msg/msg_types.h"
#include "include/types_fmt.h"
#include <fmt/format.h>

template <> struct fmt::formatter<entity_name_t> {
  constexpr auto parse(format_parse_context& ctx)
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto format(entity_name_t const& addr, FormatContext& ctx)
  {
    if (addr.is_new() || addr.num() < 0) {
      return fmt::format_to(ctx.out(), "{}.?", addr.type_str());
    }
    return fmt::format_to(ctx.out(), "{}.{}", addr.type_str(), addr.num());
  }
};

