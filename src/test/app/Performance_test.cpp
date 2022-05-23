//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2022 Ripple Labs Inc.

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
#include "test/jtx.h"

namespace ripple {
std::uint32_t numPEIters = 10;
namespace test {

class Performance_test : public beast::unit_test::suite
{
    void
    testPerformance()
    {
        testcase("performance");
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        std::array<std::uint64_t, 100> t1;
        std::array<std::uint64_t, 100> t2;
        std::array<std::uint64_t, 100> t3;
        std::array<std::uint64_t, 100> t4;
        for (int i = 0; i < 100; i++)
        {
            {
                numPEIters = 1;
                Env env{*this};
                env.fund(XRP(100000), gw, alice, carol);
                env.trust(USD(1000), alice, carol);
                env(pay(gw, alice, USD(1000)));
                auto const start = system_clock::now().time_since_epoch();
                env(pay(alice, carol, XRP(100)));
                auto const d =
                    duration_cast<microseconds>(
                        system_clock::now().time_since_epoch() - start)
                        .count();
                t1[i] = d;
            }
            {
                numPEIters = 1;
                Env env{*this};
                env.fund(XRP(100000), gw, alice, carol);
                env.trust(USD(1000), alice, carol);
                env(pay(gw, alice, USD(1000)));
                auto const start = system_clock::now().time_since_epoch();
                env(pay(alice, carol, USD(100)));
                auto const d =
                    duration_cast<microseconds>(
                        system_clock::now().time_since_epoch() - start)
                        .count();
                t2[i] = d;
            }
            {
                numPEIters = 10;
                Env env{*this};
                env.fund(XRP(100000), gw, alice, carol);
                env.trust(USD(1000), alice, carol);
                env(pay(gw, alice, USD(1000)));
                auto const start = system_clock::now().time_since_epoch();
                env(pay(alice, carol, XRP(100)));
                auto const d =
                    duration_cast<microseconds>(
                        system_clock::now().time_since_epoch() - start)
                        .count();
                t3[i] = d;
            }
            {
                numPEIters = 10;
                Env env{*this};
                env.fund(XRP(100000), gw, alice, carol);
                env.trust(USD(1000), alice, carol);
                env(pay(gw, alice, USD(1000)));
                auto const start = system_clock::now().time_since_epoch();
                env(pay(alice, carol, USD(100)));
                auto const d =
                    duration_cast<microseconds>(
                        system_clock::now().time_since_epoch() - start)
                        .count();
                t4[i] = d;
            }
        }
        auto stats = [](std::array<std::uint64_t, 100> const& t,
                        std::string const& msg) {
            auto const sum = std::accumulate(t.begin(), t.end(), 0.0);
            auto const avg = sum / static_cast<double>(t.size());
            auto sd = std::accumulate(
                t.begin(), t.end(), 0.0, [&](auto const init, auto const r) {
                    return init + pow((r - avg), 2);
                });
            sd = sqrt(sd / t.size());
            std::cout << msg << " exec time: avg " << avg << " "
                      << " sd " << sd << std::endl;
        };
        std::cout << "1 PE iteration\n";
        stats(t1, "XRP");
        stats(t2, "USD");
        std::cout << "10 PE iteration\n";
        stats(t3, "XRP");
        stats(t4, "USD");
        BEAST_EXPECT(true);
    }

    void
    run() override
    {
        testPerformance();
    }
};

BEAST_DEFINE_TESTSUITE(Performance, app, ripple);

}  // namespace test
}  // namespace ripple
