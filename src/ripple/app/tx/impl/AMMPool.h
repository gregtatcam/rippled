//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_APP_TX_AMMPOOL_H_INCLUDED
#define RIPPLE_APP_TX_AMMPOOL_H_INCLUDED

#include <ripple/app/misc/AMM.h>
#include <ripple/basics/Log.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STLedgerEntry.h>

namespace ripple {

template <typename TIn, typename TOut>
class AMMPool
{
private:
    std::shared_ptr<STLedgerEntry const> ammSle_;
    std::optional<Amounts> pool_;
    mutable std::optional<Amounts> cached_;

public:
    AMMPool(
        ReadView const& view,
        Issue const& in,
        Issue const& out,
        beast::Journal j)
        : ammSle_{getAMMSle(view, in, out)}, pool_{}
    {
        if (ammSle_)
        {
            auto const [assetIn, assetOut] = getAMMPoolFullBalances(
                view, ammSle_->getAccountID(sfAMMAccount), in, out, j);
            if (assetIn <= beast::zero || assetOut <= beast::zero)
            {
                JLOG(j.debug()) << "AMMPool: 0 balances";
                ammSle_ = nullptr;
            }
            else
                pool_ = Amounts(assetIn, assetOut);
        }
    }

    operator bool() const
    {
        return ammSle_ != nullptr;
    }

    std::optional<Amounts>
    balances() const
    {
        return pool_;
    }

    std::shared_ptr<STLedgerEntry const>
    entry() const
    {
        return ammSle_;
    }

    std::optional<Quality>
    spotPriceQuality() const
    {
        if (ammSle_)
            return Quality(*pool_);
        return std::nullopt;
    }

    void
    applyCached()
    {
        if (ammSle_ && cached_)
            pool_ = {pool_->in + cached_->in, pool_->out - cached_->out};
    }

    void
    cacheConsumed(STAmount const& in, STAmount const& out) const
    {
        if (ammSle_)
            cached_ = Amounts{in, out};
    }
};

}  // namespace ripple

#endif  // RIPPLE_APP_TX_AMMPOOL_H_INCLUDED
