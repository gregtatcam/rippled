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
#include <ripple/basics/random.h>
#include <ripple/beast/unit_test.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/Slot.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple.pb.h>
#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/thread.hpp>

namespace ripple {

namespace test {

using namespace boost::asio;
using namespace std::chrono;

class Peer;
using SquelchCB = std::function<void(PublicKey const&,
                                     std::weak_ptr<Peer>, std::uint32_t)>;
using SendCB = std::function<void(PublicKey const&, std::weak_ptr<Peer>,
                                  std::uint32_t, std::uint32_t, std::uint32_t)>;
using UnsquelchCB = std::function<void(PublicKey const&,
                                       std::weak_ptr<Peer>)>;

class ManualClock
{
public:
    typedef uint64_t rep;
    typedef std::ratio<1l, 1000000000l> period;
    typedef std::chrono::duration<rep, period> duration;
    typedef std::chrono::time_point<ManualClock> time_point;
    
    static void advance(duration d) noexcept
    {
        now_ += d;
    }
    static time_point now() noexcept
    {
        return now_;
    }

private:
    ManualClock() = delete;
    ~ManualClock() = delete;
    ManualClock(ManualClock const&) = delete;
    
    inline static time_point now_ = time_point(microseconds(0));
    inline static const bool is_steady = false;
};

class Peer : public std::enable_shared_from_this<Peer>
{
public:
    using id_t = std::uint32_t;
    Peer() = default;
    virtual ~Peer() = default;
    
    virtual id_t id() = 0;
    
    virtual void send(std::shared_ptr<Message> const& m, SquelchCB f) = 0;
    virtual void onMessage(protocol::TMSquelch const& squelch) = 0;
    
    std::shared_ptr<Peer>
    shared()
    {
        return shared_from_this();
    }
};

using Peers = std::unordered_map<Peer::id_t, std::shared_ptr<Peer>>;

class Overlay {
public:
    Overlay() = default;
    virtual ~Overlay() = default;
    
    virtual void
    checkForSquelch(PublicKey const& validator,
                    std::shared_ptr<Peer> peer,
                    SquelchCB f,
                    protocol::MessageType type = protocol::mtVALIDATION) = 0;
};

class Validator {
public:
    Validator(Peers& peers)
    : peers_(peers)
    {
        pkey_ = std::get<0>(randomKeyPair(KeyType::ed25519));
    }
    
    PublicKey const&
    key()
    {
        return pkey_;
    }
    
    operator PublicKey () const { return pkey_; }
    
    void
    send(std::vector<Peer::id_t> peers, SquelchCB f)
    {
        protocol::TMValidation v;
        v.set_validation("validation");
        auto m = std::make_shared<Message>(v, protocol::mtVALIDATION, pkey_);
        
        auto to = [&]() {
            if (!peers.empty())
                return peers;
            else
            {
                std::vector<Peer::id_t> to;
                std::vector<Peer::id_t> v1;
                std::transform(peers_.begin(),
                        peers_.end(), std::back_inserter(v1),
                        [](auto const& it) {return it.first;});
                while (to.size() != v1.size())
                {
                    auto i = rand_int(v1.size() - to.size() - 1);
                    to.push_back(v1[i]);
                }
                return to;
            }
        }();
        for (auto& id: to)
            peers_[id]->send(m, f);
    }
    
private:
    Peers& peers_;
    PublicKey pkey_;
};

class OverlaySim : public Overlay {
public:
    using id_t = Peer::id_t;
    using clock_type = ManualClock;
    OverlaySim () {}
    
    ~OverlaySim () = default;
    
    void
    checkForSquelch(PublicKey const& validator,
                    std::shared_ptr<Peer> peer,
                    SquelchCB f,
                    protocol::MessageType type = protocol::mtVALIDATION) override
    {
        slots_.checkForSquelch(validator, peer->id(), peer, type, f);
    }
    
    void
    unsquelch(id_t const& id, UnsquelchCB f)
    {
        slots_.unsquelch(id, f);
    }
    
    void
    checkIdle(UnsquelchCB f)
    {
        slots_.checkIdle(f);
    }
    
    id_t
    addPeer();
    
    /*
    void
    send(PublicKey const& validator, id_t id, SquelchCB f,
         protocol::MessageType type = protocol::mtVALIDATION)
    {
        checkForSquelch(validator, peers_[id], f, type);
    }*/
    
    std::pair<bool, bool>
    isCountingState(PublicKey const& validator)
    {
        return slots_.isCountingState(validator);
    }
    
    template<typename Comp = std::equal_to<>>
    boost::optional<std::uint16_t>
    inState(PublicKey const& validator, Squelch::PeerState state, Comp comp = {})
    {
        return slots_.inState(validator, state, comp);
    }
    
    std::set<id_t>
    getSelected(PublicKey const& validator)
    {
        return slots_.getSelected(validator);
    }
    
    id_t
    getSelectedPeer(PublicKey const& validator)
    {
        auto selected = slots_.getSelected(validator);
        assert(selected.size());
        return *selected.begin();
    }
    
    std::unordered_map<id_t, std::tuple<Squelch::PeerState,
        std::uint16_t, std::uint32_t>>
    getPeers(PublicKey const& validator)
    {
        return slots_.getPeers(validator);
    }
    
    std::unordered_map<id_t, clock_type::time_point> const&
    getIdled() const
    {
        return slots_.getIdled();
    }
    
    Validator
    makeValidator()
    {
        return Validator(peers_);
    }

private:
    Peers peers_;
    Squelch::Slots<Peer, ManualClock> slots_;
};

class PeerSim : public Peer {
public:
    using id_t = Peer::id_t;
    PeerSim (OverlaySim &overlay)
    : overlay_(overlay)
    {
        id_ = sid_++;
    }

    ~PeerSim () = default;
    
    id_t
    id() override
    {
        return id_;
    }
    
    void send(std::shared_ptr<Message> const& m, SquelchCB f) override
    {
        auto validator = m->getValidatorKey();
        assert(validator);
        if (squelch_.isSquelched(*validator))
            return;
        
        // forward to overlay
        overlay_.checkForSquelch(*validator, shared(), f);
    }
    
    virtual void onMessage(protocol::TMSquelch const& squelch) override
    {
        std::cout << "squelched " << id_ << " " << squelch.squelch()
                  << " " << squelch.squelchduration() << std::endl;
        auto validator = squelch.validatorpubkey();
        PublicKey key(Slice(validator.data(), validator.size()));
        squelch_.squelch(key, squelch.squelch(), squelch.squelchduration());
    }

private:
    inline static id_t sid_ = 0;
    id_t id_;
    OverlaySim& overlay_;
    Squelch::Squelch<ManualClock> squelch_;
};

class TestRandom
{
public:
    TestRandom(bool log = true, std::uint16_t nValidators=10, std::uint16_t nPeers=10,
               std::uint16_t nMessages=1000, std::uint8_t min=1,
               std::uint8_t max=2, std::uint8_t idled=2)
    : _log(log)
    , nValidators_(nValidators)
    , nPeers_(nPeers)
    , nMessages_(nMessages)
    {
        for (int i = 0; i < nPeers; i++)
            overlay_.addPeer();
        for (int i = 0; i < nValidators; i++)
            validators_.emplace_back(overlay_.makeValidator());
        //Squelch::Squelch<TestClock>::configSquelchDuration(seconds(min), seconds(max), seconds(0));
        //Squelch::Slots<Peer, TestClock>::configIdled(seconds(idled));
    }
    
    /** Requirements
     * - Generate messages at random given interval
     * - Send messages from random validators
     * - Send messages to random peers
     * - Randomly delete squelched and selected peers
     * - Randomly age a selected peer (stop sending to the peer)
     * - Randomly age s slot (stop sending from the validator)
     */
    void run()
    {
        // include chrono, thread
        //std::this_thread::sleep_for(std::chrono::milliseconds(x));
    }
    
private:
    bool _log;
    std::uint16_t nValidators_;
    std::uint16_t nPeers_;
    std::uint16_t nMessages_;
    OverlaySim overlay_;
    std::vector<Validator> validators_;
};

id_t
OverlaySim::addPeer() {
    auto peer = std::make_shared<PeerSim>(*this);
    auto id = peer->id();
    peers_.emplace(std::make_pair(id, std::move(peer)));
    return id;
}


class reduce_relay_test : public beast::unit_test::suite {
    using Slot = Squelch::Slot<PeerSim, ManualClock>;
    using id_t = Peer::id_t;
    
    void
    printPeers(PublicKey const& validator)
    {
        auto idled = overlay_.getIdled();
        auto peers = overlay_.getPeers(validator);
        for (auto &[id,peer] : peers)
        {
            auto [state, count, expire] = peer;
            std::cout << "peer - id: " << id << " state: " << (int)state
                      << " count: " << count
                      << " expire: " << expire
                      << " idled: " << duration_cast<milliseconds>(
                          idled[id].time_since_epoch()).count()
                      << std::endl;
        }
    }

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
    send(Validator& validator,
         std::set<id_t> const& peers, std::uint32_t messages, SendCB f)
    {
        for (auto p : peers)
            for (int m = 0; m < messages; m++)
                validator.send({p},
                             [&](PublicKey const& validator,
                                 std::weak_ptr<Peer> wp, std::uint32_t duration)
                    {
                        f(validator, wp, duration, p, m);
                    });
    }

    bool
    sendAndSquelch(Validator& validator,
                   std::set<id_t> const& peers, std::uint32_t messages,
                   bool log,
                   boost::optional<std::pair<bool, bool>> test = {})
    {
        std::uint32_t squelched = 0;
        protocol::TMSquelch squelch;
        squelch.set_squelch(true);
        squelch.set_validatorpubkey(validator.key().data(), validator.key().size());
        send(validator, peers, messages,
                             [&](PublicKey const&,
                                 std::weak_ptr<Peer> wp, std::uint32_t duration,
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
                            auto good = peers.find(peer->id()) == peers.end();
                            BEAST_EXPECT(good);
                            squelched++;
                            squelch.clear_squelchduration();
                            squelch.set_squelchduration(duration);
                            if (good)
                                peer->onMessage(squelch);
                        }
                    });
        if (log)
            std::cout << "squelched " << squelched << std::endl;
        BEAST_EXPECT(squelched == MAX_PEERS - Slot::MAX_SELECTED_PEERS);
        return(checkCounting(validator, test));
    }

    bool
    sendNoSquelch(Validator& validator,
                  std::set<id_t> const& peers, std::uint32_t messages,
                  boost::optional<std::pair<bool, bool>> test)
    {
        send(validator, peers, messages,
             [this](PublicKey const&, std::weak_ptr<Peer> wp,
                    std::uint32_t, id_t id, std::uint32_t) {
               BEAST_EXPECT(0);
             });
        return checkCounting(validator, test);
    }

    bool
    sendNoSquelchN(Validator& validator,
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

    /** Set to counting state - send message from a new peer */
    id_t setCounting(Validator& validator, bool log)
    {
        auto id = overlay_.addPeer();
        BEAST_EXPECT(sendNoSquelch(validator, {id}, 1, {{true, false}}));
        return id;
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
        ManualClock::advance(seconds(601));
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
            auto id = setCounting(validator1_, log);
            // updates count for the peer
            BEAST_EXPECT(sendNoSquelch(validator1_, {id}, 1, {{true, false}}));
        });
    }
    
    bool
    unsquelch(PublicKey const& validator, std::set<id_t> peers, bool log)
    {
        bool res = true;
        for (auto id : peers)
        {
            auto selected = overlay_.getSelected(validator);
            bool inSelected = selected.find(id) != selected.end();
            auto squelched =
                overlay_.inState(validator, Squelch::PeerState::Squelched);
            std::uint16_t n = 0;
            overlay_.unsquelch(
                id, [&](PublicKey const& pkey, std::weak_ptr<Peer> wp) {
                    auto peer = wp.lock();
                    assert(peer);
                    auto good = peers.find(peer->id()) == peers.end();
                    BEAST_EXPECT(good);
                    if (good)
                    {
                        protocol::TMSquelch squelch;
                        squelch.set_squelch(false);
                        squelch.set_validatorpubkey(
                            pkey.data(), pkey.size());
                        peer->onMessage(squelch);
                    }
                    n++;
                });
            if (log)
                std::cout << "unsquelched " << squelched << " " << n << std::endl;
            BEAST_EXPECT((!inSelected && n == 0) || (inSelected && n == *squelched));
            res = res && ((!inSelected && n == 0) || (inSelected && n == *squelched));
        }
        return res;
    }

    /** Unsquelch slot */
    bool
    unsquelchSelected(PublicKey const& validator, bool log)
    {
        auto id = overlay_.getSelectedPeer(validator1_);
        if (log)
            std::cout << "selected peer " << id << std::endl;
        return unsquelch(validator, {id}, log);
    }
    
    void
    unsquelchNew(PublicKey const& validator, bool log)
    {
        auto peers = overlay_.getPeers(validator);
        for (auto& [id, peer]: peers)
            if (id >= MAX_PEERS)
                unsquelch(validator, {id}, log);
    }
    
    /** Selected peer disconnects. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testSelectedPeerDisconnects(bool log)
    {
        doTest("Selected Peer Disconnects", log, [this](bool log) {
            std::cout << "===\n";
            BEAST_EXPECT(unsquelchSelected(validator1_, log));
        });
    }

    /** Selected peer stops relaying. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testPeerStopsRelaying(bool log)
    {
        doTest("Selected Peer Stops Relaying", log, [this](bool log) {
            Squelch::Slots<Peer, ManualClock>::configIdled(seconds(0));
            Squelch::Squelch<ManualClock>::configSquelchDuration(seconds(0), seconds(1), seconds(0));
            unsquelchNew(validator1_, log);
            BEAST_EXPECT(sendAndSquelch(
              validator1_, {0, 1, 2}, Slot::MESSAGE_COUNT_THRESHOLD + 1, log, {{false, true}}));
            ManualClock::advance(seconds(601));
            // might not get any peers squelched since not selected peer is simply
            // deleted - no need to send unsquelch. this depends on the traversal
            // order.
            auto selected = overlay_.getSelected(validator1_);
            auto squelched = overlay_.inState(validator1_, Squelch::PeerState::Squelched);
            int n = 0;
            overlay_.checkIdle([&](PublicKey const& pkey, std::weak_ptr<Peer> peerPtr) {
                auto peer = peerPtr.lock();
                assert(peer);
                auto id = peer->id();
                auto good = selected.find(id) == selected.end();
                BEAST_EXPECT(good);
                if (good)
                {
                    protocol::TMSquelch squelch;
                    squelch.set_squelch(false);
                    squelch.set_validatorpubkey(
                        pkey.data(), pkey.size());
                    peer->onMessage(squelch);
                }
                n++;
            });
            BEAST_EXPECT(n == 0 || (n != 0 && n == *squelched));
        });
        // all peers expired, there is no slot either
        BEAST_EXPECT(checkCounting(validator1_, {{false, false}}));
    }
    
    void
    testRandom(bool log)
    {
        TestRandom test(log);
    }

public:
    reduce_relay_test()
    : validator1_(overlay_.makeValidator()){
    
    }

    static constexpr std::uint32_t MAX_PEERS = 10;
    OverlaySim overlay_;
    Validator validator1_;

    void
    init()
    {
        for (int i = 0; i < MAX_PEERS; i++)
            overlay_.addPeer();

        Squelch::Squelch<ManualClock>::configSquelchDuration(seconds(300), seconds(600), seconds(0));
        Squelch::Slots<Peer, ManualClock>::configIdled(seconds(4));
    }

    void run() override {
        init();

        testInitialRound(true);
        testPeerUnsquelchedTooSoon(true);
        testPeerUnsquelched(true);
        testNewPeer(true);
        testSelectedPeerDisconnects(true);
        testPeerStopsRelaying(true);
        testRandom(true);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(reduce_relay, ripple_data, ripple, 20);

} // test

} // ripple