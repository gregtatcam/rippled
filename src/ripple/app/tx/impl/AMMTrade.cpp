//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/AMMTrade.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

/*-------------------------------------------------------------------------------*/

/** Validate the amount.
 */
static std::optional<TEMcodes>
validAmount(std::optional<STAmount> const& a)
{
    if (!a)
        return std::nullopt;
    if (badCurrency() == a->getCurrency())
        return temBAD_CURRENCY;
    if (a->native() && a->native() != !a->getIssuer())
        return temBAD_ISSUER;
    if (*a <= beast::zero)
        return temBAD_AMOUNT;
    return std::nullopt;
}

/** Check if the line is frozen from the issuer.
 */
static bool
isFrozen(ReadView const& view, std::optional<STAmount> const& a)
{
    return a && !a->native() && isGlobalFrozen(view, a->getIssuer());
}

/** Check if the field is present.
 */
bool
present(STTx const& tx, SField const& f)
{
    return tx.isFieldPresent(f);
}

/** Check if the fields are present.
 */
template <typename... Args>
bool
present(STTx const& tx, SField const& f, Args const&... args)
{
    return tx.isFieldPresent(f) || present(tx, args...);
}

/** Validate deposit/withdraw lpt amount. The amount must not
 * exceed 30% of the pool share and must not be 0.
 * @param lptAMMBalance current AMM LPT balance
 * @param tokens requested LPT amount to deposit/withdraw
 * @return
 */
bool
validLPTokens(STAmount const& lptAMMBalance, STAmount const& tokens)
{
    auto const pct = multiply(
        divide(tokens, lptAMMBalance, tokens.issue()),
        STAmount{tokens.issue(), 100},
        tokens.issue());
    return pct != beast::zero && pct <= STAmount{tokens.issue(), 30};
}

namespace deposit {

/** Preflight deposit check. Validate options and amounts.
 */
NotTEC
preflight(PreflightContext const& ctx)
{
    auto const asset1InDetails = ctx.tx[~sfAsset1InDetails];
    auto const asset2InAmount = ctx.tx[~sfAsset2InAmount];
    auto const maxEP = ctx.tx[~sfMaxEP];
    auto const lpTokens = ctx.tx[~sfLPTokens];
    // Valid combinations are:
    //   LPTokens
    //   Asset1InDetails
    //   Asset1InDetails and Asset2InAmount
    //   Asset1InDetails and LPTokens
    //   Asset1InDetails and MaxEP
    if ((!lpTokens && !asset1InDetails) ||
        (lpTokens && (asset2InAmount || maxEP)) ||
        (asset1InDetails &&
         ((asset2InAmount && (lpTokens || maxEP)) ||
          (maxEP && (asset2InAmount || lpTokens)))))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "deposit fields.";
        return temBAD_AMM_OPTIONS;
    }
    if (lpTokens)
    {
        if (*lpTokens > 30000)
        {
            JLOG(ctx.j.debug()) << "Malformed transaction: invalid LPTokens";
            return temBAD_AMM_TOKENS;
        }
    }
    else if (auto const res = validAmount(asset1InDetails))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid Asset1InDetails";
        return *res;
    }
    else if (auto const res = validAmount(asset2InAmount))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid Asset2InAmount";
        return *res;
    }
    else if (auto const res = validAmount(maxEP))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid MaxEP";
        return *res;
    }
    else if (present(
                 ctx.tx,
                 sfAsset1OutDetails,
                 sfAsset2OutAmount,
                 sfAssetInDetails,
                 sfAssetOutDetails,
                 sfAssetDetails,
                 sfSlippage))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "deposit fields.";
        return temBAD_AMM_OPTIONS;
    }
    return tesSUCCESS;
}

/** Preclaim deposit check. Validate if the lines are not frozen and the AMM
 * account exists.
 */
TER
preclaim(PreclaimContext const& ctx)
{
    if (isFrozen(ctx.view, ctx.tx[~sfAsset1InDetails]) ||
        isFrozen(ctx.view, ctx.tx[~sfAsset2OutAmount]))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit involves frozen asset";
        return tecFROZEN;
    }
    return tesSUCCESS;
}

/** Deposit requested assets and tokens amount into LP account.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1 deposit amount
 * @param asset2 deposit amount
 * @param lpTokens LPT deposit amount
 * @return
 */
std::pair<TER, bool>
deposit(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1,
    std::optional<STAmount> const& asset2,
    STAmount const& lpTokens)
{
    // Check account has sufficient funds
    auto balance = [&](auto const& asset) {
        return accountHolds(
                   view,
                   account,
                   asset.issue().currency,
                   asset.issue().account,
                   FreezeHandling::fhZERO_IF_FROZEN,
                   ctx.journal) >= asset;
    };

    // Deposit asset1
    if (!balance(asset1))
    {
        JLOG(ctx.journal.debug())
            << "AMM Trade: account has insufficient balance to deposit "
            << asset1;
        return {tecUNFUNDED_AMM, false};
    }
    auto res = accountSend(view, account, ammAccount, asset1, ctx.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx.journal.debug()) << "AMM Trade: failed to deposit " << asset1;
        return {res, false};
    }

    // Deposit asset2
    if (asset2)
    {
        if (!balance(*asset2))
        {
            JLOG(ctx.journal.debug())
                << "AMM Trade: account has insufficient balance to deposit "
                << *asset2;
            return {tecUNFUNDED_AMM, false};
        }
        res = accountSend(view, account, ammAccount, *asset2, ctx.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx.journal.debug())
                << "AMM Trade: failed to deposit " << *asset2;
            return {res, false};
        }
    }

    // Deposit LP tokens
    res = accountSend(view, ammAccount, account, lpTokens, ctx.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx.journal.debug()) << "AMM Trade: failed to deposit LPTokens";
        return {res, false};
    }

    return {tesSUCCESS, true};
}

/** Equal asset deposit for the specified share of the AMM instance pools.
 * Depositing assets proportionally doesn't change the assets ratio and
 * consequently doesn't change the relative pricing. Therefore the fee
 * is not charged.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param lptAMMBalance current AMM LPT balance
 * @param tokensPct percentage of the AMM instance pool in basis points
 * @return
 */
std::pair<TER, bool>
equalDepositTokens(
    const ApplyContext& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    std::uint16_t tokensPct)
{
    return deposit(
        ctx,
        view,
        ammAccount,
        account,
        getPct(asset1Balance, tokensPct),
        getPct(asset2Balance, tokensPct),
        getPct(lptAMMBalance, tokensPct));
}

/** Equal asset deposit with the constraint on the maximum amount of
 * both assets that the trader is willing to deposit. The fee is not
 * charged.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param lptAMMBalance current AMM LPT balance
 * @param asset1InDetails maximum asset1 deposit amount
 * @param asset2InAmount maximum asset2 deposit amount
 * @return
 */
std::pair<TER, bool>
equalDepositLimit(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1InDetails,
    STAmount const& asset2InAmount)
{
    auto const& issue1 = asset1Balance.issue();
    auto const& issue2 = asset2Balance.issue();
    auto const& lptIssue = lptAMMBalance.issue();
    // proportion of tokens to deposit is equal to proportion of
    // deposited asset1
    auto frac = divide(asset1InDetails, asset1Balance, issue1);
    auto tokens = multiply(frac, lptAMMBalance, lptIssue);
    if (!validLPTokens(lptAMMBalance, tokens))
        return {tecAMM_INVALID_TOKENS, false};
    auto const asset2Deposit = multiply(asset2Balance, frac, issue2);
    if (asset2Deposit <= asset2InAmount)
        return deposit(
            ctx,
            view,
            ammAccount,
            account,
            asset1InDetails,
            asset2Deposit,
            tokens);

    frac = divide(asset2InAmount, asset2Balance, issue2);
    tokens = multiply(frac, lptAMMBalance, lptIssue);
    if (!validLPTokens(lptAMMBalance, tokens))
        return {tecAMM_INVALID_TOKENS, false};
    auto const asset1Deposit = multiply(asset1Balance, frac, issue1);
    if (asset1Deposit <= asset1InDetails)
        return deposit(
            ctx,
            view,
            ammAccount,
            account,
            asset1Deposit,
            asset2InAmount,
            tokens);
    return {tecAMM_FAILED_DEPOSIT, false};
}

/** Single asset deposit by the amount. The fee is charged.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1Balance current AMM asset1 balance
 * @param lptAMMBalance current AMM LPT balance
 * @param asset1InDetails requested asset1 deposit amount
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return
 */
std::pair<TER, bool>
singleDeposit(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1InDetails,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const tokens = calcLPTokensIn(
        asset1Balance, asset1InDetails, lptAMMBalance, weight1, tfee);
    if (!tokens)
        return {tecAMM_FAILED_DEPOSIT, false};
    if (!validLPTokens(lptAMMBalance, *tokens))
        return {tecAMM_INVALID_TOKENS, false};
    return deposit(
        ctx, view, ammAccount, account, asset1InDetails, std::nullopt, *tokens);
}

/** Single asset deposit by the tokens. The trading fee is charged.
 * The pool to deposit into is determined in the applyGuts() via asset1InDetails
 * issue. The fee is charged.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1Balance current AMM asset1 balance
 * @param lptAMMBalance current AMM LPT balance
 * @param tokensPct percentage of the AMM instance pool in basis points
 * @param weight1 asset1 pool weight percentage
 * @param tfee trading fee in basis points
 * @return
 */
std::pair<TER, bool>
singleDepositTokens(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    std::uint16_t tokensPct,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const lptBalance = getPct(lptAMMBalance, tokensPct);
    auto const asset1Deposit =
        calcAssetIn(asset1Balance, lptBalance, lptAMMBalance, weight1, tfee);
    if (!asset1Deposit)
        return {tecAMM_FAILED_DEPOSIT, false};
    return deposit(
        ctx,
        view,
        ammAccount,
        account,
        *asset1Deposit,
        std::nullopt,
        lptBalance);
}

/** Single asset deposit with the constraint that the effective price
 * of the trade doesn't exceed the specified EP. The fee is charged.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param asset1InDetails requested asset1 deposit amount
 * @param lptAMMBalance current AMM LPT balance
 * @param maxSP maximum effective price
 * @param weight1
 * @param tfee
 * @return
 */
std::pair<TER, bool>
singleDepositMaxEP(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& asset1InDetails,
    STAmount const& lptAMMBalance,
    STAmount const& maxEP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const asset1BalanceUpd = asset1Balance + asset1InDetails;
    auto const ep = calcEffectivePrice(asset1BalanceUpd, asset2Balance);
    auto const asset1Deposit = [&]() -> std::optional<STAmount> {
        if (ep <= maxEP)
            return asset1InDetails;
        return changeSpotPrice(
            asset1Balance, asset2Balance, maxEP, weight1, tfee);
    }();
    if (!asset1InDetails)
        return {tecAMM_FAILED_DEPOSIT, false};
    auto const tokens = calcLPTokensIn(
        asset1Balance, *asset1Deposit, lptAMMBalance, weight1, tfee);
    if (!tokens)
        return {tecAMM_FAILED_DEPOSIT, false};
    if (!validLPTokens(lptAMMBalance, *tokens))
        return {tecAMM_INVALID_TOKENS, false};
    return deposit(
        ctx, view, ammAccount, account, *asset1Deposit, std::nullopt, *tokens);
}

std::pair<TER, bool>
applyGuts(
    ApplyContext& ctx,
    Sandbox& view,
    Sandbox& view_cancel,
    AccountID const& account)
{
    auto const asset1InDetails = ctx.tx[~sfAsset1InDetails];
    auto const asset2InAmount = ctx.tx[~sfAsset2InAmount];
    auto const maxEP = ctx.tx[~sfMaxEP];
    auto const lpTokens = ctx.tx[~sfLPTokens];
    auto const ammAccountID = ctx.tx[sfAMMAccount];
    auto const [asset1, asset2, lptAMMBalance] = getAMMReserves(
        ctx.view(),
        ammAccountID,
        std::nullopt,
        asset1InDetails ? asset1InDetails->issue() : std::optional<Issue>{},
        asset2InAmount ? asset2InAmount->issue() : std::optional<Issue>{},
        ctx.journal);

    auto const sle = view.read(keylet::account(ctx.tx[sfAMMAccount]));
    assert(sle);
    auto const tfee = sle->getFieldU32(sfTradingFee);
    auto const weight = sle->getFieldU8(sfAssetWeight);

    if (asset1InDetails && asset2InAmount)
        return equalDepositLimit(
            ctx,
            view,
            ammAccountID,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *asset1InDetails,
            *asset2InAmount);
    else if (asset1InDetails && lpTokens)
        return singleDepositTokens(
            ctx,
            view,
            ammAccountID,
            account,
            asset1,
            lptAMMBalance,
            *lpTokens,
            weight,
            tfee);
#if 0
    else if (asset1InDetails && maxEP)
        return singleDepositMaxEP(
            ctx,
            view,
            ammAccountID,
            account,
            asset1,
            asset2,
            *asset1InDetails,
            lptAMMBalance,
            *maxEP,
            weight,
            tfee);
#endif
    else if (asset1InDetails)
        return singleDeposit(
            ctx,
            view,
            ammAccountID,
            account,
            asset1,
            lptAMMBalance,
            *asset1InDetails,
            weight,
            tfee);
    else if (lpTokens)
        return equalDepositTokens(
            ctx,
            view,
            ammAccountID,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *lpTokens);

    return {tesSUCCESS, true};
}

}  // namespace deposit

/////

namespace withdraw {

NotTEC
preflight(PreflightContext const& ctx)
{
    auto const asset1OutDetails = ctx.tx[~sfAsset1OutDetails];
    auto const asset2OutAmount = ctx.tx[~sfAsset2OutAmount];
    auto const maxEP = ctx.tx[~sfMaxEP];
    auto const lpTokens = ctx.tx[~sfLPTokens];
    // Valid combinations are:
    //   LPTokens
    //   Asset1OutDetails
    //   Asset1OutDetails and Asset2OutAmount
    //   Asset1OutDetails and LPTokens
    //   Asset1OutDetails and MaxEP
    if ((!lpTokens && !asset1OutDetails) ||
        (lpTokens && (asset2OutAmount || maxEP)) ||
        (asset1OutDetails &&
         ((asset2OutAmount && (lpTokens || maxEP)) ||
          (maxEP && (asset2OutAmount || lpTokens)))))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "deposit fields.";
        return temBAD_AMM_OPTIONS;
    }
    if (lpTokens)
    {
        if (*lpTokens > 30000)
        {
            JLOG(ctx.j.debug()) << "Malformed transaction: invalid LPTokens";
            return temBAD_AMM_TOKENS;
        }
    }
    if (auto const res = validAmount(asset1OutDetails))
    {
        JLOG(ctx.j.debug())
            << "Malformed transaction: invalid Asset1OutDetails";
        return *res;
    }
    else if (auto const res = validAmount(asset2OutAmount))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid Asset2OutAmount";
        return *res;
    }
    else if (auto const res = validAmount(maxEP))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid MaxEP";
        return *res;
    }
    else if (present(
                 ctx.tx,
                 sfAsset1InDetails,
                 sfAsset2InAmount,
                 sfAssetInDetails,
                 sfAssetOutDetails,
                 sfAssetDetails,
                 sfSlippage))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "withdraw fields.";
        return temBAD_AMM_OPTIONS;
    }
    return tesSUCCESS;
}

TER
preclaim(PreclaimContext const& ctx)
{
    if (isFrozen(ctx.view, ctx.tx[~sfAsset1OutDetails]) ||
        isFrozen(ctx.view, ctx.tx[~sfAsset2OutAmount]))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw involves frozen asset";
        return tecFROZEN;
    }
    return tesSUCCESS;
}

/** Withdraw requested assets and tokens from AMM into LP account.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1 withdraw amount
 * @param asset2 withdraw amount
 * @param lpTokens LPT deposit amount
 * @return
 */
std::pair<TER, bool>
withdraw(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1,
    std::optional<STAmount> const& asset2,
    STAmount const& lpTokens)
{
    // Withdraw asset1
    auto res = accountSend(view, ammAccount, account, asset1, ctx.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx.journal.debug())
            << "AMM Instance: failed to withdraw " << asset1;
        return {res, false};
    }

    // Withdraw asset2
    if (asset2)
    {
        res = accountSend(view, ammAccount, account, *asset2, ctx.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx.journal.debug())
                << "AMM Instance: failed to withdraw " << *asset2;
            return {res, false};
        }
    }

    // Withdraw LP tokens
    res = redeemIOU(view, account, lpTokens, lpTokens.issue(), ctx.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx.journal.debug())
            << "AMM Instance: failed to withdraw LPTokens";
        return {res, false};
    }

    return {tesSUCCESS, true};
}

/** Equal-asset withdrawal of some percentage of the AMM
 * instance pools represented by the number of LPTokens .
 * Withdrawing assets proportionally does not change the assets ratio
 * and consequently does not change the relative pricing. Therefore the fee
 * is not charged.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param lptAMMBalance current AMM LPT balance
 * @param lptBalance current LPT balance
 * @param tokensPct percentage of the AMM instance pool in basis points
 * @return
 */
std::pair<TER, bool>
equalWithdrawalTokens(
    const ApplyContext& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& lptBalance,
    std::uint16_t tokensPct)
{
    auto const withdrawTokens = getPct(lptAMMBalance, tokensPct);
    if (withdrawTokens > lptBalance)
        return {tecAMM_FAILED_WITHDRAW, false};
    return withdraw(
        ctx,
        view,
        ammAccount,
        account,
        getPct(asset1Balance, tokensPct),
        getPct(asset2Balance, tokensPct),
        withdrawTokens);
}

std::pair<TER, bool>
equalWithdrawalLimit(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1InDetails,
    STAmount const& asset2InAmount)
{
    auto const& issue1 = asset1Balance.issue();
    auto const& issue2 = asset2Balance.issue();
    auto const& lptIssue = lptAMMBalance.issue();
    auto tokens = multiply(
        divide(asset1InDetails, asset1Balance, issue1),
        lptAMMBalance,
        lptIssue);
    auto const asset2Deposit = multiply(asset2Balance, tokens, issue2);
    if (asset2Deposit <= asset2InAmount)
        return withdraw(
            ctx,
            view,
            ammAccount,
            account,
            asset1InDetails,
            asset2Deposit,
            tokens);

    tokens = multiply(
        divide(asset2InAmount, asset2Balance, issue2), lptAMMBalance, lptIssue);
    auto const asset1Deposit = multiply(asset1Balance, tokens, issue1);
    if (asset1Deposit <= asset1InDetails)
        return withdraw(
            ctx,
            view,
            ammAccount,
            account,
            asset1Deposit,
            asset2InAmount,
            tokens);
    return {tecAMM_FAILED_DEPOSIT, false};
}

/** Single asset withdrawal equivalent to the amount specified
 * in Asset1OutDetails. The fee is charged.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1Balance current AMM asset1 balance
 * @param lptAMMBalance current AMM LPT balance
 * @param asset1OutDetails asset1 withdraw amount
 * @param weight asset1 weight
 * @param tfee trading fee in basis points
 * @return
 */
std::pair<TER, bool>
singleWithdrawal(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1OutDetails,
    std::uint8_t weight,
    std::uint16_t tfee)
{
    auto const tokens = calcLPTokensOut(
        asset1Balance, asset1OutDetails, lptAMMBalance, weight, tfee);
    if (!tokens)
        return {tecAMM_FAILED_WITHDRAW, false};
    if (!validLPTokens(lptAMMBalance, *tokens))
        return {tecAMM_INVALID_TOKENS, false};
    return withdraw(
        ctx,
        view,
        ammAccount,
        account,
        asset1OutDetails,
        std::nullopt,
        *tokens);
}

/** Single asset withdrawal proportional to the percentage share
 * specified by tokensPct. The fee is charged.
 * @param ctx
 * @param view
 * @param ammAccount AMM account
 * @param account LP account
 * @param asset1Balance current AMM asset1 balance
 * @param lptAMMBalance current AMM LPT balance
 * @param tokensPct percentage of the AMM instance pool in basis points
 * @param weight asset1 weight
 * @param tfee trading fee in basis points
 * @return
 */
std::pair<TER, bool>
singleWithdrawalTokens(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    std::uint16_t tokensPct,
    std::uint8_t weight,
    std::uint16_t tfee)
{
    auto const tokens = getPct(lptAMMBalance, tokensPct);
    auto tosq = STAmount{noIssue(), 1} +
        divide(STAmount{noIssue(), tokensPct},
               STAmount{noIssue(), 100000},
               noIssue());
    auto const num = multiply(tosq, tosq, noIssue()) - STAmount{noIssue(), 1};
    auto const den =
        STAmount{noIssue(), 1} -
        divide(
            STAmount{noIssue(), tfee}, STAmount{noIssue(), 200000}, noIssue());
    auto const asset1Deposit = multiply(
        asset1Balance, divide(num, den, noIssue()), asset1Balance.issue());
    return withdraw(
        ctx, view, ammAccount, account, asset1Deposit, std::nullopt, tokens);
}

#if 0  // not finalized
std::pair<TER, bool>
singleWithdrawMaxSP(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1OutDetails,
    STAmount const& maxSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    auto const asset1BalanceUpd = asset1Balance + asset1OutDetails;
    auto const sp =
        calcSpotPrice(asset1BalanceUpd, asset2Balance, weight1, tfee);
    auto const asset1Deposit = [&]() -> std::optional<STAmount> {
        if (sp <= maxSP)
            return asset1OutDetails;
        return changeSpotPrice(
            asset1Balance, asset2Balance, maxSP, weight1, tfee);
    }();
    if (!asset1OutDetails)
        return {tecAMM_FAILED_DEPOSIT, false};
    auto const tokens = calcLPTokensIn(
        asset1Balance, *asset1Deposit, lptAMMBalance, weight1, tfee);
    if (!tokens)
        return {tecAMM_FAILED_DEPOSIT, false};
    if (!validLPTokens(lptAMMBalance, *tokens))
        return {tecAMM_INVALID_TOKENS, false};
    return withdraw(ctx, view, account, *asset1Deposit, std::nullopt, *tokens);
}
#endif

std::pair<TER, bool>
applyGuts(
    ApplyContext& ctx,
    Sandbox& view,
    Sandbox& view_cancel,
    AccountID const& account)
{
    auto const asset1OutDetails = ctx.tx[~sfAsset1OutDetails];
    auto const asset2OutAmount = ctx.tx[~sfAsset2OutAmount];
    auto const maxSP = ctx.tx[~sfMaxEP];
    auto const lpTokens = ctx.tx[~sfLPTokens];
    auto const ammAccount = ctx.tx[sfAMMAccount];
    auto const [asset1, asset2, lptAMMBalance] = getAMMReserves(
        ctx.view(),
        ammAccount,
        std::nullopt,
        asset1OutDetails ? asset1OutDetails->issue() : std::optional<Issue>{},
        asset2OutAmount ? asset2OutAmount->issue() : std::optional<Issue>{},
        ctx.journal);
    auto const lptBalance =
        getAMMLPTokens(view, ammAccount, account, ctx.journal);

    auto const sle = view.read(keylet::account(ctx.tx[sfAMMAccount]));
    assert(sle);
    auto const tfee = sle->getFieldU32(sfTradingFee);
    auto const weight = sle->getFieldU8(sfAssetWeight);

    if (asset1OutDetails && asset2OutAmount)
        return equalWithdrawalLimit(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *asset1OutDetails,
            *asset2OutAmount);
    else if (asset1OutDetails && lpTokens)
        return singleWithdrawalTokens(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            lptAMMBalance,
            *lpTokens,
            weight,
            tfee);
#if 0
    else if (asset1OutDetails && maxSP)
        return singleWithdrawMaxSP(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *asset1OutDetails,
            *maxSP,
            weight,
            tfee);
#endif
    else if (asset1OutDetails)
        return singleWithdrawal(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            lptAMMBalance,
            *asset1OutDetails,
            weight,
            tfee);
    else if (lpTokens)
        return equalWithdrawalTokens(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            lptBalance,
            *lpTokens);
    return {tesSUCCESS, true};
}

}  // namespace withdraw

/////

namespace swap {

NotTEC
preflight(PreflightContext const& ctx)
{
    // Valid combinations are:
    //   AssetInDetails
    //   AssetOutDetails
    //   AssetInDetails and MaxSP
    //   AssetOutDetails and MaxSP
    //   AssetInDetails and Slippage
    //   AssetDetails and MaxSP and Slippage
    auto const assetInDetails = ctx.tx[~sfAssetInDetails];
    auto const assetOutDetails = ctx.tx[~sfAssetOutDetails];
    auto const assetDetails = ctx.tx[~sfAssetDetails];
    auto const maxSP = ctx.tx[~sfMaxSP];
    auto const slippage = ctx.tx[~sfSlippage];
    if ((!assetInDetails && !assetOutDetails && !assetDetails) ||
        (assetInDetails && assetOutDetails) || (assetOutDetails && slippage) ||
        (assetDetails && (!maxSP || !slippage)))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "swap fields.";
        return temBAD_AMM_OPTIONS;
    }
    if (auto const res = validAmount(assetInDetails))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid AssetInDetails";
        return *res;
    }
    else if (auto const res = validAmount(assetOutDetails))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid AssetOutDetails";
        return *res;
    }
    else if (auto const res = validAmount(assetDetails))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid AssetDetails";
        return *res;
    }
    else if (auto const res = validAmount(maxSP))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid MaxSP";
        return *res;
    }
    // TODO CHECK slippage
    else if (present(
                 ctx.tx,
                 sfAsset1InDetails,
                 sfAsset2InAmount,
                 sfAsset1OutDetails,
                 sfAsset2OutAmount,
                 sfMaxEP))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "withdraw fields.";
        return temBAD_AMM_OPTIONS;
    }
    return tesSUCCESS;
}

TER
preclaim(PreclaimContext const& ctx)
{
    if (isFrozen(ctx.view, ctx.tx[~sfAsset1InDetails]) ||
        isFrozen(ctx.view, ctx.tx[~sfAsset2OutAmount]) ||
        isFrozen(ctx.view, ctx.tx[~sfAssetDetails]))
    {
        JLOG(ctx.j.debug()) << "AMM Deposit involves frozen asset";
        return tecFROZEN;
    }
    return tesSUCCESS;
}

std::pair<TER, bool>
swapDetailsSlippageMaxSP(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& assetDetails,
    std::uint16_t slippage,
    STAmount const& maxSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return {tesSUCCESS, true};
}

std::pair<TER, bool>
swapInDetailsSlippage(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& assetInDetails,
    std::uint16_t slippage,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return {tesSUCCESS, true};
}

std::pair<TER, bool>
swapInDetailsMaxSP(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& assetInDetails,
    STAmount const& maxSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return {tesSUCCESS, true};
}

std::pair<TER, bool>
swapInDetails(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& assetInDetails,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return {tesSUCCESS, true};
}

std::pair<TER, bool>
swapOutDetailsMaxSP(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& assetOutDetails,
    STAmount const& maxSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return {tesSUCCESS, true};
}

std::pair<TER, bool>
swapOutDetails(
    ApplyContext const& ctx,
    Sandbox& view,
    AccountID const& ammAccount,
    AccountID const& account,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& assetOutDetails,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
    return {tesSUCCESS, true};
}

std::pair<TER, bool>
applyGuts(
    ApplyContext& ctx,
    Sandbox& view,
    Sandbox& view_cancel,
    AccountID const& account)
{
    auto const assetInDetails = ctx.tx[~sfAssetInDetails];
    auto const assetOutDetails = ctx.tx[~sfAssetOutDetails];
    auto const assetDetails = ctx.tx[~sfAssetDetails];
    auto const maxSP = ctx.tx[~sfMaxEP];
    auto const slippage = ctx.tx[~sfSlippage];
    auto const ammAccount = ctx.tx[sfAMMAccount];
    auto const issue = [&] {
        if (assetInDetails)
            return assetInDetails->issue();
        else if (assetOutDetails)
            return assetOutDetails->issue();
        else
            return assetDetails->issue();
    }();
    auto const [asset1, asset2, lptAMMBalance] = getAMMReserves(
        ctx.view(), ammAccount, std::nullopt, issue, std::nullopt, ctx.journal);
    auto const lptBalance =
        getAMMLPTokens(view, ammAccount, account, ctx.journal);

    auto const sle = view.read(keylet::account(ctx.tx[sfAMMAccount]));
    assert(sle);
    auto const tfee = sle->getFieldU32(sfTradingFee);
    auto const weight = sle->getFieldU8(sfAssetWeight);

    if (assetDetails)
        return swapDetailsSlippageMaxSP(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *assetDetails,
            *slippage,
            *maxSP,
            weight,
            tfee);
    else if (assetInDetails && slippage)
        return swapInDetailsSlippage(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *assetInDetails,
            *slippage,
            weight,
            tfee);
    else if (assetInDetails && maxSP)
        return swapInDetailsMaxSP(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *assetInDetails,
            *maxSP,
            weight,
            tfee);
    else if (assetInDetails)
        return swapInDetails(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *assetInDetails,
            weight,
            tfee);
    else if (assetOutDetails && maxSP)
        return swapOutDetailsMaxSP(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *assetOutDetails,
            *maxSP,
            weight,
            tfee);
    else if (assetOutDetails)
        return swapOutDetails(
            ctx,
            view,
            ammAccount,
            account,
            asset1,
            asset2,
            lptAMMBalance,
            *assetOutDetails,
            weight,
            tfee);
    return {tesSUCCESS, true};
}

}  // namespace swap

/*-------------------------------------------------------------------------------*/

AMMTrade::AMMTrade(ApplyContext& ctx) : Transactor(ctx)
{
}

TxConsequences
AMMTrade::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMTrade::preflight(PreflightContext const& ctx)
{
    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto& tx = ctx.tx;
    auto& j = ctx.j;

    std::uint32_t const uTxFlags = tx.getFlags();

    if (uTxFlags & tfAMMTradeMask)
    {
        JLOG(j.debug()) << "Malformed transaction: invalid flags set.";
        return temINVALID_FLAG;
    }

    if (uTxFlags & tfAMMDeposit)
        return deposit::preflight(ctx);
    else if (uTxFlags & tfAMMWithdraw)
        return withdraw::preflight(ctx);
    else if (uTxFlags & tfAMMSwap)
        return swap::preflight(ctx);
    else
    {
        JLOG(j.debug())
            << "Malformed transaction: subtransaction flags is not set.";
        return temINVALID_FLAG;
    }

    return preflight2(ctx);
}

TER
AMMTrade::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.read(keylet::account(ctx.tx[sfAMMAccount])))
    {
        JLOG(ctx.j.debug()) << "Invalid AMM account";
        return temBAD_SRC_ACCOUNT;
    }
    auto const [asset1, asset2, lptAMMBalance] = getAMMReserves(
        ctx.view,
        ctx.tx[sfAMMAccount],
        std::nullopt,
        std::nullopt,
        std::nullopt,
        ctx.j);
    if (asset1 <= beast::zero || asset2 <= beast::zero ||
        lptAMMBalance <= beast::zero)
    {
        JLOG(ctx.j.error()) << "AMMTrade: reserves or balance is zero";
        return tecAMM_BALANCE;
    }
    std::uint32_t const uTxFlags = ctx.tx.getFlags();
    if (uTxFlags & tfAMMDeposit)
        return deposit::preclaim(ctx);
    else if (uTxFlags & tfAMMWithdraw)
        return withdraw::preclaim(ctx);
    else if (uTxFlags & tfAMMSwap)
        return swap::preclaim(ctx);
    return tesSUCCESS;
}

void
AMMTrade::preCompute()
{
    return Transactor::preCompute();
}

std::pair<TER, bool>
AMMTrade::applyGuts(Sandbox& sb, Sandbox& sbCancel)
{
    std::uint32_t const uTxFlags = ctx_.tx.getFlags();
    if (uTxFlags & tfAMMDeposit)
        return deposit::applyGuts(ctx_, sb, sbCancel, account_);
    else if (uTxFlags & tfAMMWithdraw)
        return withdraw::applyGuts(ctx_, sb, sbCancel, account_);
    else if (uTxFlags & tfAMMSwap)
        return swap::applyGuts(ctx_, sb, sbCancel, account_);
    return {tesSUCCESS, true};
}

TER
AMMTrade::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel(&ctx_.view());

    auto const result = applyGuts(sb, sbCancel);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple
