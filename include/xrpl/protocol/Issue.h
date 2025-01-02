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

#ifndef RIPPLE_PROTOCOL_ISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_ISSUE_H_INCLUDED

#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/IOUIssue.h>
#include <xrpl/protocol/MPTIssue.h>

namespace ripple {

template <typename T>
    requires(
        std::is_same_v<T, XRPAmount> || std::is_same_v<T, IOUAmount> ||
        std::is_same_v<T, MPTAmount>)
struct AmountType
{
    using amount_type = T;
};

/* Issue is an abstraction of three different issue types: XRP, IOU, MPT.
 * For historical reasons, two issue types XRP and IOU are wrapped in Issue
 * type. Many functions and classes there were first written for Issue
 * have been rewritten for Issue.
 */
class Issue
{
public:
    using value_type = std::variant<IOUIssue, MPTIssue>;

private:
    value_type issue_;

public:
    Issue() = default;

    /** Conversions to Issue are implicit and conversions to specific issue
     *  type are explicit. This design facilitates the use of Issue.
     */
    Issue(IOUIssue const& issue) : issue_(issue)
    {
    }

    Issue(MPTIssue const& mptIssue) : issue_(mptIssue)
    {
    }

    Issue(MPTID const& issuanceID) : issue_(MPTIssue{issuanceID})
    {
    }

    explicit
    operator IOUIssue() const;

    explicit
    operator MPTIssue() const;

    AccountID const&
    getIssuer() const;

    template <ValidIssueType TIss>
    constexpr TIss const&
    get() const;

    template <ValidIssueType TIss>
    TIss&
    get();

    template <ValidIssueType TIss>
    constexpr bool
    holds() const;

    std::string
    getText() const;

    constexpr value_type const&
    value() const;

    void
    setJson(Json::Value& jv) const;

    bool
    native() const
    {
        return holds<IOUIssue>() && get<IOUIssue>().native();
    }

    std::variant<
        AmountType<XRPAmount>,
        AmountType<IOUAmount>,
        AmountType<MPTAmount>>
    getAmountType() const;

    friend constexpr bool
    operator==(Issue const& lhs, Issue const& rhs);

    friend constexpr std::weak_ordering
    operator<=>(Issue const& lhs, Issue const& rhs);

    friend constexpr bool
    operator==(Currency const& lhs, Issue const& rhs);

    /** Return true if both assets refer to the same currency (regardless of
     * issuer) or MPT issuance. Otherwise return false.
     */
    friend constexpr bool
    equalTokens(Issue const& lhs, Issue const& rhs);
};

template <ValidIssueType TIss>
constexpr bool
Issue::holds() const
{
    return std::holds_alternative<TIss>(issue_);
}

template <ValidIssueType TIss>
constexpr TIss const&
Issue::get() const
{
    if (!std::holds_alternative<TIss>(issue_))
        Throw<std::logic_error>("Issue is not a requested issue");
    return std::get<TIss>(issue_);
}

template <ValidIssueType TIss>
TIss&
Issue::get()
{
    if (!std::holds_alternative<TIss>(issue_))
        Throw<std::logic_error>("Issue is not a requested issue");
    return std::get<TIss>(issue_);
}

constexpr Issue::value_type const&
Issue::value() const
{
    return issue_;
}

constexpr bool
operator==(Issue const& lhs, Issue const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(
            TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
                return issLhs == issRhs;
            else
                return false;
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr std::weak_ordering
operator<=>(Issue const& lhs, Issue const& rhs)
{
    return std::visit(
        []<ValidIssueType TLhs, ValidIssueType TRhs>(
            TLhs const& lhs_, TRhs const& rhs_) {
            if constexpr (std::is_same_v<TLhs, TRhs>)
                return std::weak_ordering(lhs_ <=> rhs_);
            else if constexpr (
                std::is_same_v<TLhs, IOUIssue> &&
                std::is_same_v<TRhs, MPTIssue>)
                return std::weak_ordering::greater;
            else
                return std::weak_ordering::less;
        },
        lhs.issue_,
        rhs.issue_);
}

constexpr bool
operator==(Currency const& lhs, Issue const& rhs)
{
    return rhs.holds<IOUIssue>() && rhs.get<IOUIssue>().getCurrency() == lhs;
}

constexpr bool
equalTokens(Issue const& lhs, Issue const& rhs)
{
    return std::visit(
        [&]<typename TLhs, typename TRhs>(
            TLhs const& issLhs, TRhs const& issRhs) {
            if constexpr (
                std::is_same_v<TLhs, IOUIssue> &&
                std::is_same_v<TRhs, IOUIssue>)
                return issLhs.getCurrency() == issRhs.getCurrency();
            else if constexpr (
                std::is_same_v<TLhs, MPTIssue> &&
                std::is_same_v<TRhs, MPTIssue>)
                return issLhs.getMptID() == issRhs.getMptID();
            else
                return false;
        },
        lhs.issue_,
        rhs.issue_);
}

inline bool
isXRP(Issue const& asset)
{
    return asset.native();
}

std::string
to_string(Issue const& asset);

bool
validJSONAsset(Json::Value const& jv);

Issue
issueFromJson(Json::Value const& jv);

Json::Value
to_json(Issue const& asset);

inline bool
isConsistent(Issue const& issue)
{
    return std::visit(
        [&]<typename TIss>(TIss const& issue_) {
            if constexpr (std::is_same_v<TIss, IOUIssue>)
                return isConsistent(issue_);
            else
                return true;
        },
        issue.value());
}

inline bool
validAsset(Issue const& issue)
{
    return std::visit(
        [&]<typename TIss>(TIss const& issue_) {
            if constexpr (std::is_same_v<TIss, IOUIssue>)
                return isConsistent(issue_) &&
                    issue_.getCurrency() != badCurrency();
            else
                return true;
        },
        issue.value());
}

template <class Hasher>
void
hash_append(Hasher& h, Issue const& r)
{
    using beast::hash_append;
    std::visit(
        [&]<ValidIssueType TIss>(TIss const& issue) {
            if constexpr (std::is_same_v<TIss, IOUIssue>)
                hash_append(h, issue);
            else
                hash_append(h, issue);
        },
        r.value());
}

std::ostream&
operator<<(std::ostream& os, Issue const& x);

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ISSUE_H_INCLUDED
