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

#ifndef RIPPLE_PROTOCOL_STMPTAMOUNT_H_INCLUDED
#define RIPPLE_PROTOCOL_STMPTAMOUNT_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/basics/LocalValue.h>
#include <ripple/basics/Number.h>
#include <ripple/protocol/MPTIssue.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/json_get_or_throw.h>

namespace ripple {

// Internal form:
// 1: If amount is zero, then value is zero and offset is -100
// 2: Otherwise:
//   legal offset range is -96 to +80 inclusive
//   value range is 10^15 to (10^16 - 1) inclusive
//  amount = value * [10 ^ offset]

// Wire form:
// High 8 bits are (offset+142), legal range is, 80 to 22 inclusive
// Low 56 bits are value, legal range is 10^15 to (10^16 - 1) inclusive
class STMPTAmount final : public STBase, public CountedObject<STMPTAmount>
{
public:
    using value_type = std::uint64_t;

private:
    MPTIssue mIssue;
    value_type mValue;

public:
    static const std::uint64_t cMPToken = 0x2000000000000000ull;
    static const std::uint64_t cValueMask = ~cMPToken;

    //--------------------------------------------------------------------------
    STMPTAmount(SerialIter& sit, SField const& name);

    // Do not call canonicalize
    STMPTAmount(
        SField const& name,
        MPTIssue const& issue,
        value_type value);

    explicit STMPTAmount(SField const& name, STMPTAmount const& amt);

    STMPTAmount(MPTIssue const& issue, std::uint64_t value);

    // Legacy support for new-style amounts
    operator Number() const;

    //--------------------------------------------------------------------------
    //
    // Observers
    //
    //--------------------------------------------------------------------------

    int
    exponent() const noexcept;

    bool
    native() const noexcept;

    bool
    negative() const noexcept;

    std::uint64_t
    mantissa() const noexcept;

    MPTIssue const&
    issue() const;

    AccountID const&
    getIssuer() const;

    int
    signum() const noexcept;

    /** Returns a zero value with the same issuer and currency. */
    STMPTAmount
    zeroed() const;

    void
    setJson(Json::Value&) const;

    STMPTAmount const&
    value() const noexcept;

    //--------------------------------------------------------------------------
    //
    // Operators
    //
    //--------------------------------------------------------------------------

    explicit operator bool() const noexcept;

    STMPTAmount&
    operator+=(STMPTAmount const&);
    STMPTAmount&
    operator-=(STMPTAmount const&);

    STMPTAmount& operator=(beast::Zero);

    STMPTAmount&
    operator=(XRPAmount const& amount);

    //--------------------------------------------------------------------------
    //
    // Modification
    //
    //--------------------------------------------------------------------------

    void
    clear();

    // Zero while copying currency and issuer.
    void
    clear(STMPTAmount const& saTmpl);

    void
    clear(MPTIssue const& issue);

    /** Set the Issue for this amount and update mIsNative. */
    void
    setIssue(MPTIssue const& issue);

    MPT const&
    getAssetID() const;

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

private:
    static std::unique_ptr<STMPTAmount>
    construct(SerialIter&, SField const& name);

    void
    set(std::int64_t v);
    void
    canonicalize();

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    STMPTAmount&
    operator=(IOUAmount const& iou);

    friend class detail::STVar;

    friend STMPTAmount
    operator+(STMPTAmount const& v1, STMPTAmount const& v2);
};

//------------------------------------------------------------------------------
//
// Creation
//
//------------------------------------------------------------------------------

STMPTAmount
mptAmountFromString(MPTIssue const& issue, std::string const& amount);

STMPTAmount
mptAmountFromJson(SField const& name, Json::Value const& v);

bool
amountFromJsonNoThrow(STMPTAmount& result, Json::Value const& jvSource);

// IOUAmount and XRPAmount define toSTMPTAmount, defining this
// trivial conversion here makes writing generic code easier
inline STMPTAmount const&
toSTMPTAmount(STMPTAmount const& a)
{
    return a;
}

//------------------------------------------------------------------------------
//
// Observers
//
//------------------------------------------------------------------------------

inline int
STMPTAmount::exponent() const noexcept
{
    return 0;
}

inline bool
STMPTAmount::native() const noexcept
{
    return false;
}

inline bool
STMPTAmount::negative() const noexcept
{
    return false;
}

inline std::uint64_t
STMPTAmount::mantissa() const noexcept
{
    return mValue;
}

inline MPTIssue const&
STMPTAmount::issue() const
{
    return mIssue;
}

inline AccountID const&
STMPTAmount::getIssuer() const
{
    return mIssue.getAccount();
}

inline int
STMPTAmount::signum() const noexcept
{
    return mValue ? 1 : 0;
}

inline STMPTAmount
STMPTAmount::zeroed() const
{
    return STMPTAmount(mIssue, 0);
}

inline STMPTAmount::operator bool() const noexcept
{
    return *this != beast::zero;
}

inline STMPTAmount::operator Number() const
{
    return mantissa();
}

inline STMPTAmount& STMPTAmount::operator=(beast::Zero)
{
    clear();
    return *this;
}

inline void
STMPTAmount::clear()
{
    // The -100 is used to allow 0 to sort less than a small positive values
    // which have a negative exponent.
    mValue = 0;
}

// Zero while copying currency and issuer.
inline void
STMPTAmount::clear(STMPTAmount const& saTmpl)
{
    clear(saTmpl.mIssue);
}

inline void
STMPTAmount::clear(MPTIssue const& issue)
{
    setIssue(issue);
    clear();
}

inline STMPTAmount const&
STMPTAmount::value() const noexcept
{
    return *this;
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool
operator==(STMPTAmount const& lhs, STMPTAmount const& rhs);
bool
operator<(STMPTAmount const& lhs, STMPTAmount const& rhs);

inline bool
operator!=(STMPTAmount const& lhs, STMPTAmount const& rhs)
{
    return !(lhs == rhs);
}

inline bool
operator>(STMPTAmount const& lhs, STMPTAmount const& rhs)
{
    return rhs < lhs;
}

inline bool
operator<=(STMPTAmount const& lhs, STMPTAmount const& rhs)
{
    return !(rhs < lhs);
}

inline bool
operator>=(STMPTAmount const& lhs, STMPTAmount const& rhs)
{
    return !(lhs < rhs);
}

STMPTAmount
operator-(STMPTAmount const& value);

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

STMPTAmount
operator+(STMPTAmount const& v1, STMPTAmount const& v2);
STMPTAmount
operator-(STMPTAmount const& v1, STMPTAmount const& v2);

// Someone is offering X for Y, what is the rate?
// Rate: smaller is better, the taker wants the most out: in/out
// VFALCO TODO Return a Quality object
std::uint64_t
getRate(STMPTAmount const& offerOut, STMPTAmount const& offerIn);

//------------------------------------------------------------------------------

inline bool
isXRP(STMPTAmount const& amount)
{
    return false;
}

// Since `canonicalize` does not have access to a ledger, this is needed to put
// the low-level routine stAmountCanonicalize on an amendment switch. Only
// transactions need to use this switchover. Outside of a transaction it's safe
// to unconditionally use the new behavior.

/** RAII class to set and restore the STMPTAmount canonicalize switchover.
 */

}  // namespace ripple

#endif
