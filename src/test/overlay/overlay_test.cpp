//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/overlay/impl/P2POverlayImpl.h>
#include <ripple/overlay/make_Overlay.h>
#include <test/jtx/Env.h>

namespace ripple {

namespace test {

class overlay_test : public beast::unit_test::suite
{
    std::unique_ptr<P2POverlayImpl> overlay1_;
    std::unique_ptr<P2POverlayImpl> overlay2_;
    jtx::Env env_;

public:
    overlay_test() : env_(*this)
    {
    }

    void
    testOverlay()
    {
        testcase("Overlay");
        /*
        overlay1_ = std::make_shared<P2POverlayImpl>(
               env_.app(),
               setup_Overlay(env_.app().config()),
               env_,
            );
            */
        BEAST_EXPECT(1);
    }

    void
    run() override
    {
        testOverlay();
    }
};

BEAST_DEFINE_TESTSUITE(overlay, ripple_data, ripple);

}  // namespace test

}  // namespace ripple