#include <xrpld/app/misc/MPTUtils.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>

namespace xrpl {

static TER
checkMPTAllowed(
    ReadView const& view,
    TxType txType,
    Asset const& asset,
    AccountID const& accountID,
    std::optional<AccountID> const& destAccount)
{
    if (!asset.holds<MPTIssue>())
        return tesSUCCESS;

    auto const& issuanceID = asset.get<MPTIssue>().getMptID();
    auto const isDEX = txType == ttPAYMENT && destAccount;
    auto const validTx = txType == ttAMM_CREATE || txType == ttAMM_DEPOSIT ||
        txType == ttAMM_WITHDRAW || txType == ttOFFER_CREATE ||
        txType == ttCHECK_CREATE || txType == ttCHECK_CASH ||
        txType == ttPAYMENT || isDEX;
    XRPL_ASSERT(validTx, "xrpl::checkMPTAllowed : all MPT tx or DEX");
    if (!validTx)
        return tefINTERNAL;

    auto const& issuer = asset.getIssuer();
    if (!view.exists(keylet::account(issuer)))
        return tecNO_ISSUER;

    auto const issuanceKey = keylet::mptIssuance(issuanceID);
    auto const issuanceSle = view.read(issuanceKey);
    if (!issuanceSle)
        return tecOBJECT_NOT_FOUND;

    auto const flags = issuanceSle->getFlags();

    if (flags & lsfMPTLocked)
        return tecLOCKED;
    // Offer crossing and Payment
    if ((flags & lsfMPTCanTrade) == 0 && isDEX)
        return tecNO_PERMISSION;

    if (accountID != issuer)
    {
        if ((flags & lsfMPTCanTransfer) == 0 &&
            (!destAccount || destAccount != issuer))
            return tecNO_PERMISSION;

        auto const mptSle =
            view.read(keylet::mptoken(issuanceKey.key, accountID));
        // Allow to succeed since some tx create MPToken if it doesn't exist.
        // Tx's have their own check for missing MPToken.
        if (!mptSle)
            return tesSUCCESS;

        if ((mptSle->getFlags() & lsfMPTLocked) &&
            (!destAccount || destAccount != issuer))
            return tecLOCKED;
    }

    return tesSUCCESS;
}

TER
checkMPTTxAllowed(
    ReadView const& view,
    TxType txType,
    Asset const& asset,
    AccountID const& accountID,
    std::optional<AccountID> const& destAccount)
{
    // use isDEXAllowed for payment/offer crossing
    XRPL_ASSERT(txType != ttPAYMENT, "xrpl::checkMPTTxAllowed : not payment");
    return checkMPTAllowed(view, txType, asset, accountID, destAccount);
}

TER
checkMPTDEXAllowed(
    ReadView const& view,
    Asset const& asset,
    AccountID const& accountID,
    std::optional<AccountID> const& dest)
{
    // use ttPAYMENT for any DEX transaction
    return checkMPTAllowed(view, ttPAYMENT, asset, accountID, dest);
}

}  // namespace xrpl
