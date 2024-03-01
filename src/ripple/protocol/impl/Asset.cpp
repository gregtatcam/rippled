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

#include <ripple/protocol/Asset.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/jss.h>

namespace ripple {

Asset::Asset(uint192 const& u)
{
    asset_ = getMPT(u);
}

std::string
Asset::getText() const
{
    if (isIssue())
        return issue().getText();
    return to_string(mptIssue().getMptID());
}

std::string
to_string(Asset const& asset)
{
    if (asset.isIssue())
        return to_string(asset.issue());
    return to_string(asset.mptIssue().getMptID());
}

std::string
to_string(MPTIssue const& mptIssue)
{
    return to_string(mptIssue.getMptID());
}

std::string
to_string(MPT const& mpt)
{
    return to_string(getMptID(mpt.second, mpt.first));
}

Json::Value
toJson(Asset const& asset)
{
    Json::Value jv;
    if (asset.isMPT())
        jv[jss::mpt_issuance_id] = to_string(asset.mptIssue().mpt());
    else
    {
        jv[jss::currency] = to_string(asset.issue().currency);
        if (!isXRP(asset.issue()))
            jv[jss::issuer] = toBase58(asset.issue().account);
    }
    return jv;
}

Asset
assetFromJson(Json::Value const& jv)
{
    Asset asset;
    if (!validJSONAsset(jv))
        Throw<std::runtime_error>("invalid Asset");

    if (jv.isMember(jss::mpt_issuance_id))
    {
        uint192 u;
        if (!u.parseHex(jv[jss::mpt_issuance_id].asString()))
            Throw<std::runtime_error>("invalid MPTokenIssuanceID");
        asset = u;
        if (asset.account() == beast::zero)
            Throw<std::runtime_error>("invalid MPTokenIssuanceID account");
    }
    else
    {
        asset = issueFromJson(jv);
    }
    return asset;
}

bool
isConsistent(Asset const& asset)
{
    if (asset.isIssue())
        return isConsistent(asset.issue());
    return true;
}

std::ostream&
operator<<(std::ostream& os, Asset const& x)
{
    os << to_string(x);
    return os;
}

Json::Value
to_json(Asset const& asset)
{
    if (asset.isIssue())
        return to_json(asset.issue());
    return to_json(asset.mptIssue());
}

bool
validAsset(Asset const& asset)
{
    if (asset.isIssue())
        return isConsistent(asset.issue()) &&
            asset.issue().currency != badCurrency();
    return asset.mptIssue().account() != beast::zero;
}

bool
equalAssets(Asset const& asset1, Asset const& asset2)
{
    if (asset1.isIssue() != asset2.isIssue())
        return false;
    if (asset1.isIssue())
        return asset1.issue().currency == asset2.issue().currency;
    return asset1.mptIssue().mpt() == asset2.mptIssue().mpt();
}

bool
validJSONAsset(Json::Value const& jv)
{
    return (jv.isMember(jss::currency) && !jv.isMember(jss::mpt_issuance_id)) ||
        (!jv.isMember(jss::currency) && !jv.isMember(jss::issuer) &&
         jv.isMember(jss::mpt_issuance_id));
}

}  // namespace ripple