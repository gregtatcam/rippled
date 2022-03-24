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

#ifndef RIPPLE_TX_AMMTRADE_H_INCLUDED
#define RIPPLE_TX_AMMTRADE_H_INCLUDED

#include <ripple/app/tx/impl/Transactor.h>

namespace ripple {

class Sandbox;

/** AMMTrade implements AMM trade Transactor. The AMMTrade
 * has three subtransactions:
 * - AMMDeposit to deposit liquidity into the AMM pool and
 *   receive liquidity provider tokens
 * - AMMWithdraw to withdraw liquidity from the AMM pool
 * - AMMSwap to swap pool liquidity. This is mostly arbitrage
 *   function.
 * The type of subtransaction is determined by the transaction
 * flag.
 */
class AMMTrade : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Custom};

    explicit AMMTrade(ApplyContext& ctx);

    static TxConsequences
    makeTxConsequences(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static NotTEC
    preflight(PreflightContext const& ctx);

    /** Enforce constraints beyond those of the Transactor base class. */
    static TER
    preclaim(PreclaimContext const& ctx);

    /** Gather information beyond what the Transactor base class gathers. */
    void
    preCompute() override;

    /** Attempt to create the AMM instance. */
    TER
    doApply() override;

private:
    std::pair<TER, bool>
    applyGuts(Sandbox& view, Sandbox& view_cancel);
};

}  // namespace ripple

#endif  // RIPPLE_TX_AMMTRADE_H_INCLUDED
