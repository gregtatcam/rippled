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

#ifndef RIPPLE_PEERFINDER_SLOTIMP_H_INCLUDED
#define RIPPLE_PEERFINDER_SLOTIMP_H_INCLUDED

#include <ripple/peerfinder/Slot.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/beast/container/aged_unordered_map.h>
#include <ripple/beast/container/aged_container_utility.h>
#include <boost/optional.hpp>
#include <atomic>

namespace ripple {
namespace PeerFinder {

/** Facilitates reduced relaying of TMValidation, TMProposeSet,
 * TMTransaction messages.
 * Upstream messages are counted in non-squelched state. First peer which
 * reaches a threshold for specific message type sets squelchUpstream_
 * to true and directs connected peers to squelch with
 * TMSquelch message. Peers which receive TMSquelch set squelchDownstream_
 * to true and don't relay downstream. All peers are un-squelched
 * for specific message type when the time lapse reaches a
 * threshold. Upstream peers have their own timer and
 * re-set squelchDownstream_ to eliminate the need for TMSquelch (un-squelch)
 * message. The nodes send TMSquelch (un-squelch) message if the peer
 * with squelchUpstream_ set to true disconnects.
 */
class SquelchImp : public Squelch
{
public:
    static constexpr std::uint16_t MAX_MESSAGES = 10;
    static constexpr std::uint16_t MAX_LAPSE = 12000; // seconds
    SquelchImp(beast::IP::Endpoint const& endpoint) : remoteEndpoint_(endpoint) {}
    /** Properties associated with per message type squelch. */
    struct EntryBase {
        virtual ~EntryBase () = default;
        std::atomic_bool squelchDownstream_ = false; // stop sending downstream
        std::size_t timeLapseDownstream_ = 0; // expire downstream squelch after time lapse
        std::uint16_t upstreamMessageCount_ = 0; // received upstream messages
        /** Check if downstream squelch should be expired. */
        void expireDownstream ();
        /** Find peer with highest message count and squelch other peers. */
        virtual bool checkUpstreamSquelch (beast::IP::Endpoint const& endpoint) = 0;
        /** Stop sending messages to downstream peers. */
        virtual void squelchDownstream (beast::IP::Endpoint const& endpoint) = 0;
        /** Un-squelch downstream flag. */
        void unSquelchDownstream ();
    };
    template <typename T>
    struct Entry : public EntryBase
    {
        virtual ~Entry () = default;
        bool checkUpstreamSquelch (beast::IP::Endpoint const& endpoint) override;
        void squelchDownstream (beast::IP::Endpoint const& endpoint) override;
        /** Check if upstream squelch should be expired. */
        static void expireUpstream ();
        /** When a peer is removed or deactivated have to unsquelch upstream peers.
         * If the endpoint matches upstreamEndpoint then reset squelchUpstream
         * and upstreamEndpoint and return true.
         */
        static bool squelched (beast::IP::Endpoint const &endpoint);
        static beast::IP::Endpoint const upstreamEndpoint_; // receive from when rest are squelched
        static std::atomic_bool squelchUpstream_; // indicate TMSquelch should be send upstream
        static std::size_t timeLapseUpstream_; // expire upstream squelch after time lapse
        static std::mutex mutex_; // squelchUpstream_ mutex
    };
    struct ValidationEntry : public Entry<ValidationEntry> {};
    struct ProposeEntry : public Entry<ProposeEntry> {};
    struct TransactionEntry : public Entry<ProposeEntry> {};

    /** Check if upstream squelch should be expired for all message types. */
    static void expireUpstream ();
    /** Check if downstream squelch should be expired for all message types. */
    void expireDownstream () override;
    /** Return Entry for the message type. */
    EntryBase& getEntry (SquelchType type);
    /** Find peer with highest message type count and squelch other peers. */
    bool checkUpstreamSquelch (SquelchType type) override;
    /** Squelch/Un-squelch downstream for the message type. */
    void squelchDownstream (SquelchType type, bool squelch) override;
    /** Return true if squelched downstream. */
    bool squelchedDownstream (SquelchType type) override;
    /** When a peer is removed or deactivated have to unsquelch upstream peers.
     * If the endpoint matches any of upstreamEndpoint then reset squelchUpstream
     * and upstreamEndpoint and return true.
     */
     static bool squelched(SquelchType type, beast::IP::Endpoint const &endpoint);

private:
    beast::IP::Endpoint const& remoteEndpoint_;
    ValidationEntry validation_;
    ProposeEntry propose_;
    TransactionEntry transaction_;
};

class SlotImp : public Slot
{
private:
    using recent_type = beast::aged_unordered_map <beast::IP::Endpoint, int>;

public:
    using ptr = std::shared_ptr <SlotImp>;

    // inbound
    SlotImp (beast::IP::Endpoint const& local_endpoint,
        beast::IP::Endpoint const& remote_endpoint, bool fixed,
            clock_type& clock);

    // outbound
    SlotImp (beast::IP::Endpoint const& remote_endpoint,
        bool fixed, clock_type& clock);

    bool inbound () const override
    {
        return m_inbound;
    }

    bool fixed () const override
    {
        return m_fixed;
    }

    bool reserved () const override
    {
        return m_reserved;
    }

    State state () const override
    {
        return m_state;
    }

    beast::IP::Endpoint const& remote_endpoint () const override
    {
        return m_remote_endpoint;
    }

    boost::optional <beast::IP::Endpoint> const& local_endpoint () const override
    {
        return m_local_endpoint;
    }

    boost::optional <PublicKey> const& public_key () const override
    {
        return m_public_key;
    }

    boost::optional<std::uint16_t> listening_port () const override
    {
        std::uint32_t const value = m_listening_port;
        if (value == unknownPort)
            return boost::none;
        return value;
    }

    Squelch& squelch() override
    {
        return m_squelch;
    }

    void set_listening_port (std::uint16_t port)
    {
        m_listening_port = port;
    }

    void local_endpoint (beast::IP::Endpoint const& endpoint)
    {
        m_local_endpoint = endpoint;
    }

    void remote_endpoint (beast::IP::Endpoint const& endpoint)
    {
        m_remote_endpoint = endpoint;
    }

    void public_key (PublicKey const& key)
    {
        m_public_key = key;
    }

    void reserved (bool reserved_)
    {
        m_reserved = reserved_;
    }

    //--------------------------------------------------------------------------

    void state (State state_);

    void activate (clock_type::time_point const& now);

    // "Memberspace"
    //
    // The set of all recent addresses that we have seen from this peer.
    // We try to avoid sending a peer the same addresses they gave us.
    //
    class recent_t
    {
    public:
        explicit recent_t (clock_type& clock);

        /** Called for each valid endpoint received for a slot.
            We also insert messages that we send to the slot to prevent
            sending a slot the same address too frequently.
        */
        void insert (beast::IP::Endpoint const& ep, int hops);

        /** Returns `true` if we should not send endpoint to the slot. */
        bool filter (beast::IP::Endpoint const& ep, int hops);

    private:
        void expire ();

        friend class SlotImp;
        recent_type cache;
    } recent;

    void expire()
    {
        recent.expire();
        squelch().expireDownstream();
    }

private:
    bool const m_inbound;
    bool const m_fixed;
    bool m_reserved;
    State m_state;
    beast::IP::Endpoint m_remote_endpoint;
    boost::optional <beast::IP::Endpoint> m_local_endpoint;
    boost::optional <PublicKey> m_public_key;

    static std::int32_t constexpr unknownPort = -1;
    std::atomic <std::int32_t> m_listening_port;
    SquelchImp m_squelch;

public:
    // DEPRECATED public data members

    // Tells us if we checked the connection. Outbound connections
    // are always considered checked since we successfuly connected.
    bool checked;

    // Set to indicate if the connection can receive incoming at the
    // address advertised in mtENDPOINTS. Only valid if checked is true.
    bool canAccept;

    // Set to indicate that a connection check for this peer is in
    // progress. Valid always.
    bool connectivityCheckInProgress;

    // The time after which we will accept mtENDPOINTS from the peer
    // This is to prevent flooding or spamming. Receipt of mtENDPOINTS
    // sooner than the allotted time should impose a load charge.
    //
    clock_type::time_point whenAcceptEndpoints;
};

}
}

#endif
