//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/app/tx/impl/CFTokenTrust.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/st.h>

namespace ripple {

NotTEC
CFTokenTrust::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureCFTokensV1))
        return temDISABLED;

    if (auto const ret = preflight1(ctx); !isTesSuccess(ret))
        return ret;

    return preflight2(ctx);
}

TER
CFTokenTrust::preclaim(PreclaimContext const& ctx)
{
    // ensure that issuance exists
    auto const sleCFT =
        ctx.view.read(keylet::cftIssuance(ctx.tx[sfCFTokenIssuanceID]));
    if (!sleCFT)
        return tecOBJECT_NOT_FOUND;

    return tesSUCCESS;
}

TER
CFTokenTrust::doApply()
{
    if (!view().exists(keylet::cftIssuance(ctx_.tx[sfCFTokenIssuanceID])))
        return tecINTERNAL;

    // for now just create CFToken and skip the page
    auto const cftokenID =
        keylet::cftoken(account_, ctx_.tx[sfCFTokenIssuanceID]);

    auto const ownerNode = view().dirInsert(
        keylet::ownerDir(account_), cftokenID, describeOwnerDir(account_));

    if (!ownerNode)
        return tecDIR_FULL;

    auto cftoken = std::make_shared<SLE>(cftokenID);
    (*cftoken)[sfCFTokenIssuanceID] = ctx_.tx[sfCFTokenIssuanceID];
    (*cftoken)[sfCFTAmount] = 0;
    (*cftoken)[sfCFTLockedAmount] = 0;
    (*cftoken)[sfOwnerNode] = *ownerNode;
    (*cftoken)[sfFlags] = 0;

    view().insert(cftoken);

    adjustOwnerCount(view(), view().peek(keylet::account(account_)), 1, j_);

    return tesSUCCESS;
}

}  // namespace ripple
