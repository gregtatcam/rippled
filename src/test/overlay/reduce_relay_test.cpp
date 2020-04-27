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
#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/thread.hpp>
#include <ripple.pb.h>

namespace ripple {

namespace test {

using namespace boost::asio;
using namespace std::chrono;

class Peer;
using SquelchCB =
    std::function<void(PublicKey const&, std::weak_ptr<Peer>, std::uint32_t)>;
using SendCB = std::function<void(
    PublicKey const&,
    std::weak_ptr<Peer>,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t)>;
using UnsquelchCB = std::function<void(PublicKey const&, std::weak_ptr<Peer>)>;

static constexpr std::uint32_t MAX_PEERS = 10;
static constexpr std::uint32_t MAX_VALIDATORS = 10;
static constexpr std::uint32_t MAX_MESSAGES = 10000;

/** Manually advanced clock. */
class ManualClock
{
public:
    typedef uint64_t rep;
    typedef std::milli period;
    typedef std::chrono::duration<std::uint32_t, period> duration;
    typedef std::chrono::time_point<ManualClock> time_point;

    static void
    advance(duration d) noexcept
    {
        now_ += d;
    }

    static void
    randAdvance(milliseconds min, milliseconds max)
    {
        now_ += randDuration(min, max);
    }

    static void
    reset() noexcept
    {
        now_ = time_point(seconds(0));
    }

    static time_point
    now() noexcept
    {
        return now_;
    }

    static duration
    randDuration(milliseconds min, milliseconds max)
    {
        return duration(milliseconds(rand_int(min.count(), max.count())));
    }

private:
    ManualClock() = delete;
    ~ManualClock() = delete;
    ManualClock(ManualClock const&) = delete;

    inline static time_point now_ = time_point(seconds(0));
    inline static const bool is_steady = false;
};

/** Simulate two entities - peer direcly connected to the server
 * (via squelch in PeerSim) and PeerImp (via overlay)
 */
class Peer : public std::enable_shared_from_this<Peer>
{
public:
    using id_t = std::uint32_t;
    Peer() = default;
    virtual ~Peer() = default;

    virtual id_t
    id() = 0;

    virtual void
    onMessage(std::shared_ptr<Message> const& m, SquelchCB f) = 0;
    virtual void
    onMessage(protocol::TMSquelch const& squelch) = 0;
    void
    send(protocol::TMSquelch const& squelch)
    {
        onMessage(squelch);
    }

    std::shared_ptr<Peer>
    shared()
    {
        return shared_from_this();
    }
};

/** Simulate server's OverlayImpl */
class Overlay
{
public:
    Overlay() = default;
    virtual ~Overlay() = default;

    virtual void
    checkForSquelch(
        PublicKey const& validator,
        std::shared_ptr<Peer> peer,
        SquelchCB f,
        protocol::MessageType type = protocol::mtVALIDATION) = 0;

    virtual void checkIdle(UnsquelchCB) = 0;

    virtual void
    unsquelch(Peer::id_t const&, UnsquelchCB) = 0;
};

/** Simulate the link from the validator to the peer which directly connected
 * to the server.
 */
class Link
{
    using Latency = std::pair<milliseconds, milliseconds>;

public:
    Link(
        std::shared_ptr<Peer> peer,
        Latency const& latency = {milliseconds(5), milliseconds(15)},
        std::uint16_t priority = 2)
        : peer_(peer), latency_(latency), priority_(priority)
    {
        auto sp = peer_.lock();
        assert(sp);
    }
    ~Link() = default;
    void
    send(std::shared_ptr<Message> const& m, SquelchCB f)
    {
        auto sp = peer_.lock();
        assert(sp);
        ManualClock::randAdvance(std::get<0>(latency_), std::get<1>(latency_));
        sp->onMessage(m, f);
    }
    void
    setPriority(std::uint16_t priority)
    {
        priority_ = priority;
    }
    std::uint16_t
    getPriority()
    {
        return priority_;
    }

private:
    std::weak_ptr<Peer> peer_;
    Latency latency_;  // link latency(min,max)
    std::uint16_t
        priority_;  // link priority, 1 is highest
                    // validator sends to links in the order of priority
};

class Validator
{
    using Links = std::unordered_map<Peer::id_t, std::shared_ptr<Link>>;

public:
    Validator()
    {
        pkey_ = std::get<0>(randomKeyPair(KeyType::ed25519));
    }

    PublicKey const&
    key()
    {
        return pkey_;
    }

    operator PublicKey() const
    {
        return pkey_;
    }

    void
    addPeer(std::shared_ptr<Peer> peer)
    {
        links_.emplace(std::make_pair(
            peer->id(), std::move(std::make_shared<Link>(peer))));
    }

    void
    deletePeer(Peer::id_t id)
    {
        links_.erase(id);
    }

    /** Pick high priority links if not set yet */
    void
    setPriority()
    {
        std::vector<std::shared_ptr<Link>> v;
        std::uint8_t nHigh = 0;

        for (auto& [id, link] : links_)
        {
            if (link->getPriority() != 1)
                v.push_back(link);
            else
                nHigh++;
        };

        while (nHigh != Squelch::MAX_SELECTED_PEERS)
        {
            auto i = rand_int(v.size() - 1);
            v[i]->setPriority(1);
            v.erase(v.begin() + i);
            nHigh++;
        }
    }

    void
    prepSquelch(std::function<void(std::shared_ptr<Message>)> f)
    {
        ManualClock::randAdvance(milliseconds(30), milliseconds(60));

        protocol::TMValidation v;
        v.set_validation("validation");
        auto m = std::make_shared<Message>(v, protocol::mtVALIDATION, pkey_);

        f(m);
    }

    /** Send to specific peers */
    void
    send(std::vector<Peer::id_t> peers, SquelchCB f)
    {
        prepSquelch([&](std::shared_ptr<Message> m) {
            for (auto id : peers)
            {
                assert(links_.find(id) != links_.end());
                links_[id]->send(m, f);
            }
        });
    }

    /** Send to all peers - high priority first.*/
    void
    send(SquelchCB f)
    {
        prepSquelch([&](std::shared_ptr<Message> m) {
            std::vector<std::shared_ptr<Link>> links;
            for (auto& [id, link] : links_)
            {
                if (link->getPriority() == 1)
                    links.insert(links.begin(), link);
                else
                    links.push_back(link);
            }

            for (auto& link : links)
                link->send(m, f);
        });
    }

private:
    Links links_;
    PublicKey pkey_;
};

class PeerSim : public Peer
{
public:
    using id_t = Peer::id_t;
    PeerSim(Overlay& overlay) : overlay_(overlay)
    {
        id_ = sid_++;
    }

    ~PeerSim() = default;

    id_t
    id() override
    {
        return id_;
    }

    void
    onMessage(std::shared_ptr<Message> const& m, SquelchCB f) override
    {
        auto validator = m->getValidatorKey();
        assert(validator);
        if (squelch_.isSquelched(*validator))
            return;

        overlay_.checkForSquelch(*validator, shared(), f);
    }

    virtual void
    onMessage(protocol::TMSquelch const& squelch) override
    {
        // std::cout << "squelched " << id_ << " " << squelch.squelch()
        //          << " " << squelch.squelchduration() << std::endl;
        auto validator = squelch.validatorpubkey();
        PublicKey key(Slice(validator.data(), validator.size()));
        squelch_.squelch(key, squelch.squelch(), squelch.squelchduration());
    }

    static void
    resetId()
    {
        sid_ = 0;
    }

private:
    inline static id_t sid_ = 0;
    id_t id_;
    Overlay& overlay_;
    Squelch::Squelch<ManualClock> squelch_;
};

class OverlaySim : public Overlay
{
    using Peers = std::unordered_map<Peer::id_t, std::shared_ptr<Peer>>;

public:
    using id_t = Peer::id_t;
    using clock_type = ManualClock;
    OverlaySim()
    {
    }

    ~OverlaySim() = default;

    void
    checkForSquelch(
        PublicKey const& validator,
        std::shared_ptr<Peer> peer,
        SquelchCB f,
        protocol::MessageType type = protocol::mtVALIDATION) override
    {
        slots_.checkForSquelch(validator, peer->id(), peer, type, f);
    }

    void
    unsquelch(id_t const& id, UnsquelchCB f) override
    {
        slots_.unsquelch(id, f);
    }

    void
    checkIdle(UnsquelchCB f) override
    {
        slots_.checkIdle(f);
    }

    std::shared_ptr<Peer>
    addPeer()
    {
        auto peer = std::make_shared<PeerSim>(*this);
        auto id = peer->id();
        peers_.emplace(std::make_pair(id, peer));
        return peer;
    }

    void
    deletePeer(Peer::id_t id)
    {
        peers_.erase(id);
    }

    std::pair<bool, bool>
    isCountingState(PublicKey const& validator)
    {
        return slots_.isCountingState(validator);
    }

    template <typename Comp = std::equal_to<>>
    boost::optional<std::uint16_t>
    inState(
        PublicKey const& validator,
        Squelch::PeerState state,
        Comp comp = {})
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

    std::unordered_map<
        id_t,
        std::tuple<Squelch::PeerState, std::uint16_t, std::uint32_t>>
    getPeers(PublicKey const& validator)
    {
        return slots_.getPeers(validator);
    }

    std::unordered_map<id_t, clock_type::time_point> const&
    getIdled() const
    {
        return slots_.getIdled();
    }

private:
    Peers peers_;
    Squelch::Slots<Peer, ManualClock> slots_;
};

class Network
{
public:
    Network() : validators_(MAX_VALIDATORS)
    {
        for (int p = 0; p < MAX_PEERS; p++)
        {
            auto peer = overlay_.addPeer();
            for (auto& v : validators_)
                v.addPeer(peer);
        }

        for (auto& v : validators_)
            v.setPriority();
    }

    ~Network() = default;

    Peer::id_t
    addPeer()
    {
        auto peer = overlay_.addPeer();
        for (auto& v : validators_)
            v.addPeer(peer);
        return peer->id();
    }

    void
    deletePeer(Peer::id_t id)
    {
        overlay_.deletePeer(id);
        for (auto& v : validators_)
            v.deletePeer(id);
    }

    void
    deleteLastPeer(PublicKey const& validator)
    {
        auto peers = overlay_.getPeers(validator);
        std::uint16_t maxId = 0;
        for (auto& [id, peer] : peers)
        {
            if (maxId < id)
                maxId = id;
        }
        deletePeer(maxId);
    }

    Validator&
    validator(std::uint16_t v)
    {
        assert(v < validators_.size());
        return validators_[v];
    }

    OverlaySim&
    overlay()
    {
        return overlay_;
    }

private:
    OverlaySim overlay_;
    std::vector<Validator> validators_;
};

class TestRandom
{
public:
    TestRandom(
        bool log = true,
        std::uint16_t nValidators = MAX_VALIDATORS,
        std::uint16_t nPeers = MAX_PEERS,
        std::uint16_t nMessages = MAX_MESSAGES,
        std::uint8_t min = 1,
        std::uint8_t max = 2,
        std::uint8_t idled = 2)
        : log_(log)
        , nValidators_(nValidators)
        , nPeers_(nPeers)
        , nMessages_(nMessages)
    {
        ManualClock::reset();
    }

    struct PeerInfo
    {
        std::uint32_t count_ = 0;
        ManualClock::time_point expire_ =
            ManualClock::time_point(milliseconds(0));
        Squelch::PeerState state_ = Squelch::PeerState::Counting;
    };

    void
    for_rand(
        std::uint32_t min,
        std::uint32_t max,
        std::function<void(std::uint32_t)> f)
    {
        auto size = max - min;
        std::vector<std::uint32_t> s(size);
        std::iota(s.begin(), s.end(), min);
        while (s.size() != 0)
        {
            auto i = s.size() > 1 ? rand_int(s.size() - 1) : 0;
            f(s[i]);
            s.erase(s.begin() + i);
        }
    }

    /** Requirements
     * - Generate messages at random given interval
     * - Send messages from random validators
     * - Send messages to random peers
     * - Randomly delete squelched and selected peers
     * - Randomly age a selected peer (stop sending to the peer)
     * - Randomly age s slot (stop sending from the validator)
     */
    void
    run(Network& network)
    {
        using vid_t = std::uint32_t;
        using pid_t = std::uint32_t;
        std::unordered_map<vid_t, std::unordered_map<pid_t, PeerInfo>>
            validators;

        for (int m = 0; m < nMessages_; m++)
        {
            ManualClock::advance(milliseconds(rand_int(500, 800)));
            for_rand(0, nValidators_, [&](std::uint32_t v) {
                bool squelched = false;
                int n = 0;
                std::stringstream str;
                protocol::TMSquelch squelch;
                squelch.set_squelch(true);
                auto& validator = network.validator(v);
                squelch.set_validatorpubkey(
                    validator.key().data(), validator.key().size());
                validator.send([&](PublicKey const&,
                                   std::weak_ptr<Peer> peerPtr,
                                   std::uint32_t duration) {
                    auto peer = peerPtr.lock();
                    assert(peer);
                    auto p = peer->id();
                    squelched = true;
                    n++;
                    str << p << " ";
                    squelch.set_squelchduration(duration);
                    peer->send(squelch);
                });

                if (squelched)
                {
                    if (log_)
                        std::cout << "random: squelched peers validator: " << v
                                  << " num: " << n << " peers: " << str.str()
                                  << " time: "
                                  << (double)duration_cast<milliseconds>(
                                         ManualClock::now().time_since_epoch())
                                         .count() /
                                1000.
                                  << std::endl;
                }
            });
        }
    }

private:
    bool log_;
    std::uint16_t nValidators_;
    std::uint16_t nPeers_;
    std::uint16_t nMessages_;
};

class reduce_relay_test : public beast::unit_test::suite
{
    using Slot = Squelch::Slot<PeerSim, ManualClock>;
    using id_t = Peer::id_t;

    void
    printPeers(PublicKey const& validator)
    {
        auto idled = network_.overlay().getIdled();
        auto peers = network_.overlay().getPeers(validator);
        for (auto& [id, peer] : peers)
        {
            auto [state, count, expire] = peer;
            std::cout << "peer - id: " << id << " state: " << (int)state
                      << " count: " << count << " expire: " << expire
                      << " idled: "
                      << duration_cast<milliseconds>(
                             idled[id].time_since_epoch())
                             .count()
                      << std::endl;
        }
    }

    bool
    checkCounting(
        PublicKey const& validator,
        boost::optional<std::pair<bool, bool>> const& test)
    {
        if (test)
        {
            auto [b1, b2] = *test;
            auto [countingState, countsReset] =
                network_.overlay().isCountingState(validator);
            BEAST_EXPECT(countingState == b1);
            BEAST_EXPECT(countsReset == b2);
            return countingState == b1 && countsReset == b2;
        }
        return true;
    }

    void
    send(
        Validator& validator,
        std::set<id_t> const& peers,
        std::uint32_t messages,
        SendCB f)
    {
        for (auto p : peers)
            for (int m = 0; m < messages; m++)
                validator.send(
                    {p},
                    [&](PublicKey const& validator,
                        std::weak_ptr<Peer> wp,
                        std::uint32_t duration) {
                        f(validator, wp, duration, p, m);
                    });
    }

    bool
    sendAndSquelch(
        Validator& validator,
        std::set<id_t> const& peers,
        std::uint32_t messages,
        bool log,
        boost::optional<std::pair<bool, bool>> test = {})
    {
        std::uint32_t squelched = 0;
        protocol::TMSquelch squelch;
        squelch.set_squelch(true);
        squelch.set_validatorpubkey(
            validator.key().data(), validator.key().size());
        send(
            validator,
            peers,
            messages,
            [&](PublicKey const&,
                std::weak_ptr<Peer> wp,
                std::uint32_t duration,
                id_t p,
                uint32_t m) {
                if (m != (messages - 1))
                {
                    if (log)
                        std::cout << "peer " << p << " messages " << m << " "
                                  << messages << std::endl;
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
                        peer->send(squelch);
                }
            });
        if (log)
            std::cout << "squelched " << squelched << std::endl;
        BEAST_EXPECT(squelched == MAX_PEERS - Squelch::MAX_SELECTED_PEERS);
        return (checkCounting(validator, test));
    }

    bool
    sendNoSquelch(
        Validator& validator,
        std::set<id_t> const& peers,
        std::uint32_t messages,
        boost::optional<std::pair<bool, bool>> test)
    {
        send(
            validator,
            peers,
            messages,
            [this](
                PublicKey const&,
                std::weak_ptr<Peer> wp,
                std::uint32_t,
                id_t id,
                std::uint32_t) { BEAST_EXPECT(0); });
        return checkCounting(validator, test);
    }

    bool
    sendNoSquelchN(
        Validator& validator,
        std::uint16_t n,
        std::uint32_t messages,
        boost::optional<std::pair<bool, bool>> test)
    {
        std::set<id_t> peers;
        for (int i = 0; i < n; i++)
            peers.insert(i);
        return sendNoSquelch(validator, peers, messages, test);
    }

    void
    doTest(const std::string& msg, bool log, std::function<void(bool)> f)
    {
        if (log)
            std::cout << "=== " << msg << " ===\n";
        f(log);
    }

    /** Set to counting state - send message from a new peer */
    id_t
    setCounting(Validator& validator, bool log)
    {
        auto id = network_.addPeer();
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
        doTest("Initial Round", log, [this](bool log) {
            // message to the last peer resets all counts and sets ther peer's
            // count to 1
            BEAST_EXPECT(sendNoSquelchN(
                network_.validator(0), MAX_PEERS, 1, {{true, false}}));
            BEAST_EXPECT(sendAndSquelch(
                network_.validator(0),
                {0, 1, 2},
                Squelch::MESSAGE_COUNT_THRESHOLD + 1,
                log,
                {{false, true}}));
        });
    }

    /** Receiving message from squelched peer too soon should not change the
     * slot's state to Counting.
     */
    void
    testPeerUnsquelchedTooSoon(bool log)
    {
        doTest("Peer Unsquelched Too Soon", log, [this](bool log) {
            BEAST_EXPECT(
                sendNoSquelch(network_.validator(0), {3}, 1, {{false, true}}));
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
            BEAST_EXPECT(
                sendNoSquelch(network_.validator(0), {3}, 1, {{true, false}}));
            // updates count for the peer
            BEAST_EXPECT(
                sendNoSquelch(network_.validator(0), {3}, 1, {{true, false}}));
        });
    }

    /** Receiving message from new peer should change the
     * slot's state to Counting.
     */
    void
    testNewPeer(bool log)
    {
        doTest("New Peer", log, [this](bool log) {
            auto id = setCounting(network_.validator(0), log);
            // updates count for the peer
            BEAST_EXPECT(
                sendNoSquelch(network_.validator(0), {id}, 1, {{true, false}}));
        });
    }

    bool
    unsquelch(PublicKey const& validator, std::set<id_t> peers, bool log)
    {
        bool res = true;
        for (auto id : peers)
        {
            auto selected = network_.overlay().getSelected(validator);
            bool inSelected = selected.find(id) != selected.end();
            auto squelched = network_.overlay().inState(
                validator, Squelch::PeerState::Squelched);
            std::uint16_t n = 0;
            network_.overlay().unsquelch(
                id, [&](PublicKey const& pkey, std::weak_ptr<Peer> wp) {
                    auto peer = wp.lock();
                    assert(peer);
                    auto good = peers.find(peer->id()) == peers.end();
                    BEAST_EXPECT(good);
                    if (good)
                    {
                        protocol::TMSquelch squelch;
                        squelch.set_squelch(false);
                        squelch.set_validatorpubkey(pkey.data(), pkey.size());
                        peer->send(squelch);
                    }
                    n++;
                });
            if (log)
                std::cout << "unsquelched " << squelched << " " << n
                          << std::endl;
            BEAST_EXPECT(
                (!inSelected && n == 0) || (inSelected && n == *squelched));
            res = res &&
                ((!inSelected && n == 0) || (inSelected && n == *squelched));
        }
        return res;
    }

    /** Unsquelch slot */
    bool
    unsquelchSelected(PublicKey const& validator, bool log)
    {
        auto id = network_.overlay().getSelectedPeer(network_.validator(0));
        if (log)
            std::cout << "selected peer " << id << std::endl;
        return unsquelch(validator, {id}, log);
    }

    void
    unsquelchNew(PublicKey const& validator, bool log)
    {
        auto peers = network_.overlay().getPeers(validator);
        for (auto& [id, peer] : peers)
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
            BEAST_EXPECT(unsquelchSelected(network_.validator(0), log));
        });
    }

    /** Selected peer stops relaying. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testPeerStopsRelaying(bool log)
    {
        doTest("Selected Peer Stops Relaying", log, [this](bool log) {
            unsquelchNew(network_.validator(0), log);
            BEAST_EXPECT(sendAndSquelch(
                network_.validator(0),
                {0, 1, 2},
                Squelch::MESSAGE_COUNT_THRESHOLD + 1,
                log,
                {{false, true}}));
            ManualClock::advance(seconds(601));
            // might not get any peers squelched since not selected peer is
            // simply deleted - no need to send unsquelch. this depends on the
            // traversal order.
            auto selected =
                network_.overlay().getSelected(network_.validator(0));
            auto squelched = network_.overlay().inState(
                network_.validator(0), Squelch::PeerState::Squelched);
            int n = 0;
            network_.overlay().checkIdle(
                [&](PublicKey const& pkey, std::weak_ptr<Peer> peerPtr) {
                    auto peer = peerPtr.lock();
                    assert(peer);
                    auto id = peer->id();
                    auto good = selected.find(id) == selected.end();
                    BEAST_EXPECT(good);
                    if (good)
                    {
                        protocol::TMSquelch squelch;
                        squelch.set_squelch(false);
                        squelch.set_validatorpubkey(pkey.data(), pkey.size());
                        peer->send(squelch);
                    }
                    n++;
                });
            BEAST_EXPECT(n == 0 || (n != 0 && n == *squelched));
        });
        // all peers expired, there is no slot either
        BEAST_EXPECT(checkCounting(network_.validator(0), {{false, false}}));
    }

    void
    testRandom(bool log)
    {
        doTest("Random Test", log, [&](bool log) {
            TestRandom test(log);
            test.run(network_);
        });
    }

    Network network_;

public:
    reduce_relay_test()
    {
    }

    void
    run() override
    {
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

}  // namespace test

}  // namespace ripple