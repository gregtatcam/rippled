#ifndef XRPL_APP_PATHS_ACCOUNTCURRENCIES_H_INCLUDED
#define XRPL_APP_PATHS_ACCOUNTCURRENCIES_H_INCLUDED

#include <xrpld/app/paths/AssetCache.h>

#include <xrpl/protocol/UintTypes.h>

namespace ripple {

hash_set<PathAsset>
accountDestAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& cache,
    bool includeXRP);

hash_set<PathAsset>
accountSourceAssets(
    AccountID const& account,
    std::shared_ptr<AssetCache> const& lrLedger,
    bool includeXRP);

}  // namespace ripple

#endif
