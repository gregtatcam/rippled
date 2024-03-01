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

#ifndef RIPPLE_PATH_IMPL_AMOUNTSPEC_H_INCLUDED
#define RIPPLE_PATH_IMPL_AMOUNTSPEC_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/STAmount.h>

#include <optional>

namespace ripple {

struct AmountSpec
{
    explicit AmountSpec() = default;

    std::variant<XRPAmount, IOUAmount, MPTAmount> amount;
    std::optional<AccountID> issuer;
    std::optional<Currency> currency;
    std::optional<MPT> mptid;

    bool
    native() const
    {
        return std::holds_alternative<XRPAmount>(amount);
    }
    bool
    isIOU() const
    {
        return std::holds_alternative<IOUAmount>(amount);
    }
    template <typename T>
    void
    check() const
    {
        if (!std::holds_alternative<T>(amount))
            Throw<std::logic_error>("AmountSpec doesn't hold requested amount");
    }
    XRPAmount const&
    xrp() const
    {
        check<XRPAmount>();
        return std::get<XRPAmount>(amount);
    }
    IOUAmount const&
    iou() const
    {
        check<XRPAmount>();
        return std::get<IOUAmount>(amount);
    }
    MPTAmount const&
    mpt() const
    {
        check<XRPAmount>();
        return std::get<MPTAmount>(amount);
    }

    friend std::ostream&
    operator<<(std::ostream& stream, AmountSpec const& amt)
    {
        if (std::holds_alternative<MPTAmount>(amt.amount))
            stream << to_string(*amt.mptid);
        else if (amt.native())
            stream << to_string(amt.xrp());
        else
            stream << to_string(amt.iou());
        if (amt.currency)
            stream << "/(" << *amt.currency << ")";
        if (amt.issuer)
            stream << "/" << *amt.issuer << "";
        return stream;
    }
};

struct EitherAmount
{
    std::variant<XRPAmount, IOUAmount, MPTAmount> amount;

    EitherAmount() = default;

    explicit EitherAmount(IOUAmount const& a) : amount(a)
    {
    }

    explicit EitherAmount(XRPAmount const& a) : amount(a)
    {
    }

    explicit EitherAmount(MPTAmount const& a) : amount(a)
    {
    }

    explicit EitherAmount(AmountSpec const& a)
    {
        amount = a.amount;
    }

    bool
    native() const
    {
        return std::holds_alternative<XRPAmount>(amount);
    }
    bool
    isIOU() const
    {
        return std::holds_alternative<IOUAmount>(amount);
    }
    bool
    isMPT() const
    {
        return std::holds_alternative<MPTAmount>(amount);
    }
    template <typename T>
    void
    check() const
    {
        if (!std::holds_alternative<T>(amount))
            Throw<std::logic_error>(
                "EitherAmount doesn't hold requested amount");
    }
    XRPAmount const&
    xrp() const
    {
        check<XRPAmount>();
        return std::get<XRPAmount>(amount);
    }
    IOUAmount const&
    iou() const
    {
        check<IOUAmount>();
        return std::get<IOUAmount>(amount);
    }
    MPTAmount const&
    mpt() const
    {
        check<MPTAmount>();
        return std::get<MPTAmount>(amount);
    }

#ifndef NDEBUG
    friend std::ostream&
    operator<<(std::ostream& stream, EitherAmount const& amt)
    {
        if (amt.native())
            stream << to_string(amt.xrp());
        else if (amt.isIOU())
            stream << to_string(amt.iou());
        else
            stream << to_string(amt.mpt());
        return stream;
    }
#endif
};

template <class T>
T const&
get(EitherAmount& amt)
{
    static_assert(sizeof(T) == -1, "Must used specialized function");
    return T(0);
}

template <>
inline IOUAmount const&
get<IOUAmount>(EitherAmount& amt)
{
    assert(amt.isIOU());
    return amt.iou();
}

template <>
inline XRPAmount const&
get<XRPAmount>(EitherAmount& amt)
{
    assert(amt.native());
    return amt.xrp();
}

template <>
inline MPTAmount const&
get<MPTAmount>(EitherAmount& amt)
{
    assert(amt.isMPT());
    return amt.mpt();
}

template <class T>
T const&
get(EitherAmount const& amt)
{
    static_assert(sizeof(T) == -1, "Must used specialized function");
    return T(0);
}

template <>
inline IOUAmount const&
get<IOUAmount>(EitherAmount const& amt)
{
    assert(!amt.native());
    return amt.iou();
}

template <>
inline XRPAmount const&
get<XRPAmount>(EitherAmount const& amt)
{
    assert(amt.native());
    return amt.xrp();
}

template <>
inline MPTAmount const&
get<MPTAmount>(EitherAmount const& amt)
{
    assert(amt.isMPT());
    return amt.mpt();
}

inline AmountSpec
toAmountSpec(STAmount const& amt)
{
    assert(amt.mantissa() < std::numeric_limits<std::int64_t>::max());
    bool const isNeg = amt.negative();
    std::int64_t const sMant =
        isNeg ? -std::int64_t(amt.mantissa()) : amt.mantissa();
    AmountSpec result;

    if (amt.isIssue() && isXRP(amt.issue()))
    {
        result.amount = XRPAmount(sMant);
    }
    else if (amt.isMPT())
    {
        result.mptid = amt.mptIssue().mpt();
        result.amount = amt.mpt();
    }
    else
    {
        result.amount = IOUAmount(sMant, amt.exponent());
        result.issuer = amt.issue().account;
        result.currency = amt.issue().currency;
    }

    return result;
}

inline EitherAmount
toEitherAmount(STAmount const& amt)
{
    if (isXRP(amt))
        return EitherAmount{amt.xrp()};
    else if (amt.isIssue())
        return EitherAmount{amt.iou()};
    return EitherAmount(amt.mpt());
}

inline AmountSpec
toAmountSpec(EitherAmount const& ea, std::optional<Currency> const& c)
{
    AmountSpec r;
    r.currency = c;
    assert(ea.native() == r.native());
    r.amount = ea.amount;
    return r;
}

}  // namespace ripple

#endif
