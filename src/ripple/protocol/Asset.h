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

#ifndef RIPPLE_PROTOCOL_ASSET_H_INCLUDED
#define RIPPLE_PROTOCOL_ASSET_H_INCLUDED

#include <ripple/protocol/Serializer.h>
#include <ripple/protocol/UintTypes.h>

#include <variant>

namespace ripple {

class Asset
{
    inline static MPT none = noMPT();
    using asset_type = std::variant<Currency, MPT>;

private:
    asset_type asset_;

public:
    Asset() : asset_(Currency{beast::zero})
    {
    }
    Asset(Currency const& c) : asset_(c)
    {
    }
    Asset(MPT const& u) : asset_(u)
    {
    }
    Asset&
    operator=(Currency const& c)
    {
        asset_ = c;
        return *this;
    }
    Asset&
    operator=(MPT const& u)
    {
        asset_ = u;
        return *this;
    }

    asset_type constexpr const&
    asset() const
    {
        return asset_;
    }

    bool constexpr isMPT() const
    {
        return std::holds_alternative<MPT>(asset_);
    }
    bool constexpr isCurrency() const
    {
        return std::holds_alternative<Currency>(asset_);
    }
    bool constexpr isXRP() const
    {
        return isCurrency() && ripple::isXRP(std::get<Currency>(asset_));
    }

    void
    addBitString(Serializer& s) const;

    bool
    empty() const
    {
        return std::holds_alternative<MPT>(asset_) &&
            std::get<MPT>(asset_) == none;
    }

    template <typename Hasher>
    friend void
    hash_append(Hasher& h, Asset const& a)
    {
        std::visit([&h](auto&& arg) { hash_append(h, arg); }, a.asset_);
    }

    template <typename T>
    requires(std::is_same_v<T, Currency> || std::is_same_v<T, MPT>)
        T const* get() const
    {
        return std::get_if<T>(asset_);
    }

    Currency const&
    currency() const
    {
        return operator const Currency&();
    }

    MPT const&
    mpt() const
    {
        return operator const MPT&();
    }

    operator Currency const&() const;

    operator MPT const&() const;

    friend constexpr bool
    comparable(Asset const& a1, Asset const& a2)
    {
        return std::holds_alternative<Currency>(a1.asset_) ==
            std::holds_alternative<Currency>(a2.asset_);
    }
    friend constexpr bool
    operator==(Currency const& c, Asset const& a) noexcept
    {
        return a.isCurrency() && c == (Currency&)a;
    }
    friend constexpr bool
    operator==(Asset const& a, Currency const& c) noexcept
    {
        return c == a;
    }
    friend constexpr bool
    operator==(Asset const& a1, Asset const& a2) noexcept
    {
        return comparable(a1, a2) && a1.asset_ == a2.asset_;
    }
    friend constexpr bool
    operator!=(Asset const& a1, Asset const& a2) noexcept
    {
        return !(a1 == a2);
    }
    friend constexpr bool
    operator<(Asset const& a1, Asset const& a2) noexcept
    {
        return comparable(a1, a2) && a1.asset_ < a2.asset_;
    }
    friend constexpr bool
    operator>(Asset const& a1, Asset const& a2) noexcept
    {
        return a2 < a1;
    }
    friend constexpr bool
    operator<=(Asset const& a1, Asset const& a2) noexcept
    {
        return !(a2 < a1);
    }
    friend constexpr bool
    operator>=(Asset const& a1, Asset const& a2) noexcept
    {
        return !(a1 < a2);
    }
    friend constexpr std::weak_ordering
    operator<=>(Asset const& lhs, Asset const& rhs);

    friend std::string
    to_string(Asset const& a);

    friend bool
    isXRP(Asset const& a)
    {
        return !a.empty() && a.isXRP();
    }
    friend std::ostream&
    operator<<(std::ostream& stream, Asset const& a)
    {
        stream << to_string(a);
        return stream;
    }
};

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_ASSET_H_INCLUDED
