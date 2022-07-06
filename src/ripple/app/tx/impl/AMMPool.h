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
#include <ripple/ledger/View.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STLedgerEntry.h>

namespace ripple {

template <typename TIn, typename TOut>
class AMMPool
{
private:
    std::shared_ptr<STLedgerEntry const> ammSle_;
    Amounts pool_;
    bool dirty_;
    beast::Journal const j_;

public:
    AMMPool(
        ReadView const& view,
        Issue const& in,
        Issue const& out,
        beast::Journal j);

    operator bool() const
    {
        return ammSle_ != nullptr;
    }

    Amounts const&
    balances() const
    {
        return pool_;
    }

    std::shared_ptr<STLedgerEntry const>
    entry() const
    {
        return ammSle_;
    }

    Quality
    spotPriceQuality() const
    {
        if (ammSle_)
            return Quality(pool_);
        return Quality{};
    }

    void
    setDirty()
    {
        dirty_ = true;
    }

    void
    updatePool(ReadView const& view, bool enable);
};

template <typename TIn, typename TOut>
AMMPool<TIn, TOut>::AMMPool(
    ReadView const& view,
    Issue const& in,
    Issue const& out,
    beast::Journal j)
    : ammSle_{nullptr}, pool_{STAmount{in}, STAmount{out}}, dirty_(true), j_(j)
{
    updatePool(view, true);
}

template <typename TIn, typename TOut>
void
AMMPool<TIn, TOut>::updatePool(ReadView const& view, bool enable)
{
    if (!enable)
        ammSle_ = nullptr;
    else if (dirty_)
    {
        if (!ammSle_)
            ammSle_ = getAMMSle(
                view, calcAMMGroupHash(pool_.in.issue(), pool_.out.issue()));
        if (ammSle_)
        {
            auto const ammAccountID = ammSle_->getAccountID(sfAMMAccount);
            auto funds = [&](Issue const& issue) {
                if (isXRP(issue))
                {
                    if (auto const sle =
                            view.read(keylet::account(ammAccountID)))
                        return sle->getFieldAmount(sfBalance);
                }
                else if (auto const sle = view.read(keylet::line(
                             ammAccountID, issue.account, issue.currency));
                         !isFrozen(
                             view, ammAccountID, issue.currency, issue.account))
                {
                    auto amount = sle->getFieldAmount(sfBalance);
                    if (amount.negative())
                        amount.negate();
                    amount.setIssuer(issue.account);
                    return amount;
                }

                return STAmount{};
            };

            auto const assetIn = funds(pool_.in.issue());
            auto const assetOut = funds(pool_.out.issue());
            if (assetIn <= beast::zero || assetOut <= beast::zero)
            {
                JLOG(j_.debug()) << "AMMPool: 0 balances";
                ammSle_ = nullptr;
            }
            else
                pool_ = Amounts(assetIn, assetOut);
        }
    }

    dirty_ = false;
}

}  // namespace ripple

#endif  // RIPPLE_APP_TX_AMMPOOL_H_INCLUDED
