//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_PROTOCOL_COMMONCONSTRAINTS_H_INCLUDED
#define RIPPLE_PROTOCOL_COMMONCONSTRAINTS_H_INCLUDED

#include <type_traits>

namespace ripple {

class STAmount;
class STMPTAmount;
class Issue;
class MPTIssue;

// clang-format off
template <typename TAmnt>
concept ValidSerialAmountType =
    std::is_same_v<TAmnt, STAmount> || std::is_same_v<TAmnt, STMPTAmount>;

template <typename Iss>
concept ValidIssueType =
    std::is_same_v<Iss, Issue> || std::is_same_v<Iss, MPTIssue>;

template <typename Amnt1, typename Amnt2, typename Iss>
concept ValidAmountIssueComboType =
    (std::is_same_v<Amnt1, STMPTAmount> || std::is_same_v<Amnt2, STMPTAmount> ||
     (std::is_same_v<Amnt1, STAmount> && std::is_same_v<Amnt2, STAmount> &&
      std::is_same_v<Iss, MPTIssue>));
// clang-format on

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_COMMONCONSTRAINTS_H_INCLUDED
