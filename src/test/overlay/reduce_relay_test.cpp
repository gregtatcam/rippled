//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2020 Ripple Labs Inc.

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
#include <ripple/overlay/Slot.h>
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <boost/thread.hpp>

namespace ripple {

namespace test {

using namespace boost::asio;
using namespace std::chrono;

class PeerSim {
public:
    using id_t = std::uint32_t;
    PeerSim ()
    {
        id_ = sid_;
        sid_++;
    }

    ~PeerSim () = default;
    id_t id() { return id_; }

private:
    static id_t sid_;
    id_t id_;
    Squelch::Squelch squelch_;
};

id_t PeerSim::sid_ = 0;

using SquelchCB = std::function<void(PublicKey const&,
                                     std::weak_ptr<PeerSim>, std::uint32_t)>;
using UnsquelchCB = std::function<void(PublicKey const&,
                                       std::weak_ptr<PeerSim>)>;
using SendCB = std::function<void(PublicKey const&, std::weak_ptr<PeerSim>,
                                  std::uint32_t, id_t, std::uint32_t)>;

class OverlaySim {
public:
    OverlaySim () {}

    ~OverlaySim () = default;

    void
    checkForSquelch(PublicKey const& validator,
                    std::shared_ptr<PeerSim> peer,
                    SquelchCB f,
                    protocol::MessageType type = protocol::mtVALIDATION)
    {
        slots_.checkForSquelch(validator, peer->id(), peer, type, f);
    }

    void
    unsquelch(Peer::id_t const& id, UnsquelchCB f)
    {
        slots_.unsquelch(id, f);
    }

    void
    checkIdle(UnsquelchCB f)
    {
        slots_.checkIdle(f);
    }

    void
    addPeer() {
        auto peer = std::make_shared<PeerSim>();
        peers_.emplace(std::make_pair(peer->id(), std::move(peer)));
    }

    void
    send(PublicKey const& validator, id_t id, SquelchCB f,
         protocol::MessageType type = protocol::mtVALIDATION)
    {
        checkForSquelch(validator, peers_[id], f, type);
    }

    std::pair<bool, bool>
    isCountingState(PublicKey const& validator)
    {
        return slots_.isCountingState(validator);
    }

    boost::optional<std::uint16_t>
    inState(PublicKey const& validator, Squelch::PeerState state, bool notState = false)
    {
        return slots_.inState(validator, state, notState);
    }

private:
    std::unordered_map<PeerSim::id_t, std::shared_ptr<PeerSim>> peers_;
    Squelch::Slots<PeerSim> slots_;
};

class reduce_relay_test : public beast::unit_test::suite {
    using Slot = Squelch::Slot<PeerSim>;

    bool
    checkCounting(PublicKey const& validator, boost::optional<std::pair<bool,bool>> const& test)
    {
        if (test)
        {
            auto [b1, b2] = *test;
            auto [countingState, countsReset] = overlay_.isCountingState(validator);
            BEAST_EXPECT(countingState == b1);
            BEAST_EXPECT(countsReset == b2);
            return countingState == b1 && countsReset == b2;
        }
        return true;
    }

    void
    send(PublicKey const& validator,
         std::set<id_t> const& peers, std::uint32_t messages, SendCB f)
    {
        for (auto p : peers)
            for (int m = 0; m < messages; m++)
                overlay_.send(validator, p,
                             [&](PublicKey const& validator,
                                 std::weak_ptr<PeerSim> wp, std::uint32_t duration)
                    {
                        f(validator, wp, duration, p, m);
                    });
    }

    bool
    sendAndSquelch(PublicKey const& validator,
                   std::set<id_t> const& peers, std::uint32_t messages,
                   bool log,
                   boost::optional<std::pair<bool, bool>> test = {})
    {
        std::uint32_t squelched = 0;
        send(validator, peers, messages,
                             [&](PublicKey const&,
                                 std::weak_ptr<PeerSim> wp, std::uint32_t d,
                                    id_t p, uint32_t m)
                    {
                        if (m != (messages - 1))
                        {
                            if (log)
                                std::cout << "peer " << p
                                          << " messages " << m
                                          << " " << messages << std::endl;
                            BEAST_EXPECT(0);
                        }
                        else
                        {
                            auto peer = wp.lock();
                            assert(peer);
                            BEAST_EXPECT(peers.find(peer->id()) == peers.end());
                            squelched++;
                        }
                    });
        if (log)
            std::cout << "squelched " << squelched << std::endl;
        BEAST_EXPECT(squelched == MAX_PEERS - Slot::MAX_SELECTED_PEERS);
        return(checkCounting(validator, test));
    }

    bool
    sendNoSquelch(PublicKey const& validator,
                  std::set<id_t> const& peers, std::uint32_t messages,
                  boost::optional<std::pair<bool, bool>> test)
    {
        send(validator, peers, messages,
             [this](PublicKey const&, std::weak_ptr<PeerSim> wp,
                    std::uint32_t, id_t id, std::uint32_t) {
               BEAST_EXPECT(0);
             });
        return checkCounting(validator, test);
    }

    bool
    sendNoSquelchN(PublicKey const& validator,
                  std::uint16_t n, std::uint32_t messages,
                  boost::optional<std::pair<bool, bool>> test)
    {
        std::set<id_t> peers;
        for (int i = 0; i < n; i++)
            peers.insert(i);
        return sendNoSquelch(validator, peers, messages, test);
    }

    void doTest(const std::string &msg, bool log, std::function<void(bool)> f)
    {
        if (log)
            std::cout << "=== " << msg << " ===\n";
        f(log);
    }

    /** Initial counting round: three peers receive message "faster" then
     * others. Once the message count for the three peers reaches threshold
     * the rest of the peers are squelched and the slot for the give validator
     * is in Selected state.
     */
    void
    testInitialRound(bool log)
    {
        doTest("Initial Round", log, [this](bool log){
            // message to the last peer resets all counts and sets ther peer's count to 1
            BEAST_EXPECT(sendNoSquelchN(validator1_, MAX_PEERS, 1, {{true, false}}));
            BEAST_EXPECT(sendAndSquelch(
                validator1_, {0, 1, 2}, Slot::MESSAGE_COUNT_THRESHOLD + 1, log, {{false, true}}));
        });
    }

    /** Receiving message from squelched peer too soon should not change the
     * slot's state to Counting.
     */
    void
    testPeerUnsquelchedTooSoon(bool log)
    {
        doTest("Peer Unsquelched Too Soon", log, [this](bool log) {
            BEAST_EXPECT(sendNoSquelch(validator1_, {3}, 1, {{false, true}}));
        });
    }

    /** Receiving message from squelched peer should change the
     * slot's state to Counting.
     */
    void
    testPeerUnsquelched(bool log)
    {
        sleep(2); // wait to make squelch from the previous round expire
        doTest("Peer Unsquelched", log, [this](bool log) {
            BEAST_EXPECT(sendNoSquelch(validator1_, {3}, 1, {{true, false}}));
            // updates count for the peer
            BEAST_EXPECT(sendNoSquelch(validator1_, {3}, 1, {{true, false}}));
        });
    }

    /** Receiving message from new peer should change the
     * slot's state to Counting.
     */
    void
    testNewPeer(bool log)
    {
        doTest("New Peer", log, [this](bool log) {
            overlay_.addPeer();
            BEAST_EXPECT(sendNoSquelch(validator1_, {MAX_PEERS}, 1, {{true, false}}));
            // updates count for the peer
            BEAST_EXPECT(sendNoSquelch(validator1_, {MAX_PEERS}, 1, {{true, false}}));
        });
    }

    /** Selected peer disconnects. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testSelectedDisconnects(bool log)
    {
        doTest("Selected Peer Disconnects", log, [this](bool log) {
            auto squelched = overlay_.inState(validator1_, Squelch::PeerState::Squelched);
            std::uint16_t n = 0;
            overlay_.unsquelch(0, [&](PublicKey const&, std::weak_ptr<PeerSim> wp) {
                auto peer = wp.lock();
                assert(peer);
                BEAST_EXPECT(peer->id() != 0 && peer->id() != 1 && peer->id() != 2);
                n++;
            });
            if (log)
                std::cout << "squelched " << squelched << " " << n << std::endl;
            BEAST_EXPECT(n == *squelched);
        });
    }

    /** Selected peer stops relaying. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testStopsRelaying(bool log)
    {
        doTest("Selected Peer Stops Relaying", log, [this](bool log) {
        });
    }

public:
    reduce_relay_test() {}

    static constexpr std::uint32_t MAX_PEERS = 10;
    OverlaySim overlay_;
    PublicKey validator1_;
    PublicKey validator2_;

    void
    init()
    {
        validator1_ = std::get<0>(randomKeyPair(KeyType::ed25519));
        validator2_ = std::get<0>(randomKeyPair(KeyType::ed25519));

        for (int i = 0; i < MAX_PEERS; i++)
            overlay_.addPeer();

        Squelch::Squelch::setConfig(seconds(1), seconds(2), seconds(0));
    }

    void run() override {
        init();

        testInitialRound(true);
        testPeerUnsquelchedTooSoon(true);
        testPeerUnsquelched(true);
        testNewPeer(true);
        testSelectedDisconnects(true);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(reduce_relay, ripple_data, ripple, 20);

} // test

} // ripple