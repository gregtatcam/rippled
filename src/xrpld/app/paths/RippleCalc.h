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

#ifndef RIPPLE_APP_PATHS_RIPPLECALC_H_INCLUDED
#define RIPPLE_APP_PATHS_RIPPLECALC_H_INCLUDED

#include <xrpld/ledger/PaymentSandbox.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>

#include <boost/container/flat_set.hpp>

namespace ripple {
class Config;
namespace path {

namespace detail {
struct FlowDebugInfo;
}

/** RippleCalc calculates the quality of a payment path.

    Quality is the amount of input required to produce a given output along a
    specified path - another name for this is exchange rate.
*/
template <ValidSerialAmountType TDel, typename TMax>
class RippleCalc
{
public:
    struct Input
    {
        explicit Input() = default;

        bool partialPaymentAllowed = false;
        bool defaultPathsAllowed = true;
        bool limitQuality = false;
        bool isLedgerOpen = true;
    };
    struct Output
    {
        explicit Output() = default;

        // The computed input amount.
        TMax actualAmountIn;

        // The computed output amount.
        TDel actualAmountOut;

        // Collection of offers found expired or unfunded. When a payment
        // succeeds, unfunded and expired offers are removed. When a payment
        // fails, they are not removed. This vector contains the offers that
        // could have been removed but were not because the payment fails. It is
        // useful for offer crossing, which does remove the offers.
        boost::container::flat_set<uint256> removableOffers;

    private:
        TER calculationResult_ = temUNKNOWN;

    public:
        TER
        result() const
        {
            return calculationResult_;
        }
        void
        setResult(TER const value)
        {
            calculationResult_ = value;
        }
    };

    static Output
    rippleCalculate(
        PaymentSandbox& view,

        // Compute paths using this ledger entry set.  Up to caller to actually
        // apply to ledger.

        // Issuer:
        //      XRP: xrpAccount()
        //  non-XRP: uSrcAccountID (for any issuer) or another account with
        //           trust node.
        TMax const& saMaxAmountReq,  // --> -1 = no limit.

        // Issuer:
        //      XRP: xrpAccount()
        //  non-XRP: uDstAccountID (for any issuer) or another account with
        //           trust node.
        TDel const& saDstAmountReq,

        AccountID const& uDstAccountID,
        AccountID const& uSrcAccountID,

        // A set of paths that are included in the transaction that we'll
        // explore for liquidity.
        STPathSet const& spsPaths,
        Logs& l,
        Input const* const pInputs = nullptr);

    // The view we are currently working on
    PaymentSandbox& view;

    // If the transaction fails to meet some constraint, still need to delete
    // unfunded offers in a deterministic order (hence the ordered container).
    //
    // Offers that were found unfunded.
    boost::container::flat_set<uint256> permanentlyUnfundedOffers_;
};

template <ValidSerialAmountType TDel, typename TMax>
RippleCalc<TDel, TMax>::Output
RippleCalc<TDel, TMax>::rippleCalculate(
        PaymentSandbox& view,

        // Compute paths using this ledger entry set.  Up to caller to actually
        // apply to ledger.

        // Issuer:
        //      XRP: xrpAccount()
        //  non-XRP: uSrcAccountID (for any issuer) or another account with
        //           trust node.
        TMax const& saMaxAmountReq,  // --> -1 = no limit.

        // Issuer:
        //      XRP: xrpAccount()
        //  non-XRP: uDstAccountID (for any issuer) or another account with
        //           trust node.
        TDel const& saDstAmountReq,

        AccountID const& uDstAccountID,
        AccountID const& uSrcAccountID,

        // A set of paths that are included in the transaction that we'll
        // explore for liquidity.
        STPathSet const& spsPaths,
        Logs& l,
        Input const* const pInputs)
{
    Output flowOut;
    PaymentSandbox flowSB(&view);
    auto j = l.journal("Flow");

    if (!view.rules().enabled(featureFlow))
    {
        // The new payment engine was enabled several years ago. New transaction
        // should never use the old rules. Assume this is a replay
        j.fatal()
                << "Old payment rules are required for this transaction. Assuming "
                   "this is a replay and running with the new rules.";
    }

    {
        bool const defaultPaths =
                !pInputs ? true : pInputs->defaultPathsAllowed;

        bool const partialPayment =
                !pInputs ? false : pInputs->partialPaymentAllowed;

        auto const limitQuality = [&]() -> std::optional<Quality> {
            if (pInputs && pInputs->limitQuality &&
                saMaxAmountReq > beast::zero)
                return Quality{Amounts(saMaxAmountReq, saDstAmountReq)};
            return std::nullopt;
        }();

        auto const sendMax = [&]() -> std::optional<STAmount> {
            if (saMaxAmountReq >= beast::zero ||
                saMaxAmountReq.getCurrency() != saDstAmountReq.getCurrency() ||
                saMaxAmountReq.getIssuer() != uSrcAccountID)
            {
                return saMaxAmountReq;
            }
            return std::nullopt;
        }();

        bool const ownerPaysTransferFee =
                view.rules().enabled(featureOwnerPaysFee);

        try
        {
            flowOut = flow(
                    flowSB,
                    saDstAmountReq,
                    uSrcAccountID,
                    uDstAccountID,
                    spsPaths,
                    defaultPaths,
                    partialPayment,
                    ownerPaysTransferFee,
                    OfferCrossing::no,
                    limitQuality,
                    sendMax,
                    j,
                    nullptr);
        }
        catch (std::exception& e)
        {
            JLOG(j.error()) << "Exception from flow: " << e.what();

            // return a tec so the tx is stored
            path::RippleCalc<TDel, TMax>::Output exceptResult;
            exceptResult.setResult(tecINTERNAL);
            return exceptResult;
        }
    }

    j.debug() << "RippleCalc Result> "
              << " actualIn: " << flowOut.actualAmountIn
              << ", actualOut: " << flowOut.actualAmountOut
              << ", result: " << flowOut.result()
              << ", dstAmtReq: " << saDstAmountReq
              << ", sendMax: " << saMaxAmountReq;

    flowSB.apply(view);
    return flowOut;
}

}  // namespace path
}  // namespace ripple

#endif
