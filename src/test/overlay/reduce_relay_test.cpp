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
#include <boost/thread.hpp>
#include <ripple.pb.h>

namespace ripple {

namespace test {

using namespace std::chrono;

class Peer;
class Link;

using MessageSPtr = std::shared_ptr<Message>;
using LinkSPtr = std::shared_ptr<Link>;
using PeerSPtr = std::shared_ptr<Peer>;
using PeerWPtr = std::weak_ptr<Peer>;
using SquelchCB =
    std::function<void(PublicKey const&, PeerWPtr, std::uint32_t)>;
using UnsquelchCB = std::function<void(PublicKey const&, PeerWPtr)>;
using LinkIterCB = std::function<void(Link&, MessageSPtr)>;

static constexpr std::uint32_t MAX_PEERS = 10;
static constexpr std::uint32_t MAX_VALIDATORS = 10;
static constexpr std::uint32_t MAX_MESSAGES = 200000;

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

/** Simulate two entities - peer directly connected to the server
 * (via squelch in PeerSim) and PeerImp (via Overlay)
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

/** Simulate link from a validator to a peer directly connected
 * to the server.
 */
class Link
{
    using Latency = std::pair<milliseconds, milliseconds>;

public:
    Link(
        Validator& validator,
        PeerSPtr peer,
        Latency const& latency = {milliseconds(5), milliseconds(15)})
        : validator_(validator), peer_(peer), latency_(latency), up_(true)
    {
        auto sp = peer_.lock();
        assert(sp);
    }
    ~Link() = default;
    void
    send(MessageSPtr const& m, SquelchCB f)
    {
        if (!up_)
            return;
        auto sp = peer_.lock();
        assert(sp);
        sp->onMessage(m, f);
    }
    Validator&
    validator()
    {
        return validator_;
    }
    void
    up(bool linkUp)
    {
        up_ = linkUp;
    }
    Peer::id_t
    peerId()
    {
        auto p = peer_.lock();
        assert(p);
        return p->id();
    }
    PeerSPtr
    getPeer()
    {
        auto p = peer_.lock();
        assert(p);
        return p;
    }

private:
    Validator& validator_;
    PeerWPtr peer_;
    Latency latency_;
    bool up_;
};

/** Simulate Validator */
class Validator
{
    using Links = std::unordered_map<Peer::id_t, LinkSPtr>;

public:
    Validator()
    {
        pkey_ = std::get<0>(randomKeyPair(KeyType::ed25519));
        protocol::TMValidation v;
        v.set_validation("validation");
        message_ = std::make_shared<Message>(v, protocol::mtVALIDATION, pkey_);
        id_ = sid_++;
    }
    ~Validator()
    {
        clear();
    }

    void
    clear()
    {
        links_.clear();
    }

    static void
    resetId()
    {
        sid_ = 0;
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

    void
    for_links(std::vector<Peer::id_t> peers, LinkIterCB f)
    {
        for (auto id : peers)
        {
            assert(links_.find(id) != links_.end());
            f(*links_[id], message_);
        }
    }

    void
    for_links(LinkIterCB f, bool simulateSlow = false)
    {
        std::vector<LinkSPtr> v;
        std::transform(
            links_.begin(), links_.end(), std::back_inserter(v), [](auto& kv) {
                return kv.second;
            });
        std::random_device d;
        std::mt19937 g(d());
        std::shuffle(v.begin(), v.end(), g);

        for (auto& link : v)
        {
            f(*link, message_);
        }
    }

    /** Send to specific peers */
    void
    send(std::vector<Peer::id_t> peers, SquelchCB f)
    {
        for_links(peers, [&](Link& link, MessageSPtr m) { link.send(m, f); });
    }

    /** Send to all peers */
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

    void
    linkUp(Peer::id_t id)
    {
        auto it = links_.find(id);
        assert(it != links_.end());
        it->second->up(true);
    }

    void
    linkDown(Peer::id_t id)
    {
        auto it = links_.find(id);
        assert(it != links_.end());
        it->second->up(false);
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

    static void
    resetId()
    {
        sid_ = 0;
    }

    /** Local Peer (PeerImp) */
    void
    onMessage(MessageSPtr const& m, SquelchCB f) override
    {
        auto validator = m->getValidatorKey();
        assert(validator);
        if (squelch_.isSquelched(*validator))
            return;

        overlay_.checkForSquelch(*validator, shared(), f);
    }

    /** Remote Peer (Directly connected Peer) */
    virtual void
    onMessage(protocol::TMSquelch const& squelch) override
    {
        auto validator = squelch.validatorpubkey();
        PublicKey key(Slice(validator.data(), validator.size()));
        squelch_.squelch(key, squelch.squelch(), squelch.squelchduration());
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
    clear()
    {
        peers_.clear();
        ManualClock::advance(hours(1));
        slots_.checkIdle([](PublicKey const&, PeerWPtr) {});
    }

    std::uint16_t
    inState(PublicKey const& validator, Squelch::PeerState state)
    {
        auto res = slots_.inState(validator, state);
        return res ? *res : 0;
    }

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
    addPeer(bool useCache = true)
    {
        PeerSPtr peer{};
        Peer::id_t id;
        if (peersCache_.empty() || !useCache)
        {
            peer = std::make_shared<PeerSim>(*this);
            id = peer->id();
        }
        else
        {
            auto it = peersCache_.begin();
            peer = it->second;
            id = it->first;
            peersCache_.erase(it);
        }
        peers_.emplace(std::make_pair(id, peer));
        return peer;
    }

    void
    deletePeer(Peer::id_t id, bool useCache = true)
    {
        auto it = peers_.find(id);
        assert(it != peers_.end());
        unsquelch(id, [&](PublicKey const&, PeerWPtr) {});
        if (useCache)
            peersCache_.emplace(std::make_pair(id, it->second));
        peers_.erase(it);
    }

    void
    resetPeers()
    {
        while (!peers_.empty())
            deletePeer(peers_.begin()->first);
        while (!peersCache_.empty())
            addPeer();
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

        deletePeer(maxId, false);

        return maxId;
    }

    bool
    isCountingState(PublicKey const& validator)
    {
        auto ret = slots_.inState(validator, Squelch::SlotState::Counting);
        return ret && *ret;
    }

    std::set<id_t>
    getSelected(PublicKey const& validator)
    {
        return slots_.getSelected(validator);
    }

    bool
    isSelected(PublicKey const& validator, Peer::id_t peer)
    {
        auto selected = slots_.getSelected(validator);
        return selected.find(peer) != selected.end();
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
        std::tuple<
            Squelch::PeerState,
            std::uint16_t,
            std::uint32_t,
            std::uint32_t>>
    getPeers(PublicKey const& validator)
    {
        return slots_.getPeers(validator);
    }

    std::uint16_t
    getNumPeers() const
    {
        return peers_.size();
    }

private:
    Peers peers_;
    Peers peersCache_;
    Squelch::Slots<Peer, ManualClock> slots_;
};

class Network
{
public:
    Network()
    {
        init();
    }

    void
    init()
    {
        validators_.resize(MAX_VALIDATORS);
        for (int p = 0; p < MAX_PEERS; p++)
        {
            auto peer = overlay_.addPeer();
            for (auto& v : validators_)
                v.addPeer(peer);
        }
    }

    ~Network() = default;

    void
    reset()
    {
        validators_.clear();
        overlay_.clear();
        PeerSim::resetId();
        Validator::resetId();
        init();
    }

    Peer::id_t
    addPeer()
    {
        auto peer = overlay_.addPeer();
        for (auto& v : validators_)
            v.addPeer(peer);
        return peer->id();
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
    enableLink(std::uint16_t validatorId, Peer::id_t peer, bool enable)
    {
        auto it =
            std::find_if(validators_.begin(), validators_.end(), [&](auto v) {
                return v.id() == validatorId;
            });
        assert(it != validators_.end());
        if (enable)
            it->linkUp(peer);
        else
            it->linkDown(peer);
    }

    void
    onDisconnectPeer(Peer::id_t peer)
    {
        // Send unsquelch to the Peer on all links. This way when
        // the Peer "reconnects" it starts sending messages on the link.
        // We expect that if a Peer disconnects and then reconnects, it's
        // unsquelched.
        protocol::TMSquelch squelch;
        squelch.set_squelch(false);
        for (auto& v : validators_)
        {
            PublicKey key = v;
            squelch.clear_validatorpubkey();
            squelch.set_validatorpubkey(key.data(), key.size());
            v.for_links({peer}, [&](Link& l, MessageSPtr) {
                l.getPeer()->send(squelch);
            });
        }
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
        std::random_device d;
        std::mt19937 g(d());
        std::shuffle(s.begin(), s.end(), g);
        for (auto v : s)
            f(v);
    }

    void
    propagate(
        LinkIterCB link,
        std::uint16_t nValidators = MAX_VALIDATORS,
        std::uint32_t nMessages = MAX_MESSAGES,
        bool purge = true,
        bool resetClock = true)
    {
        if (resetClock)
            ManualClock::reset();

        if (purge)
        {
            purgePeers();
            overlay_.resetPeers();
        }

        for (int m = 0; m < nMessages; ++m)
        {
            ManualClock::randAdvance(milliseconds(500), milliseconds(800));
            for_rand(0, nValidators, [&](std::uint32_t v) {
                validators_[v].for_links(link);
            });
        }
    }

    /** Is peer in Selected state in any of the slots */
    bool
    isSelected(Peer::id_t id)
    {
        for (auto& v : validators_)
        {
            if (overlay_.isSelected(v, id))
                return true;
        }
        return false;
    }

    /** Check if there are peers to unsquelch - peer is in Selected
     * state in any of the slots and there are peers in Squelched state
     * in those slots.
     */
    bool
    allCounting(Peer::id_t peer)
    {
        for (auto& v : validators_)
        {
            if (!overlay_.isSelected(v, peer))
                continue;
            auto peers = overlay_.getPeers(v);
            for (auto& [k, v] : peers)
                if (std::get<Squelch::PeerState>(v) ==
                    Squelch::PeerState::Squelched)
                    return false;
        }
        return true;
    }

private:
    OverlaySim overlay_;
    std::vector<Validator> validators_;
};

class reduce_relay_test : public beast::unit_test::suite
{
    using Slot = Squelch::Slot<PeerSim, ManualClock>;
    using id_t = Peer::id_t;

    void
    printPeers(const std::string& msg, std::uint16_t validator = 0)
    {
        auto peers = network_.overlay().getPeers(network_.validator(validator));
        std::cout << msg << " "
                  << "num peers " << (int)network_.overlay().getNumPeers()
                  << std::endl;
        for (auto& [k, v] : peers)
            std::cout << k << ":" << (int)std::get<Squelch::PeerState>(v)
                      << " ";
        std::cout << std::endl;
    }

    /** Send squelch (if duration is set) or unsquelch (if duration not set) */
    Peer::id_t
    sendSquelch(
        PublicKey const& validator,
        PeerWPtr peerPtr,
        boost::optional<std::uint32_t> duration)
    {
        protocol::TMSquelch squelch;
        bool res = duration ? true : false;
        squelch.set_squelch(res);
        squelch.set_validatorpubkey(validator.data(), validator.size());
        if (res)
            squelch.set_squelchduration(*duration);
        auto peer = peerPtr.lock();
        assert(peer);
        peer->send(squelch);
        return peer->id();
    }

    /** Randomly brings the link between a validator and a peer down.
     * Randomly disconnects a peer. Those events are generated one at a time.
     */
    void
    random(bool log)
    {
        enum State { On, Off, WaitReset };
        enum EventType { LinkDown = 0, PeerDisconnected = 1 };
        // Link down or Peer disconnect event
        // TBD - add new peer event
        // TBD - add overlapping type of events at any
        //       time in any quantity
        struct Event
        {
            State state_ = State::Off;
            std::uint32_t cnt_ = 0;
            std::uint32_t handledCnt_ = 0;
            bool isSelected_ = false;
            Peer::id_t peer_;
            std::uint16_t validator_;
            PublicKey key_;
            time_point<ManualClock> time_;
            bool handled_ = false;
        };
        std::unordered_map<EventType, Event> events{
            {LinkDown, {}}, {PeerDisconnected, {}}};
        time_point<ManualClock> lastCheck = ManualClock::now();

        network_.reset();
        network_.propagate([&](Link& link, MessageSPtr m) {
            auto& validator = link.validator();
            auto now = ManualClock::now();

            bool squelched = false;
            std::stringstream str;

            link.send(
                m,
                [&](PublicKey const& key,
                    PeerWPtr peerPtr,
                    std::uint32_t duration) {
                    assert(key == validator);
                    auto p = sendSquelch(key, peerPtr, duration);
                    squelched = true;
                    str << p << " ";
                });

            if (squelched)
            {
                auto selected = network_.overlay().getSelected(validator);
                str << " selected: ";
                for (auto s : selected)
                    str << s << " ";
                if (log)
                    std::cout
                        << "random, squelched, validator: "
                        << validator.id()
                        << " peers: " << str.str() << " time: "
                        << (double)Squelch::epoch<milliseconds>(now).count() /
                            1000.
                        << std::endl;
                auto countingState =
                    network_.overlay().isCountingState(validator);
                BEAST_EXPECT(countingState == false &&
                             selected.size() == Squelch::MAX_SELECTED_PEERS);
            }

            // Trigger Link Down or Peer Disconnect event
            if (events[EventType::LinkDown].state_ == State::Off &&
                events[EventType::PeerDisconnected].state_ == State::Off)
            {
                auto update = [&](EventType event) {
                    events[event].cnt_++;
                    events[event].validator_ = validator.id();
                    events[event].key_ = validator;
                    events[event].peer_ = link.peerId();
                    events[event].state_ = State::On;
                    events[event].time_ = now;
                    if (event == EventType::LinkDown)
                    {
                        network_.enableLink(
                            validator.id(), link.peerId(), false);
                        events[event].isSelected_ =
                            network_.overlay().isSelected(
                                validator, link.peerId());
                    }
                    else
                        events[event].isSelected_ =
                            network_.isSelected(link.peerId());
                };
                auto r = rand_int(0, 500);
                if (r == (int)EventType::LinkDown ||
                    r == (int)EventType::PeerDisconnected)
                {
                    update(static_cast<EventType>(r));
                }
            }

            if (events[EventType::PeerDisconnected].state_ == State::On)
            {
                auto& event = events[EventType::PeerDisconnected];
                bool allCounting = network_.allCounting(event.peer_);
                network_.overlay().unsquelch(
                    event.peer_, [&](PublicKey const& v, PeerWPtr peerPtr) {
                        if (event.isSelected_)
                            sendSquelch(v, peerPtr, {});
                        event.handled_ = true;
                    });
                // Should only be unsquelched if the peer is in Selected state
                // If in Selected state it's possible unsquelching didn't
                // take place because there is no peers in Squelched state in
                // any of the slots where the peer is in Selected state
                // (allCounting is true)
                bool handled =
                    (event.isSelected_ == false && !event.handled_) ||
                    (event.isSelected_ == true &&
                     (event.handled_ || allCounting));
                BEAST_EXPECT(handled);
                event.state_ = State::Off;
                event.isSelected_ = false;
                event.handledCnt_ += handled;
                event.handled_ = false;
                network_.onDisconnectPeer(event.peer_);
            }

            auto& event = events[EventType::LinkDown];
            // Check every sec for idled peers. Idled peers are
            // created by Link Down event.
            if (now - lastCheck > milliseconds(1000))
            {
                lastCheck = now;
                // Check if Link Down event must be handled by
                // checkIdle(): 1) the peer is in Selected state;
                // 2) the has not recevied any messages for IDLED time;
                // 3) there are peers in Squelched state in the slot.
                bool linkDownMustHandle = false;
                if (event.state_ == State::On)
                {
                    event.isSelected_ =
                        network_.overlay().isSelected(event.key_, event.peer_);
                    auto peers = network_.overlay().getPeers(event.key_);
                    assert(peers.find(event.peer_) != peers.end());
                    auto d = Squelch::epoch<milliseconds>(now).count() -
                        std::get<3>(peers[event.peer_]);
                    linkDownMustHandle = event.isSelected_ &&
                        d > milliseconds(Squelch::IDLED).count() &&
                        network_.overlay().inState(
                            event.key_, Squelch::PeerState::Squelched) > 0;
                }
                network_.overlay().checkIdle(
                    [&](PublicKey const& v, PeerWPtr ptr) {
                        event.handled_ = true;
                        if (linkDownMustHandle && v == event.key_)
                        {
                            event.state_ = State::WaitReset;
                            sendSquelch(validator, ptr, {});
                        }
                    });
                bool handled =
                    (event.handled_ && event.state_ == State::WaitReset) ||
                    (!event.handled_ && !linkDownMustHandle);
                BEAST_EXPECT(handled);
            }
            if (event.state_ == State::WaitReset ||
                (event.state_ == State::On && (now - event.time_ > seconds(6))))
            {
                bool handled =
                    event.state_ == State::WaitReset || !event.handled_;
                BEAST_EXPECT(handled);
                event.state_ = State::Off;
                event.isSelected_ = false;
                event.handledCnt_ += handled;
                event.handled_ = false;
                network_.enableLink(event.validator_, event.peer_, true);
            }
        });

        auto& down = events[EventType::LinkDown];
        auto& disconnected = events[EventType::PeerDisconnected];
        // It's possible the last Down Link event is not handled
        BEAST_EXPECT(down.handledCnt_ >= down.cnt_ - 1);
        // All Peer Disconnect events must be handled
        BEAST_EXPECT(disconnected.cnt_ == disconnected.handledCnt_);
        if (log)
            std::cout << "link down count: " << down.cnt_ << "/"
                      << down.handledCnt_
                      << " peer disconnect count: " << disconnected.cnt_ << "/"
                      << disconnected.handledCnt_;
    }

    bool
    checkCounting(PublicKey const& validator, bool isCountingState)
    {
        auto countingState = network_.overlay().isCountingState(validator);
        BEAST_EXPECT(countingState == isCountingState);
        return countingState == isCountingState;
    }

    void
    doTest(const std::string& msg, bool log, std::function<void(bool)> f)
    {
        if (log)
            std::cout << "==== " << msg << " ====\n";
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

    /** Propagate enough messages to generate one squelch event */
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
            Squelch::MESSAGE_UPPER_THRESHOLD + 2,
            purge,
            resetClock);
        auto selected = network_.overlay().getSelected(network_.validator(0));
        BEAST_EXPECT(selected.size() == Squelch::MAX_SELECTED_PEERS);
        BEAST_EXPECT(n == 1);
        auto res = checkCounting(network_.validator(0), false);
        BEAST_EXPECT(res);
        return n == 1 && res;
    }

    /** Send fewer message so that squelch event is not generated */
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

    /** Receiving a message from new peer should change the
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
    testSelectedPeerStopsRelaying(bool log)
    {
        doTest("Selected Peer Stops Relaying", log, [this](bool log) {
            ManualClock::advance(seconds(601));
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            ManualClock::advance(seconds(5));
            std::uint16_t unsquelched = 0;
            network_.overlay().checkIdle(
                [&](PublicKey const& key, PeerWPtr peer) { unsquelched++; });
            auto peers = network_.overlay().getPeers(network_.validator(0));
            BEAST_EXPECT(
                unsquelched == MAX_PEERS - Squelch::MAX_SELECTED_PEERS);
            BEAST_EXPECT(checkCounting(network_.validator(0), true));
        });
    }

    /** Squelched peer disconnects. Should not change the state to counting.
     */
    void
    testSquelchedPeerDisconnects(bool log)
    {
        doTest("Squelched Peer Disconnects", log, [this](bool log) {
            ManualClock::advance(seconds(601));
            BEAST_EXPECT(propagateAndSquelch(log, true, false));
            auto peers = network_.overlay().getPeers(network_.validator(0));
            auto it = std::find_if(peers.begin(), peers.end(), [&](auto it) {
                return std::get<Squelch::PeerState>(it.second) ==
                    Squelch::PeerState::Squelched;
            });
            assert(it != peers.end());
            std::uint16_t unsquelched = 0;
            network_.overlay().unsquelch(
                it->first,
                [&](PublicKey const& key, PeerWPtr peer) { unsquelched++; });
            BEAST_EXPECT(unsquelched == 0);
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
        bool log = false;
        testInitialRound(log);
        testPeerUnsquelchedTooSoon(log);
        testPeerUnsquelched(log);
        testNewPeer(log);
        testSquelchedPeerDisconnects(log);
        testSelectedPeerDisconnects(log);
        testSelectedPeerStopsRelaying(log);
        testRandom(log);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(reduce_relay, ripple_data, ripple, 20);

}  // namespace test

}  // namespace ripple