//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_PROTOCOL_STISSUE_H_INCLUDED
#define RIPPLE_PROTOCOL_STISSUE_H_INCLUDED

#include <ripple/basics/CountedObject.h>
#include <ripple/protocol/Asset.h>
#include <ripple/protocol/SField.h>
#include <ripple/protocol/STBase.h>
#include <ripple/protocol/Serializer.h>

namespace ripple {

class STIssue final : public STBase, CountedObject<STIssue>
{
private:
    Asset asset_{xrpIssue()};

public:
    using value_type = Asset;

    STIssue() = default;

    explicit STIssue(SerialIter& sit, SField const& name);

    explicit STIssue(SField const& name, Asset const& asset);

    explicit STIssue(SField const& name, Issue const& issue);

    explicit STIssue(SField const& name, MPTIssue const& issue);

    explicit STIssue(SField const& name);

    Issue const&
    issue() const;

    MPTIssue const&
    mptIssue() const;

    Asset const&
    value() const noexcept;

    void
    setIssue(Issue const& issue);

    void
    setIssue(MPTIssue const& issue);

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
    static std::unique_ptr<STIssue>
    construct(SerialIter&, SField const& name);

    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

STIssue
issueFromJson(SField const& name, Json::Value const& v);

inline Issue const&
STIssue::issue() const
{
    if (!asset_.isIssue())
        Throw<std::logic_error>("STIssue doesn't hole Issue");
    return asset_.issue();
}

inline MPTIssue const&
STIssue::mptIssue() const
{
    if (!asset_.isMPT())
        Throw<std::logic_error>("STIssue doesn't hole MPTIssue");
    return asset_.mptIssue();
}

inline Asset const&
STIssue::value() const noexcept
{
    return asset_;
}

inline void
STIssue::setIssue(Issue const& issue)
{
    if (!asset_.isIssue() ||
        (isXRP(asset_.issue().currency) != isXRP(asset_.issue().account)))
        Throw<std::runtime_error>(
            "invalid issue: currency and account native mismatch");

    asset_ = issue;
}

inline void
STIssue::setIssue(MPTIssue const& issue)
{
    if (!asset_.isMPT())
        Throw<std::runtime_error>("invalid mpt issue");

    asset_ = issue;
}

inline bool
operator==(STIssue const& lhs, STIssue const& rhs)
{
    return lhs.value() == rhs.value();
}

inline bool
operator!=(STIssue const& lhs, STIssue const& rhs)
{
    return !operator==(lhs, rhs);
}

inline bool
operator<(STIssue const& lhs, STIssue const& rhs)
{
    return lhs.issue() < rhs.issue();
}

inline bool
operator==(STIssue const& lhs, Issue const& rhs)
{
    return lhs.issue() == rhs;
}

inline bool
operator<(STIssue const& lhs, Issue const& rhs)
{
    return lhs.issue() < rhs;
}

}  // namespace ripple

#endif
