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

#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

Asset::Asset(Issue const& issue) : asset_(issue)
{
}

Asset::Asset(MPTIssue const& mpt) : asset_(mpt)
{
}

Asset::Asset(MPT const& mpt) : asset_(MPTIssue{mpt})
{
}

Asset::operator Issue() const
{
    return issue();
}

Asset::operator MPTIssue() const
{
    return mptIssue();
}

AccountID const&
Asset::getIssuer() const
{
    if (isIssue())
        return issue().getIssuer();
    return mptIssue().getIssuer();
}

Asset::Asset(uint192 const& u)
{
    std::uint32_t sequence;
    AccountID account;
    memcpy(&sequence, u.data(), sizeof(sequence));
    sequence = boost::endian::big_to_native(sequence);
    memcpy(account.data(), u.data() + sizeof(sequence), sizeof(AccountID));
    asset_ = std::make_pair(sequence, account);
}

Issue&
Asset::issue()
{
    if (!std::holds_alternative<Issue>(asset_))
        Throw<std::logic_error>("Asset is not Issue");
    return std::get<Issue>(asset_);
}

MPTIssue&
Asset::mptIssue()
{
    if (!std::holds_alternative<MPTIssue>(asset_))
        Throw<std::logic_error>("Asset is not MPT");
    return std::get<MPTIssue>(asset_);
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

bool
validJSONAsset(Json::Value const& jv)
{
    return (jv.isMember(jss::currency) && !jv.isMember(jss::mpt_issuance_id)) ||
        (!jv.isMember(jss::currency) && !jv.isMember(jss::issuer) &&
         jv.isMember(jss::mpt_issuance_id));
}

}  // namespace ripple