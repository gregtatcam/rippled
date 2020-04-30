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
class Link;

using MessageSPtr = std::shared_ptr<Message>;
using PeerSPtr = std::shared_ptr<Peer>;
using PeerWPtr = std::weak_ptr<Peer>;
using SquelchCB =
    std::function<void(PublicKey const&, PeerWPtr, std::uint32_t)>;
using SendCB = std::function<void(
    PublicKey const&,
    PeerWPtr,
    std::uint32_t,
    std::uint32_t,
    std::uint32_t)>;
using UnsquelchCB = std::function<void(PublicKey const&, PeerWPtr)>;
using LinkIterCB = std::function<void(Link&, MessageSPtr)>;

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
    onMessage(MessageSPtr const& m, SquelchCB f) = 0;
    virtual void
    onMessage(protocol::TMSquelch const& squelch) = 0;
    void
    send(protocol::TMSquelch const& squelch)
    {
        onMessage(squelch);
    }

    PeerSPtr
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
        PeerSPtr peer,
        SquelchCB f,
        protocol::MessageType type = protocol::mtVALIDATION) = 0;

    virtual void checkIdle(UnsquelchCB) = 0;

    virtual void
    unsquelch(Peer::id_t const&, UnsquelchCB) = 0;
};

class Validator;

/** Simulate the link from the validator to the peer which directly connected
 * to the server.
 */
class Link
{
    using Latency = std::pair<milliseconds, milliseconds>;

public:
    Link(
        Validator& validator,
        PeerSPtr peer,
        Latency const& latency = {milliseconds(5), milliseconds(15)},
        std::uint16_t priority = 2)
        : validator_(validator)
        , peer_(peer)
        , latency_(latency)
        , priority_(priority)
    {
        auto sp = peer_.lock();
        assert(sp);
    }
    ~Link() = default;
    void
    send(MessageSPtr const& m, SquelchCB f)
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
    Validator&
    validator()
    {
        return validator_;
    }
    Peer::id_t
    getPeerId()
    {
        auto sp = peer_.lock();
        assert(sp);
        return sp->id();
    }

private:
    Validator& validator_;
    PeerWPtr peer_;
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
        protocol::TMValidation v;
        v.set_validation("validation");
        message_ = std::make_shared<Message>(v, protocol::mtVALIDATION, pkey_);
        id_ = sid_++;
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
    addPeer(PeerSPtr peer)
    {
        links_.emplace(std::make_pair(
            peer->id(), std::move(std::make_shared<Link>(*this, peer))));
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

    /** Iterate over links for the peers */
    void
    for_links(std::vector<Peer::id_t> peers, LinkIterCB f)
    {
        ManualClock::randAdvance(milliseconds(30), milliseconds(60));
        for (auto id : peers)
        {
            assert(links_.find(id) != links_.end());
            f(*links_[id], message_);
        }
    }

    /** Iterate over links for all peers - high priority first */
    void
    for_links(LinkIterCB f, bool sendLowPriority = true)
    {
        std::vector<std::shared_ptr<Link>> links;
        for (auto& [id, link] : links_)
        {
            if (link->getPriority() == 1)
                links.insert(links.begin(), link);
            else if (sendLowPriority)
                links.push_back(link);
        }
        ManualClock::randAdvance(milliseconds(30), milliseconds(60));

        for (auto& link : links)
            f(*link, message_);
    }

    /** Send to specific peers */
    void
    send(std::vector<Peer::id_t> peers, SquelchCB f)
    {
        for_links(peers, [&](Link& link, MessageSPtr m) { link.send(m, f); });
    }

    /** Send to all peers - high priority first.*/
    void
    send(SquelchCB f)
    {
        for_links([&](Link& link, MessageSPtr m) { link.send(m, f); });
    }

    MessageSPtr
    message()
    {
        return message_;
    }

    std::uint16_t
    id()
    {
        return id_;
    }

private:
    Links links_;
    PublicKey pkey_;
    MessageSPtr message_;
    inline static std::uint16_t sid_ = 0;
    std::uint16_t id_ = 0;
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
    onMessage(MessageSPtr const& m, SquelchCB f) override
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
    using Peers = std::unordered_map<Peer::id_t, PeerSPtr>;

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
        PeerSPtr peer,
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

    PeerSPtr
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
        unsquelch(id, [&](PublicKey const&, PeerWPtr) {});
        peers_.erase(id);
    }

    boost::optional<Peer::id_t>
    deleteLastPeer()
    {
        if (peers_.empty())
            return {};

        std::uint8_t maxId = 0;

        for (auto& [id, peer] : peers_)
        {
            if (id > maxId)
                maxId = id;
        }

        deletePeer(maxId);

        return maxId;
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

    std::uint8_t
    getNumPeers() const
    {
        return peers_.size();
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
    deleteLastPeer()
    {
        auto id = overlay_.deleteLastPeer();

        if (!id)
            return;

        for (auto& validator : validators_)
            validator.deletePeer(*id);
    }

    void
    purgePeers()
    {
        while (overlay_.getNumPeers() > MAX_PEERS)
            deleteLastPeer();
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

    void
    propagate(
        LinkIterCB link,
        std::uint16_t nValidators = MAX_VALIDATORS,
        std::uint16_t nMessages = MAX_MESSAGES,
        bool purge = true,
        bool resetClock = true)
    {
        if (resetClock)
            ManualClock::reset();

        if (purge)
            purgePeers();

        for (int m = 0; m < nMessages; ++m)
        {
            ManualClock::advance(milliseconds(rand_int(500, 800)));
            for_rand(0, nValidators, [&](std::uint32_t v) {
                // send less messages over low priority link
                // to model "slower" link
                // first message is sent to all links
                validators_[v].for_links(link, m == 0 || m % 5);
            });
        }
    }

private:
    OverlaySim overlay_;
    std::vector<Validator> validators_;
};

class reduce_relay_test : public beast::unit_test::suite
{
    using Slot = Squelch::Slot<PeerSim, ManualClock>;
    using id_t = Peer::id_t;
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

    Peer::id_t
    sendSquelch(
        PublicKey const& validator,
        PeerWPtr peerPtr,
        std::uint32_t duration)
    {
        protocol::TMSquelch squelch;
        squelch.set_squelch(true);
        squelch.set_validatorpubkey(validator.data(), validator.size());
        squelch.set_squelchduration(duration);
        auto peer = peerPtr.lock();
        assert(peer);
        peer->send(squelch);
        return peer->id();
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
    random(bool log)
    {
        network_.propagate([&](Link& link, MessageSPtr m) {
            protocol::TMSquelch squelch;
            squelch.set_squelch(true);
            auto& validator = link.validator();
            squelch.set_validatorpubkey(
                validator.key().data(), validator.key().size());

            bool squelched = false;
            int n = 0;
            std::stringstream str;

            link.send(
                m,
                [&](PublicKey const& key,
                    PeerWPtr peerPtr,
                    std::uint32_t duration) {
                    auto p = sendSquelch(key, peerPtr, duration);
                    squelched = true;
                    n++;
                    str << p << " ";
                });

            if (squelched)
            {
                auto selected = network_.overlay().getSelected(validator);
                str << " selected: ";
                for (auto s : selected)
                    str << s << " ";
                if (log)
                    std::cout << "random: squelched peers validator: "
                              << validator.id() << " num: " << n
                              << " peers: " << str.str() << " time: "
                              << (double)duration_cast<milliseconds>(
                                     ManualClock::now().time_since_epoch())
                                     .count() /
                            1000.
                              << std::endl;
                auto [countingState, countsReset] =
                    network_.overlay().isCountingState(validator);
                BEAST_EXPECT(countingState == false);
                BEAST_EXPECT(countsReset);
            }
        });
    }

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
    checkCounting(PublicKey const& validator, bool isCountingState)
    {
        auto [countingState, countsReset] =
            network_.overlay().isCountingState(validator);
        BEAST_EXPECT(countingState == isCountingState);
        return countingState == isCountingState;
    }

    void
    doTest(const std::string& msg, bool log, std::function<void(bool)> f)
    {
        if (log)
            std::cout << "=== " << msg << " ===\n";
        f(log);
    }

    /** Initial counting round: three peers receive message "faster" then
     * others. Once the message count for the three peers reaches threshold
     * the rest of the peers are squelched and the slot for the given validator
     * is in Selected state.
     */
    void
    testInitialRound(bool log)
    {
        doTest("Initial Round", log, [this](bool log) {
            BEAST_EXPECT(propagateAndSquelch(log));
        });
    }

    /** Receiving message from squelched peer too soon should not change the
     * slot's state to Counting.
     */
    void
    testPeerUnsquelchedTooSoon(bool log)
    {
        doTest("Peer Unsquelched Too Soon", log, [this](bool log) {
            BEAST_EXPECT(propagateNoSquelch(log, 1, false, false, false));
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
            BEAST_EXPECT(propagateNoSquelch(log, 2, true, true, false));
        });
    }

    bool
    propagateAndSquelch(bool log, bool purge = true, bool resetClock = true)
    {
        int n = 0;
        network_.propagate(
            [&](Link& link, MessageSPtr message) {
                std::uint16_t squelched = 0;
                link.send(
                    message,
                    [&](PublicKey const& key,
                        PeerWPtr peerPtr,
                        std::uint32_t duration) {
                        squelched++;
                        sendSquelch(key, peerPtr, duration);
                    });
                if (squelched)
                {
                    BEAST_EXPECT(
                        squelched == MAX_PEERS - Squelch::MAX_SELECTED_PEERS);
                    n++;
                }
            },
            1,
            Squelch::MESSAGE_COUNT_THRESHOLD + 2,
            purge,
            resetClock);
        BEAST_EXPECT(n == 1);
        auto res = checkCounting(network_.validator(0), false);
        BEAST_EXPECT(res);
        return n == 1 && res;
    }

    bool
    propagateNoSquelch(
        bool log,
        std::uint16_t nMessages,
        bool countingState,
        bool purge = true,
        bool resetClock = true)
    {
        bool squelched = false;
        network_.propagate(
            [&](Link& link, MessageSPtr message) {
                link.send(
                    message,
                    [&](PublicKey const& key,
                        PeerWPtr peerPtr,
                        std::uint32_t duration) {
                        squelched = true;
                        BEAST_EXPECT(false);
                    });
            },
            1,
            nMessages,
            purge,
            resetClock);
        auto res = checkCounting(network_.validator(0), countingState);
        return !squelched && res;
    }

    /** Receiving message from new peer should change the
     * slot's state to Counting.
     */
    void
    testNewPeer(bool log)
    {
        doTest("New Peer", log, [this](bool log) {
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            network_.addPeer();
            BEAST_EXPECT(propagateNoSquelch(log, 1, true, false, false));
        });
    }

    /** Selected peer disconnects. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testSelectedPeerDisconnects(bool log)
    {
        doTest("Selected Peer Disconnects", log, [this](bool log) {
            ManualClock::advance(seconds(601));
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            auto id = network_.overlay().getSelectedPeer(network_.validator(0));
            std::uint16_t unsquelched = 0;
            network_.overlay().unsquelch(
                id,
                [&](PublicKey const& key, PeerWPtr peer) { unsquelched++; });
            BEAST_EXPECT(
                unsquelched == MAX_PEERS - Squelch::MAX_SELECTED_PEERS);
            BEAST_EXPECT(checkCounting(network_.validator(0), true));
        });
    }

    /** Selected peer stops relaying. Should change the state to counting and
     * unsquelch squelched peers. */
    void
    testPeerStopsRelaying(bool log)
    {
        doTest("Selected Peer Stops Relaying", log, [this](bool log) {
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            ManualClock::advance(seconds(5));
            std::uint16_t unsquelched = 0;
            network_.overlay().checkIdle(
                [&](PublicKey const& key, PeerWPtr peer) { unsquelched++; });
            auto peers = network_.overlay().getPeers(network_.validator(0));
            BEAST_EXPECT(
                unsquelched == MAX_PEERS - Squelch::MAX_SELECTED_PEERS);
            BEAST_EXPECT(peers.empty());
            BEAST_EXPECT(checkCounting(network_.validator(0), false));
        });
    }

    void
    testRandom(bool log)
    {
        doTest("Random Test", log, [&](bool log) { random(log); });
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