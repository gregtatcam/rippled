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

template <typename T, typename... Ts>
constexpr bool
contains()
{
    return std::disjunction_v<std::is_same<T, Ts>...>;
}

template <typename T, typename... Ts>
concept ValidAlts = requires()
{
    contains<T, Ts...>();
};

}  // namespace detail

class VariantBase
{
protected:
    VariantBase() = default;
    ~VariantBase() = default;
    VariantBase(VariantBase const&) = default;
    VariantBase&
    operator=(VariantBase const&) = default;
    virtual void
    decode(SerialIter& sit) = 0;
};

template <typename... Alts>
class STVariant : public STBase,
                  public CountedObject<STVariant<Alts...>>,
                  public VariantBase
{
protected:
    std::variant<Alts...> alternatives_;

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
    get();

    // STBase

    std::string
    getText() const override;

    std::string
    getFullText() const override;

    Json::Value getJson(JsonOptions) const override;

    void
    add(Serializer& s) const override;

    bool
    isDefault() const override;

    //---------------------------------

    void
    setJson(Json::Value&) const;

    auto const&
    getValue() const;

    auto&
    getValue();

private:
    friend class detail::STVar;
};

template <typename... Alts>
template <detail::ValidAlts T>
STVariant<Alts...>::STVariant(SField const& name, T const& arg)
    : STBase(name), alternatives_(arg)
{
}

template <typename... Alts>
STVariant<Alts...>::STVariant(SField const& name) : STBase(name)
{
}

template <typename... Alts>
STVariant<Alts...>::STVariant(SerialIter& sit, SField const& name)
    : STBase(name)
{
    decode(sit);
}

template <typename... Alts>
template <detail::ValidAlts T>
T const&
STVariant<Alts...>::get() const
{
    if (!std::holds_alternative<T>(alternatives_))
        Throw<std::runtime_error>("The variant doesn't hold alternative");
    return std::get<T>(alternatives_);
}

template <typename... Alts>
template <detail::ValidAlts T>
T&
STVariant<Alts...>::get()
{
    if (!std::holds_alternative<T>(alternatives_))
        Throw<std::runtime_error>("The variant doesn't hold alternative");
    return std::get<T>(alternatives_);
}

// STBase

template <typename... Alts>
std::string
STVariant<Alts...>::getText() const
{
    return std::visit([&](auto&& alt) { return alt.getText(); }, alternatives_);
}

template <typename... Alts>
std::string
STVariant<Alts...>::getFullText() const
{
    return std::visit(
        [&](auto&& alt) { return alt.getFullText(); }, alternatives_);
}

template <typename... Alts>
Json::Value STVariant<Alts...>::getJson(JsonOptions) const
{
    return std::visit(
        [&](auto&& alt) { return alt.getJson(JsonOptions::none); },
        alternatives_);
}

template <typename... Alts>
void
STVariant<Alts...>::add(Serializer& s) const
{
    std::visit([&](auto&& alt) { alt.add(s); }, alternatives_);
}

template <typename... Alts>
bool
STVariant<Alts...>::isDefault() const
{
    return std::visit(
        [&](auto&& alt) { return alt.isDefault(); }, alternatives_);
}

template <typename... Alts>
void
STVariant<Alts...>::setJson(Json::Value& jv) const
{
    return std::visit(
        [&](auto&& alt) { return alt.setJson(jv); }, alternatives_);
}

template <typename... Alts>
auto const&
STVariant<Alts...>::getValue() const
{
    return alternatives_;
}

template <typename... Alts>
auto&
STVariant<Alts...>::getValue()
{
    return alternatives_;
}

}  // namespace ripple

#endif  // RIPPLE_PROTOCOL_STVARIANT_H_INCLUDED
