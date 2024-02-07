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
    Currency currency;
    AccountID account;
    currency = static_cast<Currency>(sit.get160());
    if (!isXRP(currency))
        account = sit.get160();
    else
        account = xrpAccount();

    if (isXRP(currency) != isXRP(account))
        Throw<std::runtime_error>(
            "invalid issue: currency and account native mismatch");

    auto getSequence = [](auto const& u) -> std::uint32_t {
        std::uint32_t s;
        std::memcpy(&s, u.data(), sizeof(s));
        return boost::endian::big_to_native(s);
    };
    // check if MPT
    static size_t constexpr seqSize = sizeof(MPT::first_type);
    static size_t constexpr truncAcctSize = sizeof(MPT::second_type) - seqSize;
    if (isXRP(currency))
        issue_ = std::make_pair(currency, account);
    else if (auto const sequence = getSequence(currency);
             sequence == getSequence(account) &&
             memcmp(
                 currency.data() + seqSize,
                 account.data() + 2 * seqSize,
                 truncAcctSize - seqSize) == 0)
    {
        AccountID account1;
        memcpy(account1.data(), currency.data(), truncAcctSize);
        memcpy(
            account1.data() + truncAcctSize,
            account.data() + truncAcctSize,
            seqSize);
        issue_ = std::make_pair(sequence, account1);
    }
    else
        issue_ = std::make_pair(currency, account);
}

STIssue::STIssue(SField const& name, Issue const& issue)
    : STBase{name}, issue_{issue}
{
    if (isXRP(issue_.asset()) != isXRP(issue_.account()))
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
    return issue_.getText();
}

Json::Value STIssue::getJson(JsonOptions) const
{
    return to_json(issue_);
}

void
STIssue::add(Serializer& s) const
{
    // serialize mpt as currency/2 x sequence
    // where currency = currency (32 bits) + account (first 128 bits)
    // and account = currency (32 bits) + account (last 128 bits).
    // when decoding, check first 32 bits of each 160 bits for MPT and verify
    // 96 bits of the account
    if (issue_.isMPT())
    {
        uint160 c{beast::zero};
        uint160 a{beast::zero};
        auto const mpt = static_cast<MPT>(issue_.asset());
        auto const& account = mpt.second;
        auto const sequence = boost::endian::native_to_big(mpt.first);
        static size_t constexpr seqSize = sizeof(MPT::first_type);
        static size_t constexpr truncAcctSize =
            sizeof(MPT::second_type) - seqSize;
        memcpy(c.data(), &sequence, seqSize);
        memcpy(c.data() + seqSize, account.data(), truncAcctSize);
        memcpy(a.data(), &sequence, seqSize);
        memcpy(a.data(), account.data() + seqSize, truncAcctSize);
        s.addBitString(c);
        s.addBitString(a);
    }
    else
    {
        s.addBitString(static_cast<Currency>(issue_.asset()));
        if (!isXRP(issue_.asset()))
            s.addBitString(issue_.account());
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
    return issue_ == xrpIssue();
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
    return STIssue{name, issueFromJson(v)};
}

}  // namespace ripple
