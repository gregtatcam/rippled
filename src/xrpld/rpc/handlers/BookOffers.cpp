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

#include <xrpld/app/ledger/LedgerMaster.h>
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

static std::optional<Json::Value>
isInvalidField(
    Json::Value const& param,
    std::string const& field,
    bool mptV2Enabled)
{
    std::string err = field + ".currency";
    auto isValidStrField = [&](auto const& name) {
        return param.isMember(name) && !param[name].isString();
    };

    if (!param.isObjectOrNull())
        return RPC::object_field_error(field);

    if (!param.isMember(jss::currency) &&
        (!mptV2Enabled || !param.isMember(jss::mpt_issuance_id)))
        return RPC::missing_field_error(err);

    if ((param.isMember(jss::currency) || param.isMember(jss::issuer)) &&
        param.isMember(jss::mpt_issuance_id))
        return RPC::make_param_error(
            "invalid currency/issuer with mpt_issuance_id");

    if (!isValidStrField(jss::currency) ||
        !isValidStrField(jss::mpt_issuance_id))
        return RPC::expected_field_error(err, "string");

    return std::nullopt;
}

static Expected<AccountID, Json::Value>
getIssuer(
    Currency const& currency,
    Json::Value const& param,
    std::string const& field,
    error_code_i err)
{
    AccountID issuer;
    if (!param[jss::issuer].isString())
        return Unexpected(RPC::expected_field_error(
            std::format("{}.issuer", field), "string"));

    if (!to_issuer(issuer, param[jss::issuer].asString()))
        return Unexpected(RPC::make_error(
            err, std::format("Invalid field '{}.issuer', bad issuer.", field)));

    if (issuer == noAccount())
        return Unexpected(RPC::make_error(
            err,
            std::format(
                "Invalid field '{}.issuer', bad issuer account one.", field)));

    if (isXRP(currency) && !isXRP(issuer))
        return Unexpected(RPC::make_error(
            err,
            std::format(
                "Unneeded field '{}.issuer' for "
                "XRP currency specification.",
                field)));

    if (!isXRP(currency) && isXRP(issuer))
        return Unexpected(RPC::make_error(
            err,
            std::format(
                "Invalid field '{}.issuer', expected non-XRP issuer.", field)));

    return issuer;
}

static Expected<std::variant<Issue, MPTIssue>, Json::Value>
getIssue(Json::Value const& param, std::string const& field, beast::Journal j)
{
    error_code_i curErr = rpcSRC_CUR_MALFORMED;
    error_code_i issErr = rpcSRC_ISR_MALFORMED;
    if (field == jss::taker_gets)
    {
        curErr = rpcDST_AMT_MALFORMED;
        issErr = rpcDST_ISR_MALFORMED;
    }

    if (param.isMember(jss::currency))
    {
        Currency currency;

        if (!to_currency(currency, param[jss::currency].asString()))
        {
            JLOG(j.info()) << std::format("Bad {} currency.", field);
            return Unexpected(RPC::make_error(
                curErr,
                std::format(
                    "Invalid field '{}.currency', bad currency.", field)));
        }

        AccountID issuer = xrpAccount();
        if (param.isMember(jss::issuer))
        {
            if (auto const res = getIssuer(currency, param, field, issErr);
                !res)
                return Unexpected(res.error());
            else
                issuer = *res;
        }

        return Issue{currency, issuer};
    }
    else
    {
        MPTID id;
        if (!id.parseHex(param[jss::mpt_issuance_id].asString()))
            return Unexpected(RPC::make_error(rpcMPT_ISS_ID_MALFORMED));

        return MPTIssue{id};
    }
}

static Expected<Book, Json::Value>
getBook(
    Json::Value const& takerPays,
    Json::Value const& takerGets,
    beast::Journal j)
{
    std::variant<Issue, MPTIssue> takerPaysIssue;
    std::variant<Issue, MPTIssue> takerGetsIssue;

    if (auto res = getIssue(takerPays, jss::taker_pays.c_str(), j); !res)
        return Unexpected(res.error());
    else
        takerPaysIssue = *res;

    if (auto res = getIssue(takerGets, jss::taker_gets.c_str(), j); !res)
        return Unexpected(res.error());
    else
        takerGetsIssue = *res;

    return std::visit(
        [&](auto&& in, auto&& out) {
            return Book{in, out};
        },
        takerPaysIssue,
        takerGetsIssue);
}

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

    if (!context.params.isMember(jss::taker_pays))
        return RPC::missing_field_error(jss::taker_pays);

    if (!context.params.isMember(jss::taker_gets))
        return RPC::missing_field_error(jss::taker_gets);

    Json::Value const& taker_pays = context.params[jss::taker_pays];
    Json::Value const& taker_gets = context.params[jss::taker_gets];

    bool const mptV2Enabled =
        context.ledgerMaster.getCurrentLedger()->rules().enabled(
            featureMPTokensV2);

    if (auto const err =
            isInvalidField(taker_pays, jss::taker_pays.c_str(), mptV2Enabled))
        return *err;

    if (auto const err =
            isInvalidField(taker_gets, jss::taker_gets.c_str(), mptV2Enabled))
        return *err;

    auto book = getBook(taker_pays, taker_gets, context.j);
    if (!book)
        return book.error();

    std::optional<AccountID> takerID;
    if (context.params.isMember(jss::taker))
    {
        if (!context.params[jss::taker].isString())
            return RPC::expected_field_error(jss::taker, "string");

        takerID = parseBase58<AccountID>(context.params[jss::taker].asString());
        if (!takerID)
            return RPC::invalid_field_error(jss::taker);
    }

    if constexpr (std::is_same_v<decltype(book->in), decltype(book->out)>)
    {
        if (book->in == book->out)
        {
            JLOG(context.j.info()) << "taker_gets same as taker_pays.";
            return RPC::make_error(rpcBAD_MARKET);
        }
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
        *book,
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
