//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/ledger/ReadView.h>
#include <xrpld/rpc/BookChanges.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/detail/RPCHelpers.h>
#include <xrpl/basics/Log.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/RPCErr.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

namespace ripple {

Json::Value
doBookOffers(RPC::JsonContext& context)
{
    // VFALCO TODO Here is a terrible place for this kind of business
    //             logic. It needs to be moved elsewhere and documented,
    //             and encapsulated into a function.
    if (context.app.getJobQueue().getJobCountGE(jtCLIENT) > 200)
        return rpcError(rpcTOO_BUSY);

    std::shared_ptr<ReadView const> lpLedger;
    auto jvResult = RPC::lookupLedger(lpLedger, context);

    if (!lpLedger)
        return jvResult;

    Book book;

    auto const takerPaysRes =
        RPC::parseJSONBookAsset(context.params, jss::taker_pays.c_str());
    if (!takerPaysRes)
        return getBookOffersParseError(
            takerPaysRes.error(),
            jss::taker_pays.c_str(),
            rpcSRC_CUR_MALFORMED,
            rpcSRC_ISR_MALFORMED,
            context.j);
    book.in = *takerPaysRes;

    auto const takerGetsRes =
        RPC::parseJSONBookAsset(context.params, jss::taker_gets.c_str());
    if (!takerGetsRes)
        return getBookOffersParseError(
            takerGetsRes.error(),
            jss::taker_gets.c_str(),
            rpcDST_AMT_MALFORMED,
            rpcDST_ISR_MALFORMED,
            context.j);
    book.out = *takerGetsRes;

    std::optional<AccountID> takerID;
    if (context.params.isMember(jss::taker))
    {
        if (!context.params[jss::taker].isString())
            return RPC::expected_field_error(jss::taker, "string");

        takerID = parseBase58<AccountID>(context.params[jss::taker].asString());
        if (!takerID)
            return RPC::invalid_field_error(jss::taker);
    }

    if (book.in == book.out)
    {
        JLOG(context.j.info()) << "taker_gets same as taker_pays.";
        return RPC::make_error(rpcBAD_MARKET);
    }

    unsigned int limit;
    if (auto err = readLimitField(limit, RPC::Tuning::bookOffers, context))
        return *err;

    bool const bProof(context.params.isMember(jss::proof));

    Json::Value const jvMarker(
        context.params.isMember(jss::marker) ? context.params[jss::marker]
                                             : Json::Value(Json::nullValue));

    context.netOps.getBookPage(
        lpLedger,
        book,
        takerID ? *takerID : beast::zero,
        bProof,
        limit,
        jvMarker,
        jvResult);

    context.loadType = Resource::feeMediumBurdenRPC;

    return jvResult;
}

Json::Value
doBookChanges(RPC::JsonContext& context)
{
    auto res = RPC::getLedgerByContext(context);

    if (std::holds_alternative<Json::Value>(res))
        return std::get<Json::Value>(res);

    return RPC::computeBookChanges(
        std::get<std::shared_ptr<Ledger const>>(res));
}

}  // namespace ripple
