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

#include <ripple/protocol/AMMCore.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Rules.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/STObject.h>
#include <ripple/protocol/digest.h>

namespace ripple {

AccountID
ammAccountID(
    std::uint16_t prefix,
    uint256 const& parentHash,
    uint256 const& ammID)
{
    ripesha_hasher rsh;
    auto const hash = sha512Half(prefix, parentHash, ammID);
    rsh(hash.data(), hash.size());
    return AccountID{static_cast<ripesha_hasher::result_type>(rsh)};
}

Currency
ammLPTCurrency(Asset const& asset1, Asset const& asset2)
{
    // AMM LPToken is 0x03 plus 19 bytes of the hash
    std::int32_t constexpr AMMCurrencyCode = 0x03;
    auto const [minA, maxA] = std::minmax(asset1, asset2);
    uint256 hash;
    std::visit(
        [&](auto&& arg1, auto&& arg2) {
            auto fromIss = []<typename T>(T const& iss) {
                if constexpr (std::is_same_v<T, Issue>)
                    return iss.currency;
                if constexpr (std::is_same_v<T, MPTIssue>)
                    return iss.getMptID();
            };
            hash = sha512Half(fromIss(arg1), fromIss(arg2));
        },
        minA.value(),
        maxA.value());
    Currency currency;
    *currency.begin() = AMMCurrencyCode;
    std::copy(
        hash.begin(), hash.begin() + currency.size() - 1, currency.begin() + 1);
    return currency;
}

Issue
ammLPTIssue(
    Asset const& asset1,
    Asset const& asset2,
    AccountID const& ammAccountID)
{
    return Issue(ammLPTCurrency(asset1, asset2), ammAccountID);
}

NotTEC
invalidAMMAsset(
    Asset const& asset,
    std::optional<std::pair<Asset, Asset>> const& pair)
{
    if (asset.isMPT() && asset.mptIssue().account() == beast::zero)
        return temBAD_MPT;
    if (asset.isIssue() && badCurrency() == asset.issue().currency)
        return temBAD_CURRENCY;
    if (isXRP(asset) && asset.account().isNonZero())
        return temBAD_ISSUER;
    if (pair && asset != pair->first && asset != pair->second)
        return temBAD_AMM_TOKENS;
    return tesSUCCESS;
}

NotTEC
invalidAMMAssetPair(
    Asset const& asset1,
    Asset const& asset2,
    std::optional<std::pair<Asset, Asset>> const& pair)
{
    if (asset1 == asset2)
        return temBAD_AMM_TOKENS;
    if (auto const res = invalidAMMAsset(asset1, pair))
        return res;
    if (auto const res = invalidAMMAsset(asset2, pair))
        return res;
    return tesSUCCESS;
}

NotTEC
invalidAMMAmount(
    STAmount const& amount,
    std::optional<std::pair<Asset, Asset>> const& pair,
    bool validZero)
{
    if (auto const res = invalidAMMAsset(amount.asset(), pair))
        return res;
    if (amount < beast::zero || (!validZero && amount == beast::zero))
        return temBAD_AMOUNT;
    return tesSUCCESS;
}

std::optional<std::uint8_t>
ammAuctionTimeSlot(std::uint64_t current, STObject const& auctionSlot)
{
    // It should be impossible for expiration to be < TOTAL_TIME_SLOT_SECS,
    // but check just to be safe
    auto const expiration = auctionSlot[sfExpiration];
    assert(expiration >= TOTAL_TIME_SLOT_SECS);
    if (expiration >= TOTAL_TIME_SLOT_SECS)
    {
        if (auto const start = expiration - TOTAL_TIME_SLOT_SECS;
            current >= start)
        {
            if (auto const diff = current - start; diff < TOTAL_TIME_SLOT_SECS)
                return diff / AUCTION_SLOT_INTERVAL_DURATION;
        }
    }
    return std::nullopt;
}

bool
ammEnabled(Rules const& rules)
{
    return rules.enabled(featureAMM) && rules.enabled(fixUniversalNumber);
}

}  // namespace ripple
