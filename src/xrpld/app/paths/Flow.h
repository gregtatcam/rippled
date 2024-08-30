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

#ifndef RIPPLE_APP_PATHS_FLOW_H_INCLUDED
#define RIPPLE_APP_PATHS_FLOW_H_INCLUDED

#include <xrpld/app/paths/RippleCalc.h>
#include <xrpld/app/paths/detail/Steps.h>
#include <xrpl/protocol/Quality.h>
#include <xrpld/app/paths/AMMContext.h>

namespace ripple {

namespace path {
namespace detail {
struct FlowDebugInfo;
}
}  // namespace path

namespace detail {

template <ValidSerialAmountType A>
using issueType = std::conditional_t<std::is_same_v<A, STAmount>, Issue, MPTIssue>;

template <ValidSerialAmountType TDel, ValidSerialAmountType TMax, typename FlowResult>
auto
finishFlow(
        PaymentSandbox &sb,
        issueType<TDel> const &srcIssue,
        issueType<TMax> const &dstIssue,
        FlowResult &&f) {
    typename path::RippleCalc<TDel, TMax>::Output result;
    if (f.ter == tesSUCCESS)
        f.sandbox->apply(sb);
    else
        result.removableOffers = std::move(f.removableOffers);

    result.setResult(f.ter);
    result.actualAmountIn = toSTAmount(f.in, srcIssue);
    result.actualAmountOut = toSTAmount(f.out, dstIssue);

    return result;
};

}

/**
  Make a payment from the src account to the dst account

  @param view Trust lines and balances
  @param deliver Amount to deliver to the dst account
  @param src Account providing input funds for the payment
  @param dst Account receiving the payment
  @param paths Set of paths to explore for liquidity
  @param defaultPaths Include defaultPaths in the path set
  @param partialPayment If the payment cannot deliver the entire
           requested amount, deliver as much as possible, given the constraints
  @param ownerPaysTransferFee If true then owner, not sender, pays fee
  @param offerCrossing If Yes or Sell then flow is executing offer crossing, not
  payments
  @param limitQuality Do not use liquidity below this quality threshold
  @param sendMax Do not spend more than this amount
  @param j Journal to write journal messages to
  @param flowDebugInfo If non-null a pointer to FlowDebugInfo for debugging
  @return Actual amount in and out, and the result code
*/
template <ValidSerialAmountType TDel, typename TMax>
path::RippleCalc<TDel, TMax>::Output
flow(
        PaymentSandbox& sb,
        TDel const& deliver,
        AccountID const& src,
        AccountID const& dst,
        STPathSet const& paths,
        bool defaultPaths,
        bool partialPayment,
        bool ownerPaysTransferFee,
        OfferCrossing offerCrossing,
        std::optional<Quality> const& limitQuality,
        TMax const& sendMax,
        beast::Journal j,
        path::detail::FlowDebugInfo* flowDebugInfo = nullptr)
{
    auto const srcIssue = [&] {
        if (sendMax)
            return sendMax->issue();
        if (!isXRP(deliver.issue().currency))
            return Issue(deliver.issue().currency, src);
        return xrpIssue();
    }();

    auto const dstIssue = deliver.issue();

    std::optional<Issue> sendMaxIssue;
    if (sendMax)
        sendMaxIssue = sendMax->issue();

    AMMContext ammContext(src, false);

    // convert the paths to a collection of strands. Each strand is the
    // collection of account->account steps and book steps that may be used in
    // this payment.
    auto [toStrandsTer, strands] = toStrands(
            sb,
            src,
            dst,
            dstIssue,
            limitQuality,
            sendMaxIssue,
            paths,
            defaultPaths,
            ownerPaysTransferFee,
            offerCrossing,
            ammContext,
            j);

    if (toStrandsTer != tesSUCCESS)
    {
        typename path::RippleCalc<TDel, TMax>::Output result;
        result.setResult(toStrandsTer);
        return result;
    }

    ammContext.setMultiPath(strands.size() > 1);

    if (j.trace())
    {
        j.trace() << "\nsrc: " << src << "\ndst: " << dst
                  << "\nsrcIssue: " << srcIssue << "\ndstIssue: " << dstIssue;
        j.trace() << "\nNumStrands: " << strands.size();
        for (auto const& curStrand : strands)
        {
            j.trace() << "NumSteps: " << curStrand.size();
            for (auto const& step : curStrand)
            {
                j.trace() << '\n' << *step << '\n';
            }
        }
    }

    const bool srcIsXRP = isXRP(srcIssue);
    const bool dstIsXRP = isXRP(dstIssue);

    auto const asDeliver = toAmountSpec(deliver);

    // The src account may send either xrp or iou. The dst account may receive
    // either xrp or iou. Since XRP and IOU amounts are represented by different
    // types, use templates to tell `flow` about the amount types.
    if (srcIsXRP && dstIsXRP)
    {
        return detail::finishFlow(
                sb,
                srcIssue,
                dstIssue,
                flow<XRPAmount, XRPAmount>(
                        sb,
                        strands,
                        asDeliver.xrp,
                        partialPayment,
                        offerCrossing,
                        limitQuality,
                        sendMax,
                        j,
                        ammContext,
                        flowDebugInfo));
    }

    if (srcIsXRP && !dstIsXRP)
    {
        return detail::finishFlow(
                sb,
                srcIssue,
                dstIssue,
                flow<XRPAmount, IOUAmount>(
                        sb,
                        strands,
                        asDeliver.iou,
                        partialPayment,
                        offerCrossing,
                        limitQuality,
                        sendMax,
                        j,
                        ammContext,
                        flowDebugInfo));
    }

    if (!srcIsXRP && dstIsXRP)
    {
        return detail::finishFlow(
                sb,
                srcIssue,
                dstIssue,
                flow<IOUAmount, XRPAmount>(
                        sb,
                        strands,
                        asDeliver.xrp,
                        partialPayment,
                        offerCrossing,
                        limitQuality,
                        sendMax,
                        j,
                        ammContext,
                        flowDebugInfo));
    }

    assert(!srcIsXRP && !dstIsXRP);
    return detail::finishFlow(
            sb,
            srcIssue,
            dstIssue,
            flow<IOUAmount, IOUAmount>(
                    sb,
                    strands,
                    asDeliver.iou,
                    partialPayment,
                    offerCrossing,
                    limitQuality,
                    sendMax,
                    j,
                    ammContext,
                    flowDebugInfo));
}

}  // namespace ripple

#endif
