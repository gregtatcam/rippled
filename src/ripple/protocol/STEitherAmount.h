//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_STAEITHERMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_STAEITHERMOUNT_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/LocalValue.h>
#include <ripple/basics/Number.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STMPTAmount.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/json_get_or_throw.h>

namespace ripple {

// Internal form:
class STEitherAmount final : public STBase, public CountedObject<STEitherAmount>
{
private:
    std::variant<STAmount, STMPTAmount> amount_;

public:
    using value_type = STEitherAmount;

    //--------------------------------------------------------------------------
    STEitherAmount() = default;

    STEitherAmount(SerialIter& sit, SField const& name);

    // Do not call canonicalize
    STEitherAmount(STAmount const& amount);
    STEitherAmount(XRPAmount const& amount) : STEitherAmount(STAmount{amount}) {}

    STEitherAmount(STMPTAmount const& amount);

    operator Number() const;

    operator STAmount const&() const;
    operator STAmount&();
    operator STMPTAmount const&() const;
    operator STMPTAmount&();

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    STEitherAmount const&
    value() const noexcept;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    explicit operator bool() const noexcept;

    /*bool operator == (STAmount const&) const;
    bool operator == (STMPTAmount const&) const;*/
    bool operator == (STEitherAmount const&) const;

    //--------------------------------------------------------------------------
    //
    // Modification
    //
    //--------------------------------------------------------------------------

    //--------------------------------------------------------------------------
    //
    // STBase
    //
    //--------------------------------------------------------------------------

    SerializedTypeID
    getSType() const override;

    std::string
    getFullText() const override;

    std::string
    getText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    void
    setJson(Json::Value&) const;

    bool
    native() const;

    bool
    negative() const;

    friend bool isXRP(STEitherAmount const& amount);

    int
    signum() const noexcept;

    bool
    isSTAmount() const;
    bool
    isMPTAmount() const;

    // temp to make it build
    XRPAmount
    xrp() const;
    IOUAmount
    iou() const;

    std::uint64_t
    mantissa() const;
    int
    exponent() const;

    std::variant<STAmount, STMPTAmount> const&
    amount() const;

    // temp to make it build
    STEitherAmount&
    operator+=(STEitherAmount const&) { return *this; }
    STEitherAmount&
    operator-=(STEitherAmount const&) { return *this; }
    friend STEitherAmount
    operator+(STEitherAmount const&, STEitherAmount const&) { return STEitherAmount{}; }
    friend STEitherAmount
    operator-(STEitherAmount const&, STEitherAmount const&) { return STEitherAmount{}; }

private:
    static std::unique_ptr<STEitherAmount>
    construct(SerialIter&, SField const& name);
};

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

STEitherAmount
eitherAmountFromJson(SField const& name, Json::Value const& v);

bool
amountFromJsonNoThrow(STEitherAmount& result, Json::Value const& jvSource);

// IOUAmount and XRPAmount define toSTEitherAmount, defining this
// trivial conversion here makes writing generic code easier
inline STEitherAmount const&
toSTEitherAmount(STEitherAmount const& a)
{
    return a;
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

inline int
STEitherAmount::signum() const noexcept
{
    int ret = 0;
    std::visit([&](auto&& a) {
        ret = a.signum();
    }, amount_);
    return ret;
}

inline bool STEitherAmount::native() const
{
    return std::holds_alternative<STAmount>(amount_) &&
        std::get<STAmount>(amount_).native();
}

inline bool STEitherAmount::negative() const
{
    bool ret;
    std::visit([&](auto&& a) {
        ret = a.negative();
    }, amount_);
    return ret;
}

inline STEitherAmount::operator bool() const noexcept
{
    if (std::holds_alternative<STAmount>(amount_))
        return std::get<STAmount>(amount_) != beast::zero ;
    return std::get<STMPTAmount>(amount_) != beast::zero;
}

inline STEitherAmount::operator Number() const
{
    if (std::holds_alternative<STAmount>(amount_))
        return static_cast<Number>(std::get<STAmount>(amount_));
    return static_cast<Number>(std::get<STMPTAmount>(amount_));
}

inline STEitherAmount::operator STAmount const&() const
{
    if (!std::holds_alternative<STAmount>(amount_))
        Throw<std::runtime_error>("STEitherAmount is not STAmount");
    return std::get<STAmount>(amount_);
}

inline STEitherAmount::operator STAmount&()
{
    if (!std::holds_alternative<STAmount>(amount_))
        Throw<std::runtime_error>("STEitherAmount is not STAmount");
    return std::get<STAmount>(amount_);
}

inline STEitherAmount::operator STMPTAmount const&() const
{
    if (!std::holds_alternative<STMPTAmount>(amount_))
        Throw<std::runtime_error>("STEitherAmount is not STMPTAmount");
    return std::get<STMPTAmount>(amount_);
}

inline STEitherAmount::operator STMPTAmount&()
{
    if (!std::holds_alternative<STMPTAmount>(amount_))
        Throw<std::runtime_error>("STEitherAmount is not STMPTAmount");
    return std::get<STMPTAmount>(amount_);
}

/*inline bool STEitherAmount::operator == (STAmount const& amt) const
{
    return std::holds_alternative<STAmount>(amount_) &&
        std::get<STAmount>(amount_) == amt;
}

inline bool STEitherAmount::operator == (STMPTAmount const& amt) const
{
    return std::holds_alternative<STMPTAmount>(amount_) &&
           std::get<STMPTAmount>(amount_) == amt;
}*/

inline bool STEitherAmount::operator == (STEitherAmount const& amt) const
{
    bool ret = false;
    std::visit([&]<typename T1, typename T2>(T1&& a1, T2&& a2) {
        if constexpr (std::is_same_v<T1, T2>)
            ret = a1 == a2;
    }, amount_, amt.amount_);
    return ret;
}

inline STEitherAmount const&
STEitherAmount::value() const noexcept
{
    return *this;
}

inline bool
STEitherAmount::isSTAmount() const
{
    return std::holds_alternative<STAmount>(amount_);
}

inline bool
STEitherAmount::isMPTAmount() const
{
    return std::holds_alternative<STMPTAmount>(amount_);
}

inline std::variant<STAmount, STMPTAmount> const&
STEitherAmount::amount() const
{
    return amount_;
}

inline bool
isXRP(STEitherAmount const& amount)
{
    return std::holds_alternative<STAmount>(amount.amount_) &&
        isXRP(std::get<STAmount>(amount.amount_).issue().currency);
}

// Note that this could be any combination of IOU/IOU, IOU/MPT, MPT/MPT
inline std::uint64_t
getRate(STEitherAmount const& offerOut, STEitherAmount const& offerIn)
{
    return 0;
}

inline STEitherAmount
multiply(STEitherAmount const& v1, STEitherAmount const& v2, std::variant<Issue, MPTIssue> const& issue)
{
    return STEitherAmount{};
}

inline STEitherAmount
divide(STEitherAmount const& v1, STEitherAmount const& v2, std::variant<Issue, MPTIssue> const& issue)
{
    return STEitherAmount{};
}

}  // namespace ripple

#endif
