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

#ifndef RIPPLE_PROTOCOL_STVARIANT_H_INCLUDED
#define RIPPLE_PROTOCOL_STVARIANT_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <type_traits>
#include <variant>

namespace ripple {
namespace detail {

template<typename T, typename... Ts>
constexpr bool contains() { return std::disjunction_v<std::is_same<T, Ts>...>; }

template <typename T, typename... Ts>
concept ValidAlts = contains<T, Ts ...>();

} // namespace detail

template <typename ... Alts>
class STVariant : public STBase, CountedObject<STVariant<Alts ...>> {
    std::variant<Alts ...> alternatives_;
    SerializedTypeID const sid_;
public:
    template <detail::ValidAlts T>
    STVariant(SField const& name, T const& arg);
    STVariant() = default;
    explicit STVariant(SField const& name);
    explicit STVariant(SerialIter& sit, SField const& name);

    template <detail::ValidAlts T>
    T const&
    get() const;

    template <detail::ValidAlts T>
    T&
    get() const;

    // STBase
    SerializedTypeID
    getSType() const override;

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
    static std::unique_ptr<STCurrency>
    construct(SerialIter&, SField const& name);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

template <typename ... Alts>
template <detail::ValidAlts T>
STVariant<Alts ...>::STVariant(SField const& name, T const& arg)
: STBase(name)
, alternatives_(arg)
{
}

template <typename ... Alts>
STVariant<Alts ...>::STVariant(SField const& name)
: STBase(name)
{
}

template <typename ... Alts>
STVariant<Alts ...>::STVariant(SerialIter& sit, SField const& name)
: STBase(name)
{
    std::visit([&]<typename A>(A&& a) {
        alternatives_ =
    }, alternatives_);
}

template <typename ... Alts>
template <detail::ValidAlts T>
T const&
STVariant<Alts ...>::get() const
{
}

template <typename ... Alts>
template <detail::ValidAlts T>
T&
STVariant<Alts ...>::get() const
{
}

// STBase
template <typename ... Alts>
SerializedTypeID
STVariant<Alts ...>::getSType() const
{
}

template <typename ... Alts>
std::string
STVariant<Alts ...>::getText() const
{
}

template <typename ... Alts>
Json::Value STVariant<Alts ...>::getJson(JsonOptions) const
{
}

template <typename ... Alts>
void
STVariant<Alts ...>::add(Serializer& s) const
{
}

template <typename ... Alts>
bool
STVariant<Alts...>::isEquivalent(const STBase& t) const
{
}

template <typename ... Alts>
bool
STVariant<Alts ...>::isDefault() const
{
}

template <typename ... Alts>
std::unique_ptr<STCurrency>
STVariant<Alts ...>::construct(SerialIter&, SField const& name)
{
}

template <typename ... Alts>
STBase*
STVariant<Alts ...>::copy(std::size_t n, void* buf) const
{
}

template <typename ... Alts>
STBase*
STVariant<Alts ...>::move(std::size_t n, void* buf)
{
}

} // namespace ripple

#endif // RIPPLE_PROTOCOL_STVARIANT_H_INCLUDED
