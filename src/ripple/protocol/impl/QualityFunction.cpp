//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/protocol/Quality.h>
#include <ripple/protocol/QualityFunction.h>

namespace ripple {

AvgQFunction::AvgQFunction() : m_(0), b_(0), qLimit_(std::nullopt)
{
}

AvgQFunction::AvgQFunction(Quality const& quality, AvgQFunction::CLOBTag)
{
    if (quality.rate() <= beast::zero)
        Throw<std::runtime_error>("AvgQFunction quality rate is 0.");
    m_ = 0;
    b_ = 1 / quality.rate();
    qLimit_ = std::nullopt;
}

void
AvgQFunction::combineWithNext(AvgQFunction const& next)
{
    if (m_ == 0 && b_ == 0)
    {
        m_ = next.m_;
        b_ = next.b_;
    }
    else
    {
        m_ += b_ * next.m_;
        b_ *= next.b_;
        if (qLimit_ && next.qLimit_)
        {
            auto const maxCurOut = outFromQ(*qLimit_);
            // max output from the next step if it receives the max output
            // from the current step
            auto const maxNextOutFromCur = next.outFromIn(maxCurOut);
            // max output from the next step based on its quality limits
            auto const maxNextOutFromNext = next.outFromQ(*next.qLimit_);
            auto const maxNextOut =
                std::min(maxNextOutFromCur, maxNextOutFromNext);
            qLimit_ = qFromOut(maxNextOut);
        }
        else if (qLimit_)
        {
            auto const maxCurOut = outFromQ(*qLimit_);
            auto const maxNextOutFromCur = next.outFromIn(maxCurOut);
            qLimit_ = qFromOut(maxNextOutFromCur);
        }
        else if (next.qLimit_)
        {
            auto const maxNextOutFromNext = next.outFromQ(*next.qLimit_);
            qLimit_ = qFromOut(maxNextOutFromNext);
        }
        else
            qLimit_ = std::nullopt;
    }
}

Quality
AvgQFunction::qFromOut(Number const& output) const
{
    return toQuality(m_ * output + b_);
}

Number
AvgQFunction::outFromQ(
    Number const& m,
    Number const& b,
    Quality const& quality,
    std::optional<Quality> const& qLimit)
{
    auto const q = qLimit ? std::max(*qLimit, quality) : quality;

    if (auto const rate = q.rate(); rate == beast::zero || m == 0)
        return 0;
    else if (auto const out = (1 / rate - b) / m; out < 0)
        return 0;
    else
        return out;
}

Number
AvgQFunction::outFromQ(const Quality& quality) const
{
    return outFromQ(m_, b_, quality, qLimit_);
}

Number
AvgQFunction::outFromIn(Number const& input) const
{
    Number out;
    try
    {
        out = b_ * input / (1 - m_ * input);
    }
    catch (...)
    {
        Throw<std::runtime_error>("AvgQFunction::outFromIn error");
    }
    return out;
}

////////////////////////////////////////////////////////////////////////////
// Instant Quality
////////////////////////////////////////////////////////////////////////////

InstQFunction::InstQFunction(const AvgQFunction& qf)
    : m_(qf.m_), b_(qf.b_), avgQLimit_(qf.qLimit_)
{
    qLimit_ = std::nullopt;
    // convert avg to inst q limit
    if (qf.qLimit_)
    {
        auto const out = qf.outFromQ(*qf.qLimit_);
        qLimit_ = qFromOut(out);
    }
}

Quality
InstQFunction::spotQuality() const
{
    return toQuality(b_);
}

std::optional<Quality> const&
InstQFunction::qLimit() const
{
    return qLimit_;
}

Number
InstQFunction::outFromQ(Quality const& q) const
{
    if (m_ == 0)
        return INT64_MAX;
    auto const q1 = qLimit_ ? std::max(q, *qLimit_) : q;
    if (q1 == Quality{UINT64_MAX})
        return -b_ / m_;
    return -(b_ - root2(b_ / q1.rate())) / m_;
}

Number
InstQFunction::outFromAvgQ(Quality const& aq) const
{
    return AvgQFunction::outFromQ(m_, b_, aq, avgQLimit_);
}

Number
InstQFunction::inFromQ(Quality const& q) const
{
    auto const q1 = qLimit_ ? std::max(q, *qLimit_) : q;
    if (m_ == 0 || q1 == Quality{UINT64_MAX})
        return INT64_MAX;
    return (1 - root2(b_ * q1.rate())) / m_;
}

Quality
InstQFunction::qFromOut(Number const& output) const
{
    Quality q;
    try
    {
        q = toQuality(m_ * m_ * output * output / b_ + 2 * m_ * output + b_);
    }
    catch (...)
    {
        Throw<std::runtime_error>("InstQFunction::qFromOut error");
    }
    return q;
}

}  // namespace ripple
