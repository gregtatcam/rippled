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

#ifndef RIPPLE_PROTOCOL_STPATHSET_H_INCLUDED
#define RIPPLE_PROTOCOL_STPATHSET_H_INCLUDED

#include <xrpl/basics/CountedObject.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/PathAsset.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/UintTypes.h>
#include <cassert>
#include <cstddef>
#include <optional>

namespace ripple {

class STPathElement final : public CountedObject<STPathElement>
{
    unsigned int mType;
    AccountID mAccountID;
    PathAsset mAssetID;
    AccountID mIssuerID;

    bool is_offer_;
    std::size_t hash_value_;

public:
    struct PathAssetTag
    {
    };
    enum Type {
        typeNone = 0x00,
        typeAccount =
            0x01,  // Rippling through an account (vs taking an offer).
        typeCurrency = 0x10,  // Currency follows.
        typeIssuer = 0x20,    // Issuer follows.
        typeMPT = 0x40,       // MPT follows.
        typeBoundary = 0xFF,  // Boundary between alternate paths.
        typeAsset = typeCurrency | typeMPT,
        typeAll = typeAccount | typeCurrency | typeIssuer | typeMPT,
        // Combination of all types.
    };

    STPathElement();
    STPathElement(STPathElement const&) = default;
    STPathElement&
    operator=(STPathElement const&) = default;

    STPathElement(
        std::optional<AccountID> const& account,
        std::optional<Asset> const& asset,
        std::optional<AccountID> const& issuer);

    STPathElement(
        std::optional<AccountID> const& account,
        std::optional<PathAsset> const& asset,
        std::optional<AccountID> const& issuer,
        PathAssetTag);

    STPathElement(
        AccountID const& account,
        Asset const& asset,
        AccountID const& issuer,
        bool forceCurrency = false);

    STPathElement(
        AccountID const& account,
        PathAsset const& asset,
        AccountID const& issuer,
        bool forceCurrency = false);

    STPathElement(
        unsigned int uType,
        AccountID const& account,
        Asset const& asset,
        AccountID const& issuer);

    STPathElement(
        unsigned int uType,
        AccountID const& account,
        PathAsset const& asset,
        AccountID const& issuer);

    auto
    getNodeType() const;

    bool
    isOffer() const;

    bool
    isAccount() const;

    bool
    hasIssuer() const;

    bool
    hasCurrency() const;

    bool
    hasMPT() const;

    bool
    hasAsset() const;

    bool
    isNone() const;

    // Nodes are either an account ID or a offer prefix. Offer prefixs denote a
    // class of offers.
    AccountID const&
    getAccountID() const;

    PathAsset const&
    getPathAsset() const;

    Currency const&
    getCurrency() const;

    MPTID const&
    getMPTID() const;

    AccountID const&
    getIssuerID() const;

    bool
    operator==(const STPathElement& t) const;

    bool
    operator!=(const STPathElement& t) const;

private:
    static std::size_t
    get_hash(STPathElement const& element);
};

class STPath final : public CountedObject<STPath>
{
    std::vector<STPathElement> mPath;

public:
    STPath() = default;

    STPath(std::vector<STPathElement> p);

    std::vector<STPathElement>::size_type
    size() const;

    bool
    empty() const;

    void
    push_back(STPathElement const& e);

    template <typename... Args>
    void
    emplace_back(Args&&... args);

    bool
    hasSeen(
        AccountID const& account,
        Asset const& asset,
        AccountID const& issuer) const;

    bool
    hasSeen(
        AccountID const& account,
        PathAsset const& asset,
        AccountID const& issuer) const;

    Json::Value getJson(JsonOptions) const;

    std::vector<STPathElement>::const_iterator
    begin() const;

    std::vector<STPathElement>::const_iterator
    end() const;

    bool
    operator==(STPath const& t) const;

    std::vector<STPathElement>::const_reference
    back() const;

    std::vector<STPathElement>::const_reference
    front() const;

    STPathElement&
    operator[](int i);

    const STPathElement&
    operator[](int i) const;

    void
    reserve(size_t s);
};

//------------------------------------------------------------------------------

// A set of zero or more payment paths
class STPathSet final : public STBase, public CountedObject<STPathSet>
{
    std::vector<STPath> value;

public:
    STPathSet() = default;

    STPathSet(SField const& n);
    STPathSet(SerialIter& sit, SField const& name);

    void
    add(Serializer& s) const override;

    Json::Value getJson(JsonOptions) const override;

    SerializedTypeID
    getSType() const override;

    bool
    assembleAdd(STPath const& base, STPathElement const& tail);

    bool
    isEquivalent(const STBase& t) const override;

    bool
    isDefault() const override;

    // std::vector like interface:
    std::vector<STPath>::const_reference
    operator[](std::vector<STPath>::size_type n) const;

    std::vector<STPath>::reference
    operator[](std::vector<STPath>::size_type n);

    std::vector<STPath>::const_iterator
    begin() const;

    std::vector<STPath>::const_iterator
    end() const;

    std::vector<STPath>::size_type
    size() const;

    bool
    empty() const;

    void
    push_back(STPath const& e);

    template <typename... Args>
    void
    emplace_back(Args&&... args);

private:
    STBase*
    copy(std::size_t n, void* buf) const override;
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
};

// ------------ STPathElement ------------

inline STPathElement::STPathElement() : mType(typeNone), is_offer_(true)
{
    hash_value_ = get_hash(*this);
}

inline STPathElement::STPathElement(
    std::optional<AccountID> const& account,
    std::optional<Asset> const& asset,
    std::optional<AccountID> const& issuer)
    : STPathElement(
          account,
          PathAsset::toPathAsset(asset),
          issuer,
          PathAssetTag{})
{
}

inline STPathElement::STPathElement(
    std::optional<AccountID> const& account,
    std::optional<PathAsset> const& asset,
    std::optional<AccountID> const& issuer,
    PathAssetTag)
    : mType(typeNone)
{
    if (!account)
    {
        is_offer_ = true;
    }
    else
    {
        is_offer_ = false;
        mAccountID = *account;
        mType |= typeAccount;
        assert(mAccountID != noAccount());
    }

    if (asset)
    {
        mAssetID = *asset;
        mType |= mAssetID.holds<Currency>() ? typeCurrency : typeMPT;
    }

    if (issuer)
    {
        mIssuerID = *issuer;
        mType |= typeIssuer;
        assert(mIssuerID != noAccount());
    }

    hash_value_ = get_hash(*this);
}
inline STPathElement::STPathElement(
    AccountID const& account,
    Asset const& asset,
    AccountID const& issuer,
    bool forceCurrency)
    : STPathElement(
          account,
          PathAsset::toPathAsset(asset),
          issuer,
          forceCurrency)
{
}

inline STPathElement::STPathElement(
    AccountID const& account,
    PathAsset const& asset,
    AccountID const& issuer,
    bool forceCurrency)
    : mType(typeNone)
    , mAccountID(account)
    , mAssetID(asset)
    , mIssuerID(issuer)
    , is_offer_(isXRP(mAccountID))
{
    if (!is_offer_)
        mType |= typeAccount;

    if (!asset.holds<MPTID>() &&
        (forceCurrency || !isXRP(mAssetID.get<Currency>())))
        mType |= typeCurrency;

    if (!isXRP(issuer))
        mType |= typeIssuer;

    if (asset.holds<MPTID>())
        mType |= typeMPT;

    hash_value_ = get_hash(*this);
}

inline STPathElement::STPathElement(
    unsigned int uType,
    AccountID const& account,
    Asset const& asset,
    AccountID const& issuer)
    : STPathElement(uType, account, PathAsset::toPathAsset(asset), issuer)
{
}

inline STPathElement::STPathElement(
    unsigned int uType,
    AccountID const& account,
    PathAsset const& asset,
    AccountID const& issuer)
    : mType(uType)
    , mAccountID(account)
    , mAssetID(asset)
    , mIssuerID(issuer)
    , is_offer_(isXRP(mAccountID))
{
    if (!asset.holds<MPTID>())
        mType = mType & (~Type::typeMPT);
    else if (mAssetID.holds<Currency>() && isXRP(mAssetID.get<Currency>()))
        mType = mType & (~Type::typeCurrency);
    hash_value_ = get_hash(*this);
}

inline auto
STPathElement::getNodeType() const
{
    return mType;
}

inline bool
STPathElement::isOffer() const
{
    return is_offer_;
}

inline bool
STPathElement::isAccount() const
{
    return !isOffer();
}

inline bool
STPathElement::hasIssuer() const
{
    return getNodeType() & STPathElement::typeIssuer;
}

inline bool
STPathElement::hasCurrency() const
{
    return getNodeType() & STPathElement::typeCurrency;
}

inline bool
STPathElement::hasMPT() const
{
    return getNodeType() & STPathElement::typeMPT;
}

inline bool
STPathElement::hasAsset() const
{
    return getNodeType() & STPathElement::typeAsset;
}

inline bool
STPathElement::isNone() const
{
    return getNodeType() == STPathElement::typeNone;
}

// Nodes are either an account ID or a offer prefix. Offer prefixs denote a
// class of offers.
inline AccountID const&
STPathElement::getAccountID() const
{
    return mAccountID;
}

inline PathAsset const&
STPathElement::getPathAsset() const
{
    return mAssetID;
}

inline Currency const&
STPathElement::getCurrency() const
{
    return mAssetID.get<Currency>();
}

inline MPTID const&
STPathElement::getMPTID() const
{
    return mAssetID.get<MPTID>();
}

inline AccountID const&
STPathElement::getIssuerID() const
{
    return mIssuerID;
}

inline bool
STPathElement::operator==(const STPathElement& t) const
{
    return (mType & typeAccount) == (t.mType & typeAccount) &&
        hash_value_ == t.hash_value_ && mAccountID == t.mAccountID &&
        mAssetID == t.mAssetID && mIssuerID == t.mIssuerID;
}

inline bool
STPathElement::operator!=(const STPathElement& t) const
{
    return !operator==(t);
}

// ------------ STPath ------------

inline STPath::STPath(std::vector<STPathElement> p) : mPath(std::move(p))
{
}

inline std::vector<STPathElement>::size_type
STPath::size() const
{
    return mPath.size();
}

inline bool
STPath::empty() const
{
    return mPath.empty();
}

inline void
STPath::push_back(STPathElement const& e)
{
    mPath.push_back(e);
}

template <typename... Args>
inline void
STPath::emplace_back(Args&&... args)
{
    mPath.emplace_back(std::forward<Args>(args)...);
}

inline std::vector<STPathElement>::const_iterator
STPath::begin() const
{
    return mPath.begin();
}

inline std::vector<STPathElement>::const_iterator
STPath::end() const
{
    return mPath.end();
}

inline bool
STPath::operator==(STPath const& t) const
{
    return mPath == t.mPath;
}

inline std::vector<STPathElement>::const_reference
STPath::back() const
{
    return mPath.back();
}

inline std::vector<STPathElement>::const_reference
STPath::front() const
{
    return mPath.front();
}

inline STPathElement&
STPath::operator[](int i)
{
    return mPath[i];
}

inline const STPathElement&
STPath::operator[](int i) const
{
    return mPath[i];
}

inline void
STPath::reserve(size_t s)
{
    mPath.reserve(s);
}

// ------------ STPathSet ------------

inline STPathSet::STPathSet(SField const& n) : STBase(n)
{
}

// std::vector like interface:
inline std::vector<STPath>::const_reference
STPathSet::operator[](std::vector<STPath>::size_type n) const
{
    return value[n];
}

inline std::vector<STPath>::reference
STPathSet::operator[](std::vector<STPath>::size_type n)
{
    return value[n];
}

inline std::vector<STPath>::const_iterator
STPathSet::begin() const
{
    return value.begin();
}

inline std::vector<STPath>::const_iterator
STPathSet::end() const
{
    return value.end();
}

inline std::vector<STPath>::size_type
STPathSet::size() const
{
    return value.size();
}

inline bool
STPathSet::empty() const
{
    return value.empty();
}

inline void
STPathSet::push_back(STPath const& e)
{
    value.push_back(e);
}

template <typename... Args>
inline void
STPathSet::emplace_back(Args&&... args)
{
    value.emplace_back(std::forward<Args>(args)...);
}

}  // namespace ripple

#endif
