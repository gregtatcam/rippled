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

#ifndef RIPPLE_PROTOCOL_STEITHERAMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_STEITHERAMOUNT_H_INCLUDED

#include <xrpl/protocol/CommonConstraints.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STMPTAmount.h>

namespace ripple {

// Currency or MPT issuance ID
template <typename T>
concept ValidAssetType =
    std::is_same_v<T, Currency> || std::is_same_v<T, uint192>;

template <typename T>
concept ValidSTEitherAmountType = std::is_same_v<T, STEitherAmount> ||
    std::is_same_v<T, std::optional<STEitherAmount>>;

class STEitherAmount : public STBase, public CountedObject<STEitherAmount>
{
private:
    std::variant<STAmount, STMPTAmount> amount_;

public:
    using value_type = STEitherAmount;
    STEitherAmount() = default;
    STEitherAmount(SerialIter& sit, SField const& name);
    STEitherAmount(XRPAmount const& amount);
    STEitherAmount(STAmount const& amount);
    STEitherAmount(SField const& name, STAmount const& amount = STAmount{});
    STEitherAmount(SField const& name, STMPTAmount const& amount);
    STEitherAmount(STMPTAmount const& amount);

    STEitherAmount&
    operator=(STAmount const&);
    STEitherAmount&
    operator=(STMPTAmount const&);
    STEitherAmount&
    operator=(XRPAmount const&);

    SerializedTypeID
    getSType() const override;

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    setJson(Json::Value&) const;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    //------------------------------------------------------------------------------

    bool
    isMPT() const;

    bool
    isIssue() const;

    STEitherAmount const&
    value() const;

    std::variant<STAmount, STMPTAmount> const&
    getValue() const;

    std::variant<STAmount, STMPTAmount>&
    getValue();

    AccountID
    getIssuer() const;

    bool
    negative() const;

    bool
    native() const;

    STEitherAmount
    zeroed() const;

    int
    signum() const noexcept;

    template <ValidSerialAmountType T>
    T const&
    get() const;

    template <ValidSerialAmountType T>
    T&
    get();

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;
};

template <ValidSerialAmountType T>
T const&
STEitherAmount::get() const
{
    if (std::holds_alternative<T>(amount_))
        return std::get<T>(amount_);
    Throw<std::logic_error>("Invalid STEitherAmount conversion");
}

template <ValidSerialAmountType T>
T&
STEitherAmount::get()
{
    if (std::holds_alternative<T>(amount_))
        return std::get<T>(amount_);
    Throw<std::logic_error>("Invalid STEitherAmount conversion");
}

template <ValidSerialAmountType T>
decltype(auto)
get(auto&& amount)
{
    using TAmnt = std::decay_t<decltype(amount)>;
    if constexpr (std::is_same_v<TAmnt, STEitherAmount>)
    {
        if constexpr (std::is_lvalue_reference_v<decltype(amount)>)
            return amount.template get<T>();
        else
            return T{amount.template get<T>()};
    }
    else if constexpr (std::is_same_v<TAmnt, std::optional<STEitherAmount>>)
    {
        static std::optional<T> t;
        if (amount.has_value())
            return std::make_optional(amount->template get<T>());
        return t;
    }
    else if constexpr (std::is_convertible_v<TAmnt, STEitherAmount>)
    {
        if constexpr (std::is_lvalue_reference_v<decltype(amount)>)
            return amount.operator STEitherAmount().template get<T>();
        else
            return T{amount.operator STEitherAmount().template get<T>()};
    }
    else
    {
        bool const alwaysFalse = !std::is_same_v<T, T>;
        static_assert(alwaysFalse, "Invalid STEitherAmount conversion");
    }
}

template <ValidIssueType Iss>
Iss const&
get(STEitherAmount const& amount)
{
    if constexpr (std::is_same_v<Iss, Issue>)
        return get<STAmount>(amount).issue();
    else
        return get<STMPTAmount>(amount).issue();
}

STEitherAmount
amountFromJson(SField const& name, Json::Value const& v);

STAmount
amountFromJson(SF_AMOUNT const& name, Json::Value const& v);

bool
amountFromJsonNoThrow(STEitherAmount& result, Json::Value const& jvSource);

bool
amountFromJsonNoThrow(STAmount& result, Json::Value const& jvSource);

inline bool
operator==(STEitherAmount const& lhs, STEitherAmount const& rhs)
{
    return std::visit(
        [&]<typename T1, typename T2>(T1 const& a1, T2 const& a2) {
            if constexpr (std::is_same_v<T1, T2>)
                return a1 == a2;
            else
                return false;
        },
        lhs.getValue(),
        rhs.getValue());
}

inline bool
operator!=(STEitherAmount const& lhs, STEitherAmount const& rhs)
{
    return !operator==(lhs, rhs);
}

template <ValidSerialAmountType T>
bool
isMPT(T const& amount)
{
    if constexpr (std::is_same_v<T, STMPTAmount>)
        return true;
    else if constexpr (std::is_same_v<T, STAmount>)
        return false;
}

template <ValidSTEitherAmountType T>
bool
isMPT(T const& amount)
{
    if constexpr (std::is_same_v<T, STEitherAmount>)
        return amount.isMPT();
    else
        return amount && amount->isMPT();
}

template <ValidSerialAmountType T>
bool
isIssue(T const& amount)
{
    return !isMPT(amount);
}

inline constexpr bool
isXRP(STEitherAmount const& amount)
{
    if (amount.isIssue())
        return isXRP(get<STAmount>(amount));
    return false;
}

template <ValidSerialAmountType T>
bool
isNative(T const& amount)
{
    if constexpr (std::is_same_v<T, STAmount>)
        return amount.native();
    else
        return false;
}

template <ValidAssetType A1, ValidAssetType A2>
bool
sameAsset(A1 const& a1, A2 const& a2)
{
    if constexpr (std::is_same_v<A1, A2>)
        return a1 == a2;
    else
        return false;
}

template <ValidIssueType I1, ValidIssueType I2>
bool
sameAsset(I1 const& i1, I2 const& i2)
{
    if constexpr (std::is_same_v<I1, I2>)
        return i1 == i2;
    else
        return false;
}

struct BadAsset
{
    BadAsset()
    {
    }
    bool
    operator==(Currency const& c) const
    {
        return badCurrency() == c;
    }
    bool
    operator==(uint192 const& mpt) const
    {
        return noMPT() == mpt;
    }
};

BadAsset const&
badAsset()
{
    static BadAsset badAsset;
    return badAsset;
}

// clang-format off
template <typename A1, typename A2>
    requires(
        (std::is_same_v<A1, STMPTAmount> && std::is_same_v<A2, STAmount>) ||
        (std::is_same_v<A1, STAmount> && std::is_same_v<A2, STMPTAmount>) ||
        (std::is_same_v<A1, STMPTAmount> && std::is_same_v<A2, STMPTAmount>))
std::uint64_t getRate(A1 const& offerOut, A2 const& offerIn)
{
    if constexpr (
        std::is_same_v<A1, STMPTAmount> && std::is_same_v<A2, STAmount>)
        return getRate(STAmount{noIssue(), offerOut.value(), 0}, offerIn);
    else if constexpr (
        std::is_same_v<A1, STAmount> && std::is_same_v<A2, STMPTAmount>)
        return getRate(offerOut, STAmount{noIssue(), offerIn.value(), 0});
    else
        return getRate(
            STAmount{noIssue(), offerOut.value(), 0},
            STAmount{noIssue(), offerIn.value(), 0});
}
// clang-format on

template <typename T1, typename T2, ValidIssueType Iss>
    requires ValidAmountIssueComboType<T1, T2, Iss> decltype(auto)
divide(T1 const& num, T2 const& den, Iss const& issue)
{
    if constexpr (std::is_same_v<Iss, Issue>)
        return toAmount<STAmount>(issue, num / den);
    else
        return toAmount<STMPTAmount>(issue, num / den);
}

template <typename T1, typename T2, ValidIssueType Iss>
    requires ValidAmountIssueComboType<T1, T2, Iss> decltype(auto)
multiply(T1 const& v1, T2 const& v2, Iss const& issue)
{
    if constexpr (std::is_same_v<Iss, Issue>)
        return toAmount<STAmount>(issue, v1 * v2);
    else
        return toAmount<STMPTAmount>(issue, v1 * v2);
}

}  // namespace ripple

//------------------------------------------------------------------------------
namespace Json {
template <>
inline ripple::STAmount
getOrThrow(Json::Value const& v, ripple::SField const& field)
{
    using namespace ripple;
    Json::StaticString const& key = field.getJsonName();
    if (!v.isMember(key))
        Throw<JsonMissingKeyError>(key);
    Json::Value const& inner = v[key];
    return get<STAmount>(amountFromJson(field, inner));
}

}  // namespace Json

#endif  // RIPPLE_PROTOCOL_STEITHERAMOUNT_H_INCLUDED
