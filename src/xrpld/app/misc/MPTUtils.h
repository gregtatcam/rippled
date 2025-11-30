#ifndef XRPL_APP_MISC_MPTUTILS_H_INLCUDED
#define XRPL_APP_MISC_MPTUTILS_H_INLCUDED

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/UintTypes.h>

namespace ripple {

class Asset;
class ReadView;

/* Return true if a transaction is allowed for the specified MPT/account. The
 * function checks MPTokenIssuance and MPToken objects flags to determine if the
 * transaction is allowed.
 */
TER
checkMPTTxAllowed(
    ReadView const& v,
    TxType tx,
    Asset const& asset,
    AccountID const& accountID,
    std::optional<AccountID> const& destAccount = std::nullopt);

TER
checkMPTDEXAllowed(
    ReadView const& view,
    Asset const& issuanceID,
    AccountID const& srcAccount,
    std::optional<AccountID> const& destAccount);

inline std::int64_t
maxMPTAmount(SLE const& sleIssuance)
{
    return sleIssuance[~sfMaximumAmount].value_or(maxMPTokenAmount);
}

}  // namespace ripple

#endif  // XRPL_APP_MISC_MPTUTILS_H_INLCUDED
