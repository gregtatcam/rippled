//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2022 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/AMMWithdraw.h>

#include <ripple/app/misc/AMM.h>
#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/basics/Number.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

TxConsequences
AMMWithdraw::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMWithdraw::preflight(PreflightContext const& ctx)
{
    if (!ammEnabled(ctx.rules))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto const flags = ctx.tx.getFlags();
    if (flags & tfAMMWithdrawMask)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid flags.";
        return temINVALID_FLAG;
    }
    bool const withdrawAll = flags & tfAMMWithdrawAll;

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];
    auto const ePrice = ctx.tx[~sfEPrice];
    auto const lpTokens = ctx.tx[~sfLPTokenIn];
    // Valid combinations are:
    //   LPTokens|tfAMMWithdrawAll
    //   Amount
    //   Amount and Amount2
    //   AssetLPToken and [LPTokens|tfAMMWithdrawAll]
    //   Amount and EPrice
    if (auto const subTxType = std::bitset<32>(flags & tfAMMSubTx);
        subTxType.none() || subTxType.count() > 1)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid flags.";
        return temINVALID_FLAG;
    }
    else if (flags & tfLPToken)
    {
        if ((!lpTokens && !withdrawAll) || (lpTokens && withdrawAll) ||
            amount || amount2 || ePrice)
            return temBAD_AMM_OPTIONS;
    }
    else if (flags & tfSingleAsset)
    {
        if (!amount || lpTokens || withdrawAll || amount2 || ePrice)
            return temBAD_AMM_OPTIONS;
    }
    else if (flags & tfTwoAsset)
    {
        if (!amount || !amount2 || lpTokens || withdrawAll || ePrice)
            return temBAD_AMM_OPTIONS;
    }
    else if (flags & tfOneAssetLPToken)
    {
        if (!amount || (!lpTokens && !withdrawAll) ||
            (lpTokens && withdrawAll) || amount2 || ePrice)
            return temBAD_AMM_OPTIONS;
    }
    else if (flags & tfLimitLPToken)
    {
        if (!amount || !ePrice || lpTokens || withdrawAll || amount2)
            return temBAD_AMM_OPTIONS;
    }

    auto const asset = ctx.tx[sfAsset];
    auto const asset2 = ctx.tx[sfAsset2];
    if (auto const res = invalidAMMAssetPair(asset, asset2))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid asset pair.";
        return res;
    }

    if (amount && amount2 && amount->issue() == amount2->issue())
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens, same issue."
                            << amount->issue() << " " << amount2->issue();
        return temBAD_AMM_TOKENS;
    }

    if (lpTokens && *lpTokens == beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens.";
        return temBAD_AMM_TOKENS;
    }

    if (auto const res = invalidAMMAmount(
            amount, {{asset, asset2}}, withdrawAll || lpTokens || ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset1Out";
        return res;
    }

    if (auto const res = invalidAMMAmount(amount2, {{asset, asset2}}))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid Asset2OutAmount";
        return res;
    }

    if (auto const res = invalidAMMAmount(ePrice))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid EPrice";
        return res;
    }

    return preflight2(ctx);
}

TER
AMMWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const accountID = ctx.tx[sfAccount];

    auto const ammSle = getAMMSle(ctx.view, ctx.tx[sfAsset], ctx.tx[sfAsset2]);
    if (!ammSle)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid asset pair.";
        return terNO_AMM;
    }

    auto const amount = ctx.tx[~sfAmount];
    auto const amount2 = ctx.tx[~sfAmount2];

    if (amount)
    {
        if (auto const ter = requireAuth(ctx.view, amount->issue(), accountID);
            ter != tesSUCCESS)
        {
            JLOG(ctx.j.debug()) << "AMM Instance: account is not authorized, "
                                << amount->issue();
            return ter;
        }
    }

    if (amount2)
    {
        if (auto const ter = requireAuth(ctx.view, amount2->issue(), accountID);
            ter != tesSUCCESS)
        {
            JLOG(ctx.j.debug()) << "AMM Instance: account is not authorized, "
                                << amount2->issue();
            return ter;
        }
    }

    if (isFrozen(ctx.view, amount) || isFrozen(ctx.view, sfAmount2))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw involves frozen asset.";
        return tecFROZEN;
    }

    auto const lptBalance =
        ammLPHolds(ctx.view, **ammSle, ctx.tx[sfAccount], ctx.j);
    auto const lpTokens = (ctx.tx.getFlags() & tfAMMWithdrawAll)
        ? std::optional<STAmount>(lptBalance)
        : ctx.tx[~sfLPTokenIn];

    if (lptBalance <= beast::zero)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: tokens balance is zero.";
        return tecAMM_BALANCE;
    }

    if (lpTokens && *lpTokens > lptBalance)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid tokens.";
        return tecAMM_INVALID_TOKENS;
    }

    if (lpTokens && lpTokens->issue() != lptBalance.issue())
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: invalid LPTokens.";
        return temBAD_AMM_TOKENS;
    }

    return tesSUCCESS;
}

std::pair<TER, bool>
AMMWithdraw::applyGuts(Sandbox& sb)
{
    auto const amount = ctx_.tx[~sfAmount];
    auto const amount2 = ctx_.tx[~sfAmount2];
    auto const ePrice = ctx_.tx[~sfEPrice];
    auto ammSle = getAMMSle(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2]);
    if (!ammSle)
        return {ammSle.error(), false};
    auto const ammAccountID = (**ammSle)[sfAMMAccount];
    auto const lpTokensWithdraw = (ctx_.tx.getFlags() & tfAMMWithdrawAll)
        ? std::optional<STAmount>(ammLPHolds(
              ctx_.view(), **ammSle, ctx_.tx[sfAccount], ctx_.journal))
        : ctx_.tx[~sfLPTokenIn];

    auto const tfee = getTradingFee(ctx_.view(), **ammSle, account_);

    auto const expected = ammHolds(
        sb,
        **ammSle,
        amount ? amount->issue() : std::optional<Issue>{},
        amount2 ? amount2->issue() : std::optional<Issue>{},
        ctx_.journal);
    if (!expected)
        return {expected.error(), false};
    auto const [amountBalance, amount2Balance, lptAMMBalance] = *expected;

    auto const subTxType = ctx_.tx.getFlags() & tfAMMSubTx;

    auto const [result, withdrawnTokens] =
        [&,
         &amountBalance = amountBalance,
         &amount2Balance = amount2Balance,
         &lptAMMBalance = lptAMMBalance]() -> std::pair<TER, STAmount> {
        if (subTxType == tfTwoAsset)
            return equalWithdrawLimit(
                sb,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                *amount,
                *amount2);
        if (subTxType == tfOneAssetLPToken)
            return singleWithdrawTokens(
                sb,
                ammAccountID,
                amountBalance,
                lptAMMBalance,
                *amount,
                *lpTokensWithdraw,
                tfee);
        if (subTxType == tfLimitLPToken)
            return singleWithdrawEPrice(
                sb,
                ammAccountID,
                amountBalance,
                lptAMMBalance,
                *amount,
                *ePrice,
                tfee);
        if (subTxType == tfSingleAsset)
            return singleWithdraw(
                sb, ammAccountID, amountBalance, lptAMMBalance, *amount, tfee);
        if (subTxType == tfLPToken)
            return equalWithdrawTokens(
                sb,
                ammAccountID,
                amountBalance,
                amount2Balance,
                lptAMMBalance,
                *lpTokensWithdraw);
        // should not happen.
        JLOG(j_.error()) << "AMM Withdraw: invalid options.";
        return std::make_pair(tecAMM_FAILED_WITHDRAW, STAmount{});
    }();

    if (result == tesSUCCESS && withdrawnTokens != beast::zero)
    {
        (*ammSle)->setFieldAmount(
            sfLPTokenBalance, lptAMMBalance - withdrawnTokens);
        sb.update(*ammSle);
    }

    return {result, result == tesSUCCESS};
}

TER
AMMWithdraw::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel(&ctx_.view());

    auto const result = applyGuts(sb);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());

    return result.first;
}

TER
AMMWithdraw::deleteAccount(Sandbox& sb, AccountID const& ammAccountID)
{
    auto sleAMMRoot = sb.peek(keylet::account(ammAccountID));
    auto sleAMM = getAMMSle(sb, ctx_.tx[sfAsset], ctx_.tx[sfAsset2]);

    if (!sleAMMRoot || !sleAMM)
        return tecINTERNAL;

    // Note, the AMM trust lines are deleted since the balance
    // goes to 0. It also means there are no linked
    // ledger objects.
    sb.erase(*sleAMM);
    sb.erase(sleAMMRoot);

    return tesSUCCESS;
}

std::pair<TER, STAmount>
AMMWithdraw::withdraw(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountWithdraw,
    std::optional<STAmount> const& amount2Withdraw,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensWithdraw)
{
    auto const ammSle =
        getAMMSle(ctx_.view(), ctx_.tx[sfAsset], ctx_.tx[sfAsset2]);
    if (!ammSle)
        return {ammSle.error(), STAmount{}};
    auto const lpTokens = ammLPHolds(view, **ammSle, account_, ctx_.journal);
    auto const expected =
        ammHolds(view, **ammSle, amountWithdraw.issue(), std::nullopt, j_);
    if (!expected)
        return {expected.error(), STAmount{}};
    auto const [amountBalance, amount2Balance, _] = *expected;
    (void)_;

    // Invalid tokens or withdrawing more than own.
    // TODO Note, the balances might be different due to round-off. Need to
    // handle. Fail for now.
    if (lpTokensWithdraw == beast::zero || lpTokensWithdraw > lpTokens ||
        lpTokensWithdraw > lptAMMBalance)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw, invalid LP tokens "
            << " tokens: " << lpTokensWithdraw << " " << lpTokens;
        return {tecAMM_INVALID_TOKENS, STAmount{}};
    }
    // Withdrawing all tokens but balances are not 0.
    if (lpTokensWithdraw == lptAMMBalance &&
        (amountWithdraw != amountBalance ||
         (amount2Withdraw && *amount2Withdraw != amount2Balance)))
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw, invalid LP balance "
            << " amountBalance: " << amountBalance << " " << amountWithdraw
            << " amount2Balance: " << amount2Balance << " "
            << (amount2Withdraw ? to_string(*amount2Withdraw) : "");
        return {tecAMM_BALANCE, STAmount{}};
    }
    // Withdrawing one side of the pool
    if (amountWithdraw == amountBalance && !amount2Withdraw)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw one side of the pool "
            << " amountBalance: " << amountBalance << " " << amountWithdraw
            << " lpTokens: " << lpTokensWithdraw << " lptBalance "
            << lptAMMBalance;
        return {tecAMM_BALANCE, STAmount{}};
    }

    // Withdraw amountWithdraw
    auto res =
        ammSend(view, ammAccount, account_, amountWithdraw, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw " << amountWithdraw;
        return {res, STAmount{}};
    }

    // Withdraw amount2Withdraw
    if (amount2Withdraw)
    {
        res =
            ammSend(view, ammAccount, account_, *amount2Withdraw, ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Withdraw: failed to withdraw " << *amount2Withdraw;
            return {res, STAmount{}};
        }
    }

    // Withdraw LP tokens
    res = redeemIOU(
        view,
        account_,
        lpTokensWithdraw,
        lpTokensWithdraw.issue(),
        ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Withdraw: failed to withdraw LPTokens";
        return {res, STAmount{}};
    }

    if (lpTokensWithdraw == lptAMMBalance)
        return {deleteAccount(view, ammAccount), STAmount{}};

    return {tesSUCCESS, lpTokensWithdraw};
}

/** Proportional withdrawal of pool assets for the amount of LPTokens.
 */
std::pair<TER, STAmount>
AMMWithdraw::equalWithdrawTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokensWithdraw)
{
    auto const frac = divide(lpTokensWithdraw, lptAMMBalance, noIssue());
    return withdraw(
        view,
        ammAccount,
        multiply(amountBalance, frac, amountBalance.issue()),
        multiply(amount2Balance, frac, amount2Balance.issue()),
        lptAMMBalance,
        lpTokensWithdraw);
}

/** All assets withdrawal with the constraints on the maximum amount
 * of each asset that the trader is willing to withdraw.
 *       a = (t/T) * A (5)
 *       b = (t/T) * B (6)
 *       where
 *      A,B: current pool composition
 *      T: current balance of outstanding LPTokens
 *      a: balance of asset A being added
 *      b: balance of asset B being added
 *      t: balance of LPTokens issued to LP after a successful transaction
 * Use equation 5 to compute , given the amount in Asset1Out. Let this be Z
 * Use equation 6 to compute the amount of asset2, given  t~Z. Let
 *     the computed amount of asset2 be X
 * If X <= amount in Asset2Out:
 *   The amount of asset1 to be withdrawn is the one specified in Asset1Out
 *   The amount of asset2 to be withdrawn is X
 *   The amount of LPTokens redeemed is Z
 * If X> amount in Asset2Out:
 *   Use equation 5 to compute , given the amount in Asset2Out. Let this be Q
 *   Use equation 6 to compute the amount of asset1, given t~Q.
 *     Let the computed amount of asset1 be W
 *   The amount of asset2 to be withdrawn is the one specified in Asset2Out
 *   The amount of asset1 to be withdrawn is W
 *   The amount of LPTokens redeemed is Q
 */
std::pair<TER, STAmount>
AMMWithdraw::equalWithdrawLimit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& amount2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& amount2)
{
    auto frac = Number{amount} / amountBalance;
    auto const amount2Withdraw = amount2Balance * frac;
    if (amount2Withdraw <= amount2)
        return withdraw(
            view,
            ammAccount,
            amount,
            toSTAmount(amount2.issue(), amount2Withdraw),
            lptAMMBalance,
            toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac));
    frac = Number{amount2} / amount2Balance;
    auto const amountWithdraw = amountBalance * frac;
    return withdraw(
        view,
        ammAccount,
        toSTAmount(amount.issue(), amountWithdraw),
        amount2,
        lptAMMBalance,
        toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac));
}

/** Withdraw single asset equivalent to the amount specified in Asset1Out.
 *       t = T * (1 - sqrt(1 - b/(B * (1 - 0.5 * tfee)))) (7)
 * Use equation 7 to compute the t, given the amount in Asset1Out.
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdraw(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    std::uint16_t tfee)
{
    auto const tokens = lpTokensOut(amountBalance, amount, lptAMMBalance, tfee);
    if (tokens == beast::zero)
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    return withdraw(
        view, ammAccount, amount, std::nullopt, lptAMMBalance, tokens);
}

/** withdrawal of single asset specified in Asset1Out proportional
 * to the share represented by the amount of LPTokens.
 *       Y = B * (1 - (1 - t/T)**2) * (1 - 0.5 * tfee) (8)
 * Use equation 8 to compute the amount of asset1, given the redeemed t
 *   represented by LPTokens. Let this be Y.
 * If (amount exists for Asset1Out & Y >= amount in Asset1Out) ||
 *       (amount field does not exist for Asset1Out):
 *   The amount of asset out is Y
 *   The amount of LPTokens redeemed is LPTokens
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdrawTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& lpTokensWithdraw,
    std::uint16_t tfee)
{
    auto const amountWithdraw =
        withdrawByTokens(amountBalance, lptAMMBalance, lpTokensWithdraw, tfee);
    if (amount == beast::zero || amountWithdraw >= amount)
        return withdraw(
            view,
            ammAccount,
            toSTAmount(amount.issue(), amountWithdraw),
            std::nullopt,
            lptAMMBalance,
            lpTokensWithdraw);
    return {tecAMM_FAILED_WITHDRAW, STAmount{}};
}

/** Withdraw single asset with two constraints.
 * a. amount of asset1 if specified in Asset1Out specifies the minimum
 *     amount of asset1 that the trader is willing to withdraw.
 * b. The effective price of asset traded out does not exceed the amount
 *     specified in EPrice
 *       The effective price (EP) of a trade is defined as the ratio
 *       of the tokens the trader sold or swapped in (Token B) and
 *       the token they got in return or swapped out (Token A).
 *       EP(B/A) = b/a (III)
 *       b = B * (1 - (1 - t/T)**2) * (1 - 0.5 * tfee) (8)
 * Use equations 8 & III and amount in EPrice to compute the two variables:
 *   asset in as LPTokens. Let this be X
 *   asset out as that in Asset1Out. Let this be Y
 * If (amount exists for Asset1Out & Y >= amount in Asset1Out) ||
 *     (amount field does not exist for Asset1Out):
 *   The amount of assetOut is given by Y
 *   The amount of LPTokens is given by X
 */
std::pair<TER, STAmount>
AMMWithdraw::singleWithdrawEPrice(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& amountBalance,
    STAmount const& lptAMMBalance,
    STAmount const& amount,
    STAmount const& ePrice,
    std::uint16_t tfee)
{
    auto const tokens = lptAMMBalance *
        (Number(2) -
         lptAMMBalance / (amountBalance * ePrice * feeMultHalf(tfee)));
    if (tokens <= 0)
        return {tecAMM_FAILED_WITHDRAW, STAmount{}};
    auto const amountWithdraw = toSTAmount(amount.issue(), tokens / ePrice);
    if (amount == beast::zero ||
        (amount != beast::zero && amountWithdraw >= amount))
        return withdraw(
            view,
            ammAccount,
            amountWithdraw,
            std::nullopt,
            lptAMMBalance,
            toSTAmount(lptAMMBalance.issue(), tokens));

    return {tecAMM_FAILED_WITHDRAW, STAmount{}};
}

}  // namespace ripple