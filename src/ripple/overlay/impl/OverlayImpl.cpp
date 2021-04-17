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

#include <ripple/overlay/impl/OverlayImpl.h>

namespace ripple {

// TODO needs to be split into p2p/app config
// with p2p config done in the p2p layer
P2POverlay::Setup
setup_Overlay(BasicConfig const& config)
{
    P2POverlay::Setup setup;

    {
        auto const& section = config.section("overlay");
        setup.context = make_SSLContext("");

        set(setup.ipLimit, "ip_limit", section);
        if (setup.ipLimit < 0)
            Throw<std::runtime_error>("Configured IP limit is invalid");

        std::string ip;
        set(ip, "public_ip", section);
        if (!ip.empty())
        {
            boost::system::error_code ec;
            setup.public_ip = beast::IP::Address::from_string(ip, ec);
            if (ec || beast::IP::is_private(setup.public_ip))
                Throw<std::runtime_error>("Configured public IP is invalid");
        }
    }

    {
        auto const& section = config.section("crawl");
        auto const& values = section.values();

        if (values.size() > 1)
        {
            Throw<std::runtime_error>(
                "Configured [crawl] section is invalid, too many values");
        }

        bool crawlEnabled = true;

        // Only allow "0|1" as a value
        if (values.size() == 1)
        {
            try
            {
                crawlEnabled = boost::lexical_cast<bool>(values.front());
            }
            catch (boost::bad_lexical_cast const&)
            {
                Throw<std::runtime_error>(
                    "Configured [crawl] section has invalid value: " +
                    values.front());
            }
        }

        if (crawlEnabled)
        {
            if (get<bool>(section, "overlay", true))
            {
                setup.crawlOptions |= CrawlOptions::Overlay;
            }
            if (get<bool>(section, "server", true))
            {
                setup.crawlOptions |= CrawlOptions::ServerInfo;
            }
            if (get<bool>(section, "counts", false))
            {
                setup.crawlOptions |= CrawlOptions::ServerCounts;
            }
            if (get<bool>(section, "unl", true))
            {
                setup.crawlOptions |= CrawlOptions::Unl;
            }
        }
    }
    {
        auto const& section = config.section("vl");

        set(setup.vlEnabled, "enabled", section);
    }

    try
    {
        auto id = config.legacy("network_id");

        if (!id.empty())
        {
            if (id == "main")
                id = "0";

            if (id == "testnet")
                id = "1";

            if (id == "devnet")
                id = "2";

            setup.networkID = beast::lexicalCastThrow<std::uint32_t>(id);
        }
    }
    catch (...)
    {
        Throw<std::runtime_error>(
            "Configured [network_id] section is invalid: must be a number "
            "or one of the strings 'main', 'testnet' or 'devnet'.");
    }

    return setup;
}

std::unique_ptr<Overlay>
make_Overlay(
    Application& app,
    P2POverlay::Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<OverlayImpl<P2POverlayImpl>>(
        app,
        setup,
        parent,
        serverHandler,
        resourceManager,
        resolver,
        io_service,
        config,
        collector);
}

}  // namespace ripple
