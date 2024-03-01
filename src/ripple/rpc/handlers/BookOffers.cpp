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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/beast/utility/Zero.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/BookChanges.h>
#include <ripple/rpc/Context.h>
#include <ripple/rpc/impl/RPCHelpers.h>

#include <boost/format.hpp>

namespace ripple {

namespace {

class AssetHelper
{
private:
    Json::Value const& takerField_;
    Json::StaticString name_;

public:
    AssetHelper(Json::Value const& params, Json::StaticString const& name)
        : takerField_(params[name]), name_(name)
    {
    }

    operator bool() const
    {
        return takerField_.isObjectOrNull();
    }

    std::optional<Json::Value>
    invalidAssetField()
    {
        using namespace boost;
        auto const currencyName = str(format("%1%.currency") % name_.c_str());

        if (!takerField_.isMember(jss::currency) &&
            !takerField_.isMember(jss::mpt_issuance_id))
            return RPC::missing_field_error(currencyName);

        if ((takerField_.isMember(jss::currency) &&
             takerField_.isMember(jss::mpt_issuance_id)) ||
            (takerField_.isMember(jss::mpt_issuance_id) &&
             takerField_.isMember(jss::issuer)))
            return RPC::invalid_field_error(currencyName);

        auto const assetField = [&]() {
            if (takerField_.isMember(jss::currency))
                return jss::currency;
            return jss::mpt_issuance_id;
        }();

        if (!takerField_[assetField].isString())
            return RPC::expected_field_error(currencyName, "string");

        return std::nullopt;
    }

    Expected<std::variant<Currency, MPT>, Json::Value>
    getAssetType(beast::Journal j)
    {
        auto const errc = name_ == jss::taker_pays ? rpcSRC_CUR_MALFORMED
                                                   : rpcDST_AMT_MALFORMED;
        {
            using namespace boost;
            auto const currencyName =
                str(format("%1%.currency") % name_.c_str());

            if (takerField_.isMember(jss::currency))
            {
                Currency currency;
                if (!to_currency(
                        currency, takerField_[jss::currency].asString()))
                {
                    JLOG(j.info())
                        << str(format("Bad %1% currency.") % name_.c_str());
                    return Unexpected(RPC::make_error(
                        errc,
                        str(format("Invalid field '%1%', bad currency.") %
                            currencyName)));
                }
                return currency;
            }
        }

        uint192 u;
        if (!u.parseHex(takerField_[jss::mpt_issuance_id].asString()))
            return Unexpected(RPC::make_error(errc, "Invalid MPT field"));
        auto const mpt = getMPT(u);
        if (mpt.second == beast::zero)
            return Unexpected(RPC::make_error(errc, "Invalid MPT field"));
        return mpt;
    }

    Expected<Asset, Json::Value>
    getAsset(std::variant<Currency, MPT> const& assetType)
    {
        Asset asset;
        using namespace boost;
        auto const issuerName = str(format("%1%.issuer") % name_.c_str());
        auto const errc = name_ == jss::taker_pays ? rpcSRC_ISR_MALFORMED
                                                   : rpcDST_ISR_MALFORMED;

        if (std::holds_alternative<Currency>(assetType))
        {
            AccountID issuer;

            if (takerField_.isMember(jss::issuer))
            {
                if (!takerField_[jss::issuer].isString())
                    return Unexpected(
                        RPC::expected_field_error(issuerName, "string"));

                if (!to_issuer(issuer, takerField_[jss::issuer].asString()))
                    return Unexpected(RPC::make_error(
                        errc,
                        str(format("Invalid field '%1%', bad issuer.") %
                            issuerName)));

                if (issuer == noAccount())
                    return Unexpected(RPC::make_error(
                        errc,
                        str(format("Invalid field '%1%', bad issuer account "
                                   "one.") %
                            issuerName)));
            }
            else
            {
                issuer = xrpAccount();
            }

            auto const& currency = std::get<Currency>(assetType);
            if (isXRP(currency) && !isXRP(issuer))
                return Unexpected(RPC::make_error(
                    errc,
                    str(format("Unneeded field '%1%' for "
                               "XRP currency specification.") %
                        issuerName)));

            if (!isXRP(currency) && isXRP(issuer))
                return Unexpected(RPC::make_error(
                    errc,
                    str(format(
                            "Invalid field '%1%', expected non-XRP issuer.") %
                        issuerName)));

            asset = Issue{currency, issuer};
        }
        else if (takerField_.isMember(jss::issuer))
        {
            return Unexpected(RPC::invalid_field_error(
                str(format("Invalid field '%1%', should not be included for "
                           "MPT.") %
                    issuerName)));
        }
        else
        {
            asset = std::get<MPT>(assetType);
        }

        return asset;
    }
};

}  // namespace

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

    AssetHelper paysHelper(context.params, jss::taker_pays);
    AssetHelper getsHelper(context.params, jss::taker_gets);

    if (!paysHelper)
        return RPC::object_field_error(jss::taker_pays);

    if (!getsHelper)
        return RPC::object_field_error(jss::taker_gets);

    if (auto const res = paysHelper.invalidAssetField())
        return *res;

    if (auto const res = getsHelper.invalidAssetField())
        return *res;

    auto const paysAssetType = paysHelper.getAssetType(context.j);
    if (!paysAssetType)
        return paysAssetType.error();

    auto const getsAssetType = getsHelper.getAssetType(context.j);
    if (!getsAssetType)
        return getsAssetType.error();

    auto const paysAsset = paysHelper.getAsset(*paysAssetType);
    if (!paysAsset)
        return paysAsset.error();

    auto const getsAsset = getsHelper.getAsset(*getsAssetType);
    if (!getsAsset)
        return getsAsset.error();

    std::optional<AccountID> takerID;
    if (context.params.isMember(jss::taker))
    {
        if (!context.params[jss::taker].isString())
            return RPC::expected_field_error(jss::taker, "string");

        takerID = parseBase58<AccountID>(context.params[jss::taker].asString());
        if (!takerID)
            return RPC::invalid_field_error(jss::taker);
    }

    if (*paysAsset == *getsAsset)
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
        {*paysAsset, *getsAsset},
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
