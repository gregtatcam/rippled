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

#include <ripple/protocol/STIssue.h>

#include <ripple/basics/Log.h>
#include <ripple/basics/contract.h>
#include <ripple/protocol/SystemParameters.h>
#include <ripple/protocol/jss.h>

#include <boost/algorithm/string.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/regex.hpp>

#include <iostream>
#include <iterator>
#include <memory>

namespace ripple {

STIssue::STIssue(SField const& name) : STBase{name}
{
}

STIssue::STIssue(SerialIter& sit, SField const& name) : STBase{name}
{
    Issue issue;
    issue.currency = sit.get160();

    if (isXRP(issue.currency))
        issue.account = xrpAccount();
    // Check if MPT
    else
    {
        auto const seqSize = sizeof(MPT::first_type);
        auto const size = sizeof(MPT::second_type) - seqSize;
        auto getSequence = [&](uint160::pointer p) {
            std::uint32_t sequence;
            memcpy(&sequence, p, seqSize);
            return boost::endian::big_to_native(sequence);
        };
        auto acctMatch = [&]() {
            // first 128-bit of MPT account are serialized
            // at issue.currency 32-bit offset
            // last 128-bit of MPT account are serialized
            // at issue.account 32-bit offset
            // 96-bit must match if MPT
            return memcmp(
                       issue.currency.data() + 2 * seqSize,
                       issue.account.data() + seqSize,
                       size - seqSize) == 0;
        };

        issue.account = sit.get160();
        MPTIssue mpt;
        mpt.sequence() = getSequence(issue.currency.data());
        // sequence and account must match if MPT
        if (mpt.sequence() == getSequence(issue.account.data()) && acctMatch())
        {
            memcpy(mpt.account().data(), issue.currency.data() + seqSize, size);
            memcpy(
                mpt.account().data() + size,
                issue.account.data() + size,
                seqSize);
            asset_ = mpt;
        }
        else
        {
            if (isXRP(issue.currency) != isXRP(issue.account))
                Throw<std::runtime_error>(
                    "invalid issue: currency and account native mismatch");
            asset_ = issue;
        }
    }
}

STIssue::STIssue(SField const& name, Issue const& issue)
    : STBase{name}, asset_{issue}
{
    if (isXRP(issue.currency) != isXRP(issue.account))
        Throw<std::runtime_error>(
            "invalid issue: currency and account native mismatch");
}

STIssue::STIssue(SField const& name, MPTIssue const& issue)
    : STBase{name}, asset_{issue}
{
}

STIssue::STIssue(SField const& name, Asset const& asset)
    : STBase{name}, asset_{asset}
{
    if (asset.isIssue() &&
        isXRP(asset.issue().currency) != isXRP(asset.issue().account))
        Throw<std::runtime_error>(
            "invalid issue: currency and account native mismatch");
}

SerializedTypeID
STIssue::getSType() const
{
    return STI_ISSUE;
}

std::string
STIssue::getText() const
{
    return asset_.getText();
}

Json::Value STIssue::getJson(JsonOptions) const
{
    return to_json(asset_);
}

void
STIssue::add(Serializer& s) const
{
    if (asset_.isIssue())
    {
        s.addBitString(asset_.issue().currency);
        if (!isXRP(asset_.issue().currency))
            s.addBitString(asset_.issue().account);
    }
    else
    {
        uint160 u;
        auto const& mpt = asset_.mptIssue();
        auto const sequence = boost::endian::native_to_big(mpt.sequence());
        auto const seqSize = sizeof(MPT::first_type);
        auto const size = sizeof(MPT::second_type) - seqSize;
        assert(sizeof(MPT::second_type) == sizeof(u));
        memcpy(u.data(), &sequence, seqSize);
        memcpy(u.data() + seqSize, mpt.account().data(), size);
        s.addBitString(u);
        memcpy(u.data() + seqSize, mpt.account().data() + seqSize, size);
        s.addBitString(u);
    }
}

bool
STIssue::isEquivalent(const STBase& t) const
{
    const STIssue* v = dynamic_cast<const STIssue*>(&t);
    return v && (*v == *this);
}

bool
STIssue::isDefault() const
{
    return asset_.isIssue() && asset_.issue() == xrpIssue();
}

std::unique_ptr<STIssue>
STIssue::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STIssue>(sit, name);
}

STBase*
STIssue::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STIssue::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

STIssue
issueFromJson(SField const& name, Json::Value const& v)
{
    return STIssue{name, assetFromJson(v)};
}

}  // namespace ripple
