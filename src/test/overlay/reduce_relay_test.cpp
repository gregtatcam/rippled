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

private:
    std::unordered_map<PeerSim::id_t, std::shared_ptr<PeerSim>> peers_;
    Squelch::Slots<PeerSim> slots_;
};

class reduce_relay_test : public beast::unit_test::suite {
    using Slot = Squelch::Slot<PeerSim>;

    void
    send(PublicKey const& validator,
         std::set<id_t> const& peers, std::uint32_t messages,
         std::function<void(PublicKey const&, std::weak_ptr<PeerSim>,
             std::uint32_t, id_t, uint32_t)> f)
    {
        for (auto p : peers)
            for (int m = 0; m < (messages + 1); m++)
                overlay.send(validator, p,
                             [&](PublicKey const& validator,
                                 std::weak_ptr<PeerSim> wp, std::uint32_t duration)
                    {
                        f(validator, wp, duration, p, m);
                    });
    }

    void
    sendAndSquelch(PublicKey const& validator,
                     std::set<id_t> const& peers, std::uint32_t messages)
    {
        std::uint32_t squelched = 0;
        send(validator, peers, Slot::MESSAGE_COUNT_THRESHOLD,
                             [&](PublicKey const&,
                                 std::weak_ptr<PeerSim> wp, std::uint32_t d,
                                    id_t p, uint32_t m)
                    {
                        if (m != messages)
                            BEAST_EXPECT(0);
                        else
                        {
                            auto peer = wp.lock();
                            assert(peer);
                            if (log)
                                std::cout << "squelched peer " << peer->id()
                                          << std::endl;
                            BEAST_EXPECT(peers.find(peer->id()) == peers.end());
                            squelched++;
                        }
                    });
        if (log)
            std::cout << "squelched " << squelched << std::endl;
        BEAST_EXPECT(squelched == MAX_PEERS - Slot::MAX_SELECTED_PEERS);
    }

    void
    testInitialRound(OverlaySim &overlay,
                     PublicKey const& validator,
                     bool log)
    {
        if (log)
            std::cout << "=== Initial Round " << "3 peers initially selected\n";
        for (int p = 0; p < MAX_PEERS; p++)
            overlay.send(validator, p,
                         [&](PublicKey const&,
                             std::weak_ptr<PeerSim> wp, std::uint32_t)
            {
                BEAST_EXPECT(0);
            });

        sendAndSquelch(validator1, {0,1,2}, Slot::MESSAGE_COUNT_THRESHOLD);

        if (log)
            std::cout << "=== Initial Round complete\n";
    }

public:
    reduce_relay_test() {}

    static constexpr std::uint32_t MAX_PEERS = 10;
    OverlaySim overlay;
    PublicKey validator1;
    PublicKey validator2;

    void
    init()
    {
        validator1 = std::get<0>(randomKeyPair(KeyType::ed25519));
        validator2 = std::get<0>(randomKeyPair(KeyType::ed25519));

        for (int i = 0; i < MAX_PEERS; i++)
            overlay.addPeer();

        Squelch::Squelch::setConfig(seconds(1),
                                    seconds(2),
                                    seconds(1));
    }

    void run() override {
        init();

        testInitialRound(overlay, validator1, true);
        sleep(2);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(reduce_relay, ripple_data, ripple, 20);

} // test

} // ripple