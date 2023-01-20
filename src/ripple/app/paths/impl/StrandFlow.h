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

#ifndef RIPPLE_APP_PATHS_IMPL_STRANDFLOW_H_INCLUDED
#define RIPPLE_APP_PATHS_IMPL_STRANDFLOW_H_INCLUDED

#include <ripple/app/paths/AMMContext.h>
#include <ripple/app/paths/Credit.h>
#include <ripple/app/paths/Flow.h>
#include <ripple/app/paths/impl/AmountSpec.h>
#include <ripple/app/paths/impl/FlatSets.h>
#include <ripple/app/paths/impl/FlowDebugInfo.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/XRPAmount.h>
#include <ripple/protocol/Feature.h>

#include <boost/container/flat_set.hpp>

#include <algorithm>
#include <iterator>
#include <numeric>
#include <sstream>

namespace ripple {

/** Result of flow() execution of a single Strand. */
template <class TInAmt, class TOutAmt>
struct StrandResult
{
    bool success;                                  ///< Strand succeeded
    TInAmt in = beast::zero;                       ///< Currency amount in
    TOutAmt out = beast::zero;                     ///< Currency amount out
    std::optional<PaymentSandbox> sandbox;         ///< Resulting Sandbox state
    boost::container::flat_set<uint256> ofrsToRm;  ///< Offers to remove
    // Num offers consumed or partially consumed (includes expired and unfunded
    // offers)
    std::uint32_t ofrsUsed = 0;
    // strand can be inactive if there is no more liquidity or too many offers
    // have been consumed
    bool inactive = false;  ///< Strand should not considered as a further
                            ///< source of liquidity (dry)

    /** Strand result constructor */
    StrandResult() = default;

    StrandResult(
        Strand const& strand,
        TInAmt const& in_,
        TOutAmt const& out_,
        PaymentSandbox&& sandbox_,
        boost::container::flat_set<uint256> ofrsToRm_,
        bool inactive_)
        : success(true)
        , in(in_)
        , out(out_)
        , sandbox(std::move(sandbox_))
        , ofrsToRm(std::move(ofrsToRm_))
        , ofrsUsed(offersUsed(strand))
        , inactive(inactive_)
    {
    }

    StrandResult(
        Strand const& strand,
        boost::container::flat_set<uint256> ofrsToRm_)
        : success(false)
        , ofrsToRm(std::move(ofrsToRm_))
        , ofrsUsed(offersUsed(strand))
    {
    }
};

/**
   Request `out` amount from a strand

   @param baseView Trust lines and balances
   @param strand Steps of Accounts to ripple through and offer books to use
   @param maxIn Max amount of input allowed
   @param out Amount of output requested from the strand
   @param j Journal to write log messages to
   @return Actual amount in and out from the strand, errors, offers to remove,
           and payment sandbox
 */
template <class TInAmt, class TOutAmt>
StrandResult<TInAmt, TOutAmt>
flow(
    PaymentSandbox const& baseView,
    Strand const& strand,
    std::optional<TInAmt> const& maxIn,
    TOutAmt const& out,
    beast::Journal j)
{
    using Result = StrandResult<TInAmt, TOutAmt>;
    if (strand.empty())
    {
        JLOG(j.warn()) << "Empty strand passed to Liquidity";
        return {};
    }

    boost::container::flat_set<uint256> ofrsToRm;

    if (isDirectXrpToXrp<TInAmt, TOutAmt>(strand))
    {
        return Result{strand, std::move(ofrsToRm)};
    }

    try
    {
        std::size_t const s = strand.size();

        std::size_t limitingStep = strand.size();
        std::optional<PaymentSandbox> sb(&baseView);
        // The "all funds" view determines if an offer becomes unfunded or is
        // found unfunded
        // These are the account balances before the strand executes
        std::optional<PaymentSandbox> afView(&baseView);
        EitherAmount limitStepOut;
        {
            EitherAmount stepOut(out);
            for (auto i = s; i--;)
            {
                auto r = strand[i]->rev(*sb, *afView, ofrsToRm, stepOut);
                if (strand[i]->isZero(r.second))
                {
                    JLOG(j.trace()) << "Strand found dry in rev";
                    return Result{strand, std::move(ofrsToRm)};
                }

                if (i == 0 && maxIn && *maxIn < get<TInAmt>(r.first))
                {
                    // limiting - exceeded maxIn
                    // Throw out previous results
                    sb.emplace(&baseView);
                    limitingStep = i;

                    // re-execute the limiting step
                    r = strand[i]->fwd(
                        *sb, *afView, ofrsToRm, EitherAmount(*maxIn));
                    limitStepOut = r.second;

                    if (strand[i]->isZero(r.second))
                    {
                        JLOG(j.trace()) << "First step found dry";
                        return Result{strand, std::move(ofrsToRm)};
                    }
                    if (get<TInAmt>(r.first) != *maxIn)
                    {
                        // Something is very wrong
                        // throwing out the sandbox can only increase liquidity
                        // yet the limiting is still limiting
                        JLOG(j.fatal())
                            << "Re-executed limiting step failed. r.first: "
                            << to_string(get<TInAmt>(r.first))
                            << " maxIn: " << to_string(*maxIn);
                        assert(0);
                        return Result{strand, std::move(ofrsToRm)};
                    }
                }
                else if (!strand[i]->equalOut(r.second, stepOut))
                {
                    // limiting
                    // Throw out previous results
                    sb.emplace(&baseView);
                    afView.emplace(&baseView);
                    limitingStep = i;

                    // re-execute the limiting step
                    stepOut = r.second;
                    r = strand[i]->rev(*sb, *afView, ofrsToRm, stepOut);
                    limitStepOut = r.second;

                    if (strand[i]->isZero(r.second))
                    {
                        // A tiny input amount can cause this step to output
                        // zero. I.e. 10^-80 IOU into an IOU -> XRP offer.
                        JLOG(j.trace()) << "Limiting step found dry";
                        return Result{strand, std::move(ofrsToRm)};
                    }
                    if (!strand[i]->equalOut(r.second, stepOut))
                    {
                        // Something is very wrong
                        // throwing out the sandbox can only increase liquidity
                        // yet the limiting is still limiting
#ifndef NDEBUG
                        JLOG(j.fatal())
                            << "Re-executed limiting step failed. r.second: "
                            << r.second << " stepOut: " << stepOut;
#else
                        JLOG(j.fatal()) << "Re-executed limiting step failed";
#endif
                        assert(0);
                        return Result{strand, std::move(ofrsToRm)};
                    }
                }

                // prev node needs to produce what this node wants to consume
                stepOut = r.first;
            }
        }

        {
            EitherAmount stepIn(limitStepOut);
            for (auto i = limitingStep + 1; i < s; ++i)
            {
                auto const r = strand[i]->fwd(*sb, *afView, ofrsToRm, stepIn);
                if (strand[i]->isZero(r.second))
                {
                    // A tiny input amount can cause this step to output zero.
                    // I.e. 10^-80 IOU into an IOU -> XRP offer.
                    JLOG(j.trace()) << "Non-limiting step found dry";
                    return Result{strand, std::move(ofrsToRm)};
                }
                if (!strand[i]->equalIn(r.first, stepIn))
                {
                    // The limits should already have been found, so executing a
                    // strand forward from the limiting step should not find a
                    // new limit
#ifndef NDEBUG
                    JLOG(j.fatal())
                        << "Re-executed forward pass failed. r.first: "
                        << r.first << " stepIn: " << stepIn;
#else
                    JLOG(j.fatal()) << "Re-executed forward pass failed";
#endif
                    assert(0);
                    return Result{strand, std::move(ofrsToRm)};
                }
                stepIn = r.second;
            }
        }

        auto const strandIn = *strand.front()->cachedIn();
        auto const strandOut = *strand.back()->cachedOut();

#ifndef NDEBUG
        {
            // Check that the strand will execute as intended
            // Re-executing the strand will change the cached values
            PaymentSandbox checkSB(&baseView);
            PaymentSandbox checkAfView(&baseView);
            EitherAmount stepIn(*strand[0]->cachedIn());
            for (auto i = 0; i < s; ++i)
            {
                bool valid;
                std::tie(valid, stepIn) =
                    strand[i]->validFwd(checkSB, checkAfView, stepIn);
                if (!valid)
                {
                    JLOG(j.warn())
                        << "Strand re-execute check failed. Step: " << i;
                    break;
                }
            }
        }
#endif

        bool const inactive = std::any_of(
            strand.begin(),
            strand.end(),
            [](std::unique_ptr<Step> const& step) { return step->inactive(); });

        return Result(
            strand,
            get<TInAmt>(strandIn),
            get<TOutAmt>(strandOut),
            std::move(*sb),
            std::move(ofrsToRm),
            inactive);
    }
    catch (FlowException const&)
    {
        return Result{strand, std::move(ofrsToRm)};
    }
}

/// @cond INTERNAL
template <class TInAmt, class TOutAmt>
struct FlowResult
{
    TInAmt in = beast::zero;
    TOutAmt out = beast::zero;
    std::optional<PaymentSandbox> sandbox;
    boost::container::flat_set<uint256> removableOffers;
    TER ter = temUNKNOWN;

    FlowResult() = default;

    FlowResult(
        TInAmt const& in_,
        TOutAmt const& out_,
        PaymentSandbox&& sandbox_,
        boost::container::flat_set<uint256> ofrsToRm)
        : in(in_)
        , out(out_)
        , sandbox(std::move(sandbox_))
        , removableOffers(std::move(ofrsToRm))
        , ter(tesSUCCESS)
    {
    }

    FlowResult(TER ter_, boost::container::flat_set<uint256> ofrsToRm)
        : removableOffers(std::move(ofrsToRm)), ter(ter_)
    {
    }

    FlowResult(
        TER ter_,
        TInAmt const& in_,
        TOutAmt const& out_,
        boost::container::flat_set<uint256> ofrsToRm)
        : in(in_), out(out_), removableOffers(std::move(ofrsToRm)), ter(ter_)
    {
    }
};
/// @endcond

/// @cond INTERNAL
inline std::optional<Quality>
qualityUpperBound(ReadView const& v, Strand const& strand)
{
    Quality q{STAmount::uRateOne};
    std::optional<Quality> stepQ;
    DebtDirection dir = DebtDirection::issues;
    for (auto const& step : strand)
    {
        if (std::tie(stepQ, dir) = step->qualityUpperBound(v, dir); stepQ)
            q = composed_quality(q, *stepQ);
        else
            return std::nullopt;
    }
    return q;
};
/// @endcond

inline std::optional<AvgQFunction>
avgQFunction(ReadView const& v, Strand const& strand)
{
    std::optional<AvgQFunction> stepQF;
    AvgQFunction qf;
    DebtDirection dir = DebtDirection::issues;
    for (auto const& step : strand)
    {
        if (std::tie(stepQF, dir) = step->getQF(v, dir); stepQF)
            qf.combineWithNext(*stepQF);
        else
            return std::nullopt;
    }
    return qf;
}

inline std::optional<InstQFunction>
instQFunction(ReadView const& v, Strand const& strand)
{
    if (auto const qf = avgQFunction(v, strand); !qf)
        return std::nullopt;
    else
        return InstQFunction(*qf);
}

/// @cond INTERNAL
/** Limit remaining out only if one strand and limitQuality is included.
 * Targets one path payment with AMM where the average quality is linear
 * and instant quality is quadratic function of output. Calculating quality
 * function for the whole strand enables figuring out required output
 * to produce requested strand's limitQuality. Reducing the output,
 * increases quality of AMM steps, increasing the strand's composite
 * quality as the result.
 */
template <typename TOutAmt>
inline TOutAmt
limitOut(
    ReadView const& v,
    Strand const& strand,
    TOutAmt const& remainingOut,
    Quality const& limitQuality)
{
    auto const qf = avgQFunction(v, strand);
    if (!qf)
        return remainingOut;

    // AvgQFunction is constant
    if (qf->isConstQ())
        return remainingOut;

    auto const out = [&]() {
        if (auto const out = qf->outFromQ(limitQuality); out == 0)
            return remainingOut;
        else if constexpr (std::is_same_v<TOutAmt, XRPAmount>)
            return (XRPAmount)out;
        else if constexpr (std::is_same_v<TOutAmt, IOUAmount>)
            return IOUAmount{out};
        else
            return STAmount{
                remainingOut.issue(), out.mantissa(), out.exponent()};
    }();
    return std::min(out, remainingOut);
};
/// @endcond

/// @cond INTERNAL
/* Track the non-dry strands

   flow will search the non-dry strands (stored in `cur_`) for the best
   available liquidity If flow doesn't use all the liquidity of a strand, that
   strand is added to `next_`. The strands in `next_` are searched after the
   current best liquidity is used.
 */
template <typename TIn, typename TOut>
class ActiveStrands
{
    using StrandsItem = std::pair<InstQFunction, Strand const*>;
    using StrandsInstQ = std::vector<StrandsItem>;
    using StrandsIter = StrandsInstQ::const_iterator;

private:
    // Strands to be explored for liquidity
    std::vector<Strand const*> cur_;
    // Strands that may be explored for liquidity on the next iteration
    std::vector<Strand const*> next_;
    // Initial remaining out amount
    TOut remainingOut_;
    // Initial remaining in amount
    std::optional<TIn> remainingIn_;

public:
    ActiveStrands(
        std::vector<Strand> const& strands,
        std::optional<TIn> const& remainingIn,
        TOut const& remainingOut)
    {
        cur_.reserve(strands.size());
        next_.reserve(strands.size());
        for (auto& strand : strands)
            next_.push_back(&strand);
        remainingOut_ = remainingOut;
        remainingIn_ = remainingIn;
    }

    // Start a new iteration in the search for liquidity
    // Set the current strands to the strands in `next_`.
    // Return max output amount that this strand can
    // generate.
    TOut
    activateNext(
        ReadView const& v,
        std::optional<Quality> const& limitQuality,
        std::optional<TIn> const& remainingIn,
        TOut const& remainingOut)
    {
        TOut output = remainingOut;
        // add the strands in `next_` to `cur_`, sorted by theoretical quality.
        // Best quality first.
        cur_.clear();
        if (v.rules().enabled(featureFlowSortStrands) && !next_.empty())
        {
            StrandsInstQ strandQuals;
            strandQuals.reserve(next_.size());
            if (next_.size() > 1 || (next_.size() == 1 && limitQuality))
            {
                for (Strand const* strand : next_)
                {
                    if (!strand)
                    {
                        // should not happen
                        continue;
                    }
                    if (auto const qual = instQFunction(v, *strand))
                    {
                        if (limitQuality && qual->spotQuality() < *limitQuality)
                        {
                            // If a strand's quality is ever over limitQuality
                            // it is no longer part of the candidate set. Note
                            // that when transfer fees are charged, and an
                            // account goes from redeeming to issuing then
                            // strand quality _can_ increase; However, this is
                            // an unusual corner case.
                            continue;
                        }
                        strandQuals.push_back({*qual, strand});
                    }
                }
                // must stable sort for deterministic order across different c++
                // standard library implementations
                std::stable_sort(
                    strandQuals.begin(),
                    strandQuals.end(),
                    [](auto const& lhs, auto const& rhs) {
                        auto const& liq = std::get<InstQFunction>(lhs);
                        auto const& riq = std::get<InstQFunction>(rhs);
                        auto const lq = liq.spotQuality();
                        auto const rq = riq.spotQuality();
                        // higher qualities first
                        // const quality first if equal qualities
                        // and one is AMM
                        // or higher slope if equal and both AMM
                        return lq > rq ||
                            (lq == rq &&
                             ((liq.isConstQ() && !riq.isConstQ()) ||
                              (liq.slope() > riq.slope())));
                    });
                next_.clear();
                next_.reserve(strandQuals.size());
                for (auto const& sq : strandQuals)
                {
#if 0
                    auto const strand = get<Strand const*>(sq);
                    std::cout
                        << (Number(1) /
                            std::get<InstQFunction>(sq).spotQuality().rate())
                        << " ";
                    for (auto const& step : *strand)
                    {
                        if (step->bookStepBook())
                            std::cout
                                << to_string(step->bookStepBook()->in.currency)
                                << "/"
                                << to_string(step->bookStepBook()->out.currency)
                                << " ";
                    }
#endif

                    next_.push_back(std::get<Strand const*>(sq));
                }
                //                std::cout << std::endl;
            }
            if (strandQuals.size() > 0)
                output = limitOutput(
                    strandQuals, limitQuality, remainingIn, remainingOut);
        }
        std::swap(cur_, next_);

        return output;
    }

    Strand const*
    get(size_t i) const
    {
        if (i >= cur_.size())
        {
            assert(0);
            return nullptr;
        }
        return cur_[i];
    }

    void
    push(Strand const* s)
    {
        next_.push_back(s);
    }

    // Push the strands from index i to the end of cur_ to next_
    void
    pushRemainingCurToNext(size_t i)
    {
        if (i >= cur_.size())
            return;
        next_.insert(next_.end(), std::next(cur_.begin(), i), cur_.end());
    }

    auto
    size() const
    {
        return cur_.size();
    }

    void
    removeIndex(std::size_t i)
    {
        if (i >= next_.size())
            return;
        next_.erase(next_.begin() + i);
    }

private:
    template <typename T>
    using ReqFromStrands = T (*)(
        StrandsIter const&,
        StrandsIter const&,
        Quality const&,
        Issue const&);
    using InstQStrandPair = std::pair<InstQFunction, Strand const*>;
    using InstQGetter =
        std::function<InstQFunction const&(InstQStrandPair const&)>;
    using SplitBetweenStrands = Quality (*)(
        StrandsIter const&,
        StrandsIter const&,
        Number const&,
        InstQGetter const&);

    /** Return relative diff. actual must be <= remaining.
     */
    static Number
    relDiff(Number const& actual, Number const& remaining)
    {
        if (remaining == 0 || actual > remaining)
            Throw<std::runtime_error>("relDiff 0 remaining");
        return (remaining - actual) / remaining;
    }

    /** Given the remaining amount and the collection of quality functions
     * compute the quality such that if that quality were used, the sum
     * of all the amounts from all the quality functions (while respecting the
     * quality limits) is as close to the remaining amount as possible, without
     * going over that amount. Where the remaining amount is either
     * remainingOut or remainingIn. Return the quality.
     */
    template <typename T>
    std::pair<Quality, T>
    reqFromActiveStrands(
        StrandsInstQ const& sortedStrands,
        StrandsIter& it,
        T const& remaining,
        T const& initialRemaining,
        ReqFromStrands<T> reqFromStrands,
        SplitBetweenStrands splitBetweenStrands)
    {
        auto const pct99_9 = Number(999, -3);
        auto const cbegin = sortedStrands.cbegin();
        auto const cend = sortedStrands.cend();
        auto endQ = std::get<InstQFunction>(*cbegin).qLimit();
        auto const remIssue = getIssue(remaining);

        auto actual = [&]() {
            auto actual = remaining;
            it = cbegin + 1;

            // If a small amount is left then use one strand
            // for the entire remaining amount.
            if (relDiff(remaining, initialRemaining) > pct99_9)
                return remaining;

            for (; it != cend; ++it)
            {
                // Get all strands at the same quality as the best quality
                // and update the endQ
                if (it->first.spotQuality() == cbegin->first.spotQuality())
                {
                    endQ = [&]() -> std::optional<Quality> {
                        if (it->first.qLimit() && endQ)
                            return std::max(*it->first.qLimit(), *endQ);
                        else if (endQ)
                            return endQ;
                        return it->first.qLimit();
                    }();
                    continue;
                }
                // Stop at the first const quality or the endq.
                // Once the spot price quality (SPQ) of the const quality
                // strand is reached then this strand has the best quality.
                // Endq (or quality limit) is somewhat similar. A non-const
                // quality strand with a quality limit may change to a const
                // quality strand once AMM offer is consumed.
                else if (
                    it->first.isConstQ() || it->first.spotQuality() <= endQ)
                {
                    endQ = endQ ? std::min(it->first.spotQuality(), *endQ)
                                : it->first.spotQuality();
                    // std::cout << " reqQ " << (Number(1) / endQ->rate()) << "
                    // ";
                    actual = reqFromStrands(cbegin, it, *endQ, remIssue);
                    break;
                }
                endQ = [&]() -> std::optional<Quality> {
                    if (it->first.qLimit() && endQ)
                        return std::min(*it->first.qLimit(), *endQ);
                    else if (endQ)
                        return endQ;
                    return it->first.qLimit();
                }();
                // std::cout << " reqQ "
                //           << (Number(1) / it->first.spotQuality().rate())
                //           << " ";
                actual = reqFromStrands(
                    cbegin, it, it->first.spotQuality(), remIssue);
                if (actual >= remaining)
                    break;
            }
            return actual;
        }();

        // Got all strands or more actual than requested remaining -
        // split entire remaining between the strands.
        if (it == cend || actual > remaining)
            actual = remaining;

        InstQGetter getter(
            [](InstQStrandPair const& Q) -> InstQFunction const& {
                return Q.first;
            });
        auto const q = splitBetweenStrands(cbegin, it, actual, getter);
        endQ = endQ ? std::min(q, *endQ) : q;

        // std::cout << " actual " << to_string(actual) << " #strands "
        //           << (it - cbegin);

        // endQ is seated
        return {*endQ, actual};
    }

    /** Get output from the strands at the given quality.
     */
    static TOut
    outFromStrands(
        StrandsIter const& cbeginIt,
        StrandsIter const& cendIt,
        Quality const& q,
        Issue const& issueOut)
    {
        Number output{0};
        for (auto it = cbeginIt; it != cendIt; ++it)
            output += std::get<InstQFunction>(*it).outFromQ(q);
        return toAmount<TOut>(issueOut, output);
    }

    /** Get input from the strands at the given quality.
     */
    static TIn
    inFromStrands(
        StrandsIter const& cbeginIt,
        StrandsIter const& cendIt,
        Quality const& q,
        Issue const& issueIn)
    {
        Number in{0};
        for (auto it = cbeginIt; it != cendIt; ++it)
            in += std::get<InstQFunction>(*it).inFromQ(q);
        return toAmount<TIn>(issueIn, in);
    }

    /** When calculating the quality to generate required output or input
     * amount from active independent strands, it is expected that when each
     * strand's liquidity is consumed, all active strands end up at the same
     * instant quality. However, due to the round-off, those qualities
     * may differ by a tiny amount resulting in multiple payment engine
     * iterations for the actual to reach the remaining amount. To address this
     * scenario, ignore the actual if it differs from the remaining by
     * a tiny amount.
     */
    template <typename T>
    static T
    roundOutput(T const& actual, T const& remaining)
    {
        Number const pct0_001(1, -5);
        if (relDiff(actual, remaining) < pct0_001)
            return remaining;
        return actual;
    }

    /** Find the output limited by the input. This function is called
     * if SendMax is included in the payment. SendMax may limit the output
     * generated by the strand. SendMax itself can be limited by other
     * factors such as the number of strands factored in the calculation or
     * the strand's quality range limit.
     */
    std::optional<TOut>
    limitOutputByInput(
        StrandsInstQ const& sortedStrands,
        TIn const& remainingIn,
        TOut const& remainingOut)
    {
        auto const issueOut = getIssue(remainingOut);
        StrandsInstQ::const_iterator it;

        // std::cout << "limitOutputByInput: remIn: " << to_string(remainingIn);

        auto const [endQ, actual] = reqFromActiveStrands(
            sortedStrands,
            it,
            remainingIn,
            *remainingIn_,
            &ActiveStrands::inFromStrands,
            &InstQFunction::splitInReqBetweenStrands);

        // Entire remainingIn is used by one strand. We could figure
        // the output corresponding to this remainingIn. But due to
        // limited precision, the calculated output might be slightly
        // less than required. This may result in extra payment
        // engine iterations until the output converges to the
        // requested amount. To avoid this, return the entire
        // remainingOut. This way if there is a limit on
        // remainingIn, it'll get adjusted in the forward iteration.
        if (actual == remainingIn && (it - sortedStrands.cbegin()) == 1)
            return remainingOut;

        // If remainingIn limits the output then find the output generated
        // by the active strands from the quality corresponding
        // to the remainingIn split between the active strands.
        if (auto const output =
                outFromStrands(sortedStrands.cbegin(), it, endQ, issueOut);
            output <= beast::zero)
        {
            // std::cout << " out " << to_string(output) << " remOut "
            //           << to_string(remainingOut) << std::endl;
            return toAmount<TOut>(issueOut, Number{0});
        }
        else if (output <= remainingOut)
        {
            // std::cout << " out " << to_string(output) << " remOut "
            //           << to_string(remainingOut) << std::endl;
            return toAmount<TOut>(
                issueOut,
                std::get<InstQFunction>(sortedStrands[0]).outFromQ(endQ));
        }
        // std::cout << std::endl;
        return std::nullopt;
    }

    /** Find the max output that the best quality strand may generate.
     * The output may be limited by remainingIn or other factors (see above).
     */
    TOut
    limitOutput(
        StrandsInstQ const& sortedStrands,
        std::optional<Quality> const& limitQuality,
        std::optional<TIn> const& remainingIn,
        TOut const& remainingOut)
    {
        auto& bestQ = std::get<InstQFunction>(sortedStrands[0]);
        // Best Q Strand has const quality. Don't have to set
        // the output limit.
        if (bestQ.isConstQ())
            return remainingOut;

        TOut output{remainingOut};
        auto const issueOut = getIssue(remainingOut);
        if (sortedStrands.size() > 1)
        {
            if (remainingIn)
            {
                if (auto const output = limitOutputByInput(
                        sortedStrands, *remainingIn, remainingOut))
                    return roundOutput(*output, remainingOut);
            }
            StrandsIter it;

            // std::cout << "limitOutput: remOut " << to_string(remainingOut);
            auto const [endQ, actual] = reqFromActiveStrands(
                sortedStrands,
                it,
                remainingOut,
                remainingOut_,
                &ActiveStrands::outFromStrands,
                &InstQFunction::splitOutReqBetweenStrands);
            // std::cout << std::endl;

            // std::cout << "limitOutput: remOut " << to_string(remainingOut)
            //           << " actual " << to_string(actual) << std::endl;

            if (actual <= remainingOut && (it - sortedStrands.cbegin()) > 1)
                output = toAmount<TOut>(issueOut, bestQ.outFromQ(endQ));
        }

        if (limitQuality)
            output = std::min(
                output,
                toAmount<TOut>(issueOut, bestQ.outFromAvgQ(*limitQuality)));

        if (output <= beast::zero)
            return toAmount<TOut>(issueOut, Number{0});
        if (output < remainingOut)
            return roundOutput(output, remainingOut);
        return remainingOut;
    }
};
/// @endcond

/**
   Request `out` amount from a collection of strands

   Attempt to fulfill the payment by using liquidity from the strands in order
   from least expensive to most expensive

   @param baseView Trust lines and balances
   @param strands Each strand contains the steps of accounts to ripple through
                  and offer books to use
   @param outReq Amount of output requested from the strand
   @param partialPayment If true allow less than the full payment
   @param offerCrossing If true offer crossing, not handling a standard payment
   @param limitQuality If present, the minimum quality for any strand taken
   @param sendMaxST If present, the maximum STAmount to send
   @param j Journal to write journal messages to
   @param ammContext counts iterations with AMM offers
   @param flowDebugInfo If pointer is non-null, write flow debug info here
   @return Actual amount in and out from the strands, errors, and payment
   sandbox
*/
template <class TInAmt, class TOutAmt>
FlowResult<TInAmt, TOutAmt>
flow(
    PaymentSandbox const& baseView,
    std::vector<Strand> const& strands,
    TOutAmt const& outReq,
    bool partialPayment,
    bool offerCrossing,
    std::optional<Quality> const& limitQuality,
    std::optional<STAmount> const& sendMaxST,
    beast::Journal j,
    AMMContext& ammContext,
    path::detail::FlowDebugInfo* flowDebugInfo = nullptr)
{
    // Used to track the strand that offers the best quality (output/input
    // ratio)
    struct BestStrand
    {
        TInAmt in;
        TOutAmt out;
        PaymentSandbox sb;
        Strand const& strand;
        Quality quality;

        BestStrand(
            TInAmt const& in_,
            TOutAmt const& out_,
            PaymentSandbox&& sb_,
            Strand const& strand_,
            Quality const& quality_)
            : in(in_)
            , out(out_)
            , sb(std::move(sb_))
            , strand(strand_)
            , quality(quality_)
        {
        }
    };

    std::size_t const maxTries = 1000;
    std::size_t curTry = 0;
    std::uint32_t maxOffersToConsider = 1500;
    std::uint32_t offersConsidered = 0;

    // There is a bug in gcc that incorrectly warns about using uninitialized
    // values if `remainingIn` is initialized through a copy constructor. We can
    // get similar warnings for `sendMax` if it is initialized in the most
    // natural way. Using `make_optional`, allows us to work around this bug.
    TInAmt const sendMaxInit =
        sendMaxST ? toAmount<TInAmt>(*sendMaxST) : TInAmt{beast::zero};
    std::optional<TInAmt> const sendMax =
        (sendMaxST && sendMaxInit >= beast::zero)
        ? std::make_optional(sendMaxInit)
        : std::nullopt;
    std::optional<TInAmt> remainingIn =
        !!sendMax ? std::make_optional(sendMaxInit) : std::nullopt;
    // std::optional<TInAmt> remainingIn{sendMax};

    TOutAmt remainingOut(outReq);

    PaymentSandbox sb(&baseView);

    // non-dry strands
    ActiveStrands<TInAmt, TOutAmt> activeStrands(
        strands, remainingIn, remainingOut);

    // Keeping a running sum of the amount in the order they are processed
    // will not give the best precision. Keep a collection so they may be summed
    // from smallest to largest
    boost::container::flat_multiset<TInAmt> savedIns;
    savedIns.reserve(maxTries);
    boost::container::flat_multiset<TOutAmt> savedOuts;
    savedOuts.reserve(maxTries);

    auto sum = [](auto const& col) {
        using TResult = std::decay_t<decltype(*col.begin())>;
        if (col.empty())
            return TResult{beast::zero};
        return std::accumulate(col.begin() + 1, col.end(), *col.begin());
    };

    // These offers only need to be removed if the payment is not
    // successful
    boost::container::flat_set<uint256> ofrsToRmOnFail;
#if 0
    std::unordered_map<std::string, Number> in;
    std::unordered_map<std::string, Number> out;
    Number totIn{0};
    Number totOut{0};
    auto key = [](Strand const& strand) {
        std::stringstream str;
        for (auto const& step : strand)
        {
            if (step->bookStepBook())
                str << to_string(step->bookStepBook()->in.currency) << "/"
                    << to_string(step->bookStepBook()->out.currency) << " ";
        }
        return str.str();
    };
#endif
    while (remainingOut > beast::zero &&
           (!remainingIn || *remainingIn > beast::zero))
    {
        ++curTry;
        if (curTry >= maxTries)
        {
            return {telFAILED_PROCESSING, std::move(ofrsToRmOnFail)};
        }

        auto const limitRemainingOut = activeStrands.activateNext(
            sb, limitQuality, remainingIn, remainingOut);

        boost::container::flat_set<uint256> ofrsToRm;
        std::optional<BestStrand> best;
        if (flowDebugInfo)
            flowDebugInfo->newLiquidityPass();
        // Index of strand to mark as inactive (remove from the active list) if
        // the liquidity is used. This is used for strands that consume too many
        // offers Constructed as `false,0` to workaround a gcc warning about
        // uninitialized variables
        std::optional<std::size_t> markInactiveOnUse;
        for (size_t strandIndex = 0, sie = activeStrands.size();
             strandIndex != sie;
             ++strandIndex)
        {
            Strand const* strand = activeStrands.get(strandIndex);
            if (!strand)
            {
                // should not happen
                continue;
            }
            // Clear AMM liquidity used flag. The flag might still be set if
            // the previous strand execution failed. It has to be reset
            // since this strand might not have AMM liquidity.
            ammContext.clear();
            if (offerCrossing && limitQuality)
            {
                auto const strandQ = qualityUpperBound(sb, *strand);
                if (!strandQ || *strandQ < *limitQuality)
                    continue;
            }
            auto f = flow<TInAmt, TOutAmt>(
                sb, *strand, remainingIn, limitRemainingOut, j);

            // rm bad offers even if the strand fails
            SetUnion(ofrsToRm, f.ofrsToRm);

            offersConsidered += f.ofrsUsed;

            if (!f.success || f.out == beast::zero)
                continue;

            if (flowDebugInfo)
                flowDebugInfo->pushLiquiditySrc(
                    EitherAmount(f.in), EitherAmount(f.out));

            assert(
                f.out <= remainingOut && f.sandbox &&
                (!remainingIn || f.in <= *remainingIn));

            Quality const q(f.out, f.in);

            JLOG(j.trace())
                << "New flow iter (iter, in, out): " << curTry - 1 << " "
                << to_string(f.in) << " " << to_string(f.out);
#if 0
            std::cout << "New flow iter (iter, in, out): " << curTry - 1 << " "
                      << to_string(f.in) << " " << to_string(f.out)
                      << std::endl;
#endif

            if (limitQuality && q < *limitQuality)
            {
                JLOG(j.trace())
                    << "Path rejected by limitQuality"
                    << " limit: " << *limitQuality << " path q: " << q;
                continue;
            }

            if (baseView.rules().enabled(featureFlowSortStrands))
            {
                assert(!best);
                if (!f.inactive)
                    activeStrands.push(strand);
                best.emplace(f.in, f.out, std::move(*f.sandbox), *strand, q);
                activeStrands.pushRemainingCurToNext(strandIndex + 1);
                break;
            }

            activeStrands.push(strand);

            if (!best || best->quality < q ||
                (best->quality == q && best->out < f.out))
            {
                // If this strand is inactive (because it consumed too many
                // offers) and ends up having the best quality, remove it
                // from the activeStrands. If it doesn't end up having the
                // best quality, keep it active.

                if (f.inactive)
                {
                    // This should be `nextSize`, not `size`. This issue is
                    // fixed in featureFlowSortStrands.
                    markInactiveOnUse = activeStrands.size() - 1;
                }
                else
                {
                    markInactiveOnUse.reset();
                }

                best.emplace(f.in, f.out, std::move(*f.sandbox), *strand, q);
            }
        }

        bool const shouldBreak = [&] {
            if (baseView.rules().enabled(featureFlowSortStrands))
                return !best || offersConsidered >= maxOffersToConsider;
            return !best;
        }();

        if (best)
        {
            if (markInactiveOnUse)
            {
                activeStrands.removeIndex(*markInactiveOnUse);
                markInactiveOnUse.reset();
            }
            savedIns.insert(best->in);
            savedOuts.insert(best->out);
            remainingOut = outReq - sum(savedOuts);
            if (sendMax)
                remainingIn = *sendMax - sum(savedIns);

            if (flowDebugInfo)
                flowDebugInfo->pushPass(
                    EitherAmount(best->in),
                    EitherAmount(best->out),
                    activeStrands.size());

            JLOG(j.trace()) << "Best path: in: " << to_string(best->in)
                            << " out: " << to_string(best->out)
                            << " remainingOut: " << to_string(remainingOut);
#if 0
            std::cout << "Best path: " << key(best->strand)
                      << " in: " << to_string(best->in)
                      << " out: " << to_string(best->out)
                      << " remainingOut: " << to_string(remainingOut)
                      << " remainingIn: "
                      << (remainingIn ? to_string(*remainingIn) : "")
                      << std::endl;
            in[key(best->strand)] += best->in;
            out[key(best->strand)] += best->out;
            totIn += best->in;
            totOut += best->out;
#endif

            best->sb.apply(sb);
            ammContext.update();
        }
        else
        {
            JLOG(j.trace()) << "All strands dry.";
        }

        best.reset();  // view in best must be destroyed before modifying base
                       // view
        if (!ofrsToRm.empty())
        {
            SetUnion(ofrsToRmOnFail, ofrsToRm);
            for (auto const& o : ofrsToRm)
            {
                if (auto ok = sb.peek(keylet::offer(o)))
                    offerDelete(sb, ok, j);
            }
        }

        if (shouldBreak)
            break;
    }
#if 0
    for (auto const& [key, val] : in)
        std::cout << key << " " << val << " " << out[key] << std::endl;
    std::cout << "total: " << totIn << " " << totOut << " q "
              << (totOut / totIn) << std::endl;
#endif

    auto const actualOut = sum(savedOuts);
    auto const actualIn = sum(savedIns);

    JLOG(j.trace()) << "Total flow: in: " << to_string(actualIn)
                    << " out: " << to_string(actualOut);
#if 0
    std::cout << "Total flow: in: " << to_string(actualIn)
              << " out: " << to_string(actualOut) << std::endl;
#endif

    if (actualOut != outReq)
    {
        if (actualOut > outReq)
        {
            assert(0);
            return {tefEXCEPTION, std::move(ofrsToRmOnFail)};
        }
        if (!partialPayment)
        {
            // If we're offerCrossing a !partialPayment, then we're
            // handling tfFillOrKill.  That case is handled below; not here.
            if (!offerCrossing)
                return {
                    tecPATH_PARTIAL,
                    actualIn,
                    actualOut,
                    std::move(ofrsToRmOnFail)};
        }
        else if (actualOut == beast::zero)
        {
            return {tecPATH_DRY, std::move(ofrsToRmOnFail)};
        }
    }
    if (offerCrossing && !partialPayment)
    {
        // If we're offer crossing and partialPayment is *not* true, then
        // we're handling a FillOrKill offer.  In this case remainingIn must
        // be zero (all funds must be consumed) or else we kill the offer.
        assert(remainingIn);
        if (remainingIn && *remainingIn != beast::zero)
            return {
                tecPATH_PARTIAL,
                actualIn,
                actualOut,
                std::move(ofrsToRmOnFail)};
    }

    return {actualIn, actualOut, std::move(sb), std::move(ofrsToRmOnFail)};
}

}  // namespace ripple

#endif
