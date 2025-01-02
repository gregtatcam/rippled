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

#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/IOUIssue.h>
#include <map>
#include <set>
#include <typeinfo>
#include <unordered_set>

#if BEAST_MSVC
#define STL_SET_HAS_EMPLACE 1
#else
#define STL_SET_HAS_EMPLACE 0
#endif

#ifndef RIPPLE_ASSETS_ENABLE_STD_HASH
#if BEAST_MAC || BEAST_IOS
#define RIPPLE_ASSETS_ENABLE_STD_HASH 0
#else
#define RIPPLE_ASSETS_ENABLE_STD_HASH 1
#endif
#endif

namespace ripple {

class Issue_test : public beast::unit_test::suite
{
public:
    // Comparison, hash tests for uint60 (via base_uint)
    template <typename Unsigned>
    void
    testUnsigned()
    {
        Unsigned const u1(1);
        Unsigned const u2(2);
        Unsigned const u3(3);

        BEAST_EXPECT(u1 != u2);
        BEAST_EXPECT(u1 < u2);
        BEAST_EXPECT(u1 <= u2);
        BEAST_EXPECT(u2 <= u2);
        BEAST_EXPECT(u2 == u2);
        BEAST_EXPECT(u2 >= u2);
        BEAST_EXPECT(u3 >= u2);
        BEAST_EXPECT(u3 > u2);

        std::hash<Unsigned> hash;

        BEAST_EXPECT(hash(u1) == hash(u1));
        BEAST_EXPECT(hash(u2) == hash(u2));
        BEAST_EXPECT(hash(u3) == hash(u3));
        BEAST_EXPECT(hash(u1) != hash(u2));
        BEAST_EXPECT(hash(u1) != hash(u3));
        BEAST_EXPECT(hash(u2) != hash(u3));
    }

    //--------------------------------------------------------------------------

    // Comparison, hash tests for Issue
    template <class IOUIssue>
    void
    testIssue()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Currency const c3(3);
        AccountID const i3(3);

        BEAST_EXPECT(IOUIssue(c1, i1) != IOUIssue(c2, i1));
        BEAST_EXPECT(IOUIssue(c1, i1) < IOUIssue(c2, i1));
        BEAST_EXPECT(IOUIssue(c1, i1) <= IOUIssue(c2, i1));
        BEAST_EXPECT(IOUIssue(c2, i1) <= IOUIssue(c2, i1));
        BEAST_EXPECT(IOUIssue(c2, i1) == IOUIssue(c2, i1));
        BEAST_EXPECT(IOUIssue(c2, i1) >= IOUIssue(c2, i1));
        BEAST_EXPECT(IOUIssue(c3, i1) >= IOUIssue(c2, i1));
        BEAST_EXPECT(IOUIssue(c3, i1) > IOUIssue(c2, i1));
        BEAST_EXPECT(IOUIssue(c1, i1) != IOUIssue(c1, i2));
        BEAST_EXPECT(IOUIssue(c1, i1) < IOUIssue(c1, i2));
        BEAST_EXPECT(IOUIssue(c1, i1) <= IOUIssue(c1, i2));
        BEAST_EXPECT(IOUIssue(c1, i2) <= IOUIssue(c1, i2));
        BEAST_EXPECT(IOUIssue(c1, i2) == IOUIssue(c1, i2));
        BEAST_EXPECT(IOUIssue(c1, i2) >= IOUIssue(c1, i2));
        BEAST_EXPECT(IOUIssue(c1, i3) >= IOUIssue(c1, i2));
        BEAST_EXPECT(IOUIssue(c1, i3) > IOUIssue(c1, i2));

        std::hash<IOUIssue> hash;

        BEAST_EXPECT(hash(IOUIssue(c1, i1)) == hash(IOUIssue(c1, i1)));
        BEAST_EXPECT(hash(IOUIssue(c1, i2)) == hash(IOUIssue(c1, i2)));
        BEAST_EXPECT(hash(IOUIssue(c1, i3)) == hash(IOUIssue(c1, i3)));
        BEAST_EXPECT(hash(IOUIssue(c2, i1)) == hash(IOUIssue(c2, i1)));
        BEAST_EXPECT(hash(IOUIssue(c2, i2)) == hash(IOUIssue(c2, i2)));
        BEAST_EXPECT(hash(IOUIssue(c2, i3)) == hash(IOUIssue(c2, i3)));
        BEAST_EXPECT(hash(IOUIssue(c3, i1)) == hash(IOUIssue(c3, i1)));
        BEAST_EXPECT(hash(IOUIssue(c3, i2)) == hash(IOUIssue(c3, i2)));
        BEAST_EXPECT(hash(IOUIssue(c3, i3)) == hash(IOUIssue(c3, i3)));
        BEAST_EXPECT(hash(IOUIssue(c1, i1)) != hash(IOUIssue(c1, i2)));
        BEAST_EXPECT(hash(IOUIssue(c1, i1)) != hash(IOUIssue(c1, i3)));
        BEAST_EXPECT(hash(IOUIssue(c1, i1)) != hash(IOUIssue(c2, i1)));
        BEAST_EXPECT(hash(IOUIssue(c1, i1)) != hash(IOUIssue(c2, i2)));
        BEAST_EXPECT(hash(IOUIssue(c1, i1)) != hash(IOUIssue(c2, i3)));
        BEAST_EXPECT(hash(IOUIssue(c1, i1)) != hash(IOUIssue(c3, i1)));
        BEAST_EXPECT(hash(IOUIssue(c1, i1)) != hash(IOUIssue(c3, i2)));
        BEAST_EXPECT(hash(IOUIssue(c1, i1)) != hash(IOUIssue(c3, i3)));
    }

    template <class Set>
    void
    testIssueSet()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        IOUIssue const a1(c1, i1);
        IOUIssue const a2(c2, i2);

        {
            Set c;

            c.insert(a1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(a2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(IOUIssue(c1, i2)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(IOUIssue(c1, i1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(IOUIssue(c2, i2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Set c;

            c.insert(a1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(a2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(IOUIssue(c1, i2)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(IOUIssue(c1, i1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(IOUIssue(c2, i2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;

#if STL_SET_HAS_EMPLACE
            c.emplace(c1, i1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.emplace(c2, i2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;
#endif
        }
    }

    template <class Map>
    void
    testIssueMap()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        IOUIssue const a1(c1, i1);
        IOUIssue const a2(c2, i2);

        {
            Map c;

            c.insert(std::make_pair(a1, 1));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(std::make_pair(a2, 2));
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(IOUIssue(c1, i2)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(IOUIssue(c1, i1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(IOUIssue(c2, i2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Map c;

            c.insert(std::make_pair(a1, 1));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(std::make_pair(a2, 2));
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(IOUIssue(c1, i2)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(IOUIssue(c1, i1)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(IOUIssue(c2, i2)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }
    }

    void
    testIssueSets()
    {
        testcase("std::set <IOUIssue>");
        testIssueSet<std::set<IOUIssue>>();

        testcase("std::set <IOUIssue>");
        testIssueSet<std::set<IOUIssue>>();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        testcase("std::unordered_set <IOUIssue>");
        testIssueSet<std::unordered_set<IOUIssue>>();

        testcase("std::unordered_set <IOUIssue>");
        testIssueSet<std::unordered_set<IOUIssue>>();
#endif

        testcase("hash_set <IOUIssue>");
        testIssueSet<hash_set<IOUIssue>>();

        testcase("hash_set <IOUIssue>");
        testIssueSet<hash_set<IOUIssue>>();
    }

    void
    testIssueMaps()
    {
        testcase("std::map <IOUIssue, int>");
        testIssueMap<std::map<IOUIssue, int>>();

        testcase("std::map <IOUIssue, int>");
        testIssueMap<std::map<IOUIssue, int>>();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        testcase("std::unordered_map <IOUIssue, int>");
        testIssueMap<std::unordered_map<IOUIssue, int>>();

        testcase("std::unordered_map <IOUIssue, int>");
        testIssueMap<std::unordered_map<IOUIssue, int>>();

        testcase("hash_map <IOUIssue, int>");
        testIssueMap<hash_map<IOUIssue, int>>();

        testcase("hash_map <IOUIssue, int>");
        testIssueMap<hash_map<IOUIssue, int>>();

#endif
    }

    //--------------------------------------------------------------------------

    // Comparison, hash tests for Book
    template <class Book>
    void
    testBook()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        Currency const c3(3);
        AccountID const i3(3);

        IOUIssue a1(c1, i1);
        IOUIssue a2(c1, i2);
        IOUIssue a3(c2, i2);
        IOUIssue a4(c3, i2);

        BEAST_EXPECT(Book(a1, a2) != Book(a2, a3));
        BEAST_EXPECT(Book(a1, a2) < Book(a2, a3));
        BEAST_EXPECT(Book(a1, a2) <= Book(a2, a3));
        BEAST_EXPECT(Book(a2, a3) <= Book(a2, a3));
        BEAST_EXPECT(Book(a2, a3) == Book(a2, a3));
        BEAST_EXPECT(Book(a2, a3) >= Book(a2, a3));
        BEAST_EXPECT(Book(a3, a4) >= Book(a2, a3));
        BEAST_EXPECT(Book(a3, a4) > Book(a2, a3));

        std::hash<Book> hash;

        //         log << std::hex << hash (Book (a1, a2));
        //         log << std::hex << hash (Book (a1, a2));
        //
        //         log << std::hex << hash (Book (a1, a3));
        //         log << std::hex << hash (Book (a1, a3));
        //
        //         log << std::hex << hash (Book (a1, a4));
        //         log << std::hex << hash (Book (a1, a4));
        //
        //         log << std::hex << hash (Book (a2, a3));
        //         log << std::hex << hash (Book (a2, a3));
        //
        //         log << std::hex << hash (Book (a2, a4));
        //         log << std::hex << hash (Book (a2, a4));
        //
        //         log << std::hex << hash (Book (a3, a4));
        //         log << std::hex << hash (Book (a3, a4));

        BEAST_EXPECT(hash(Book(a1, a2)) == hash(Book(a1, a2)));
        BEAST_EXPECT(hash(Book(a1, a3)) == hash(Book(a1, a3)));
        BEAST_EXPECT(hash(Book(a1, a4)) == hash(Book(a1, a4)));
        BEAST_EXPECT(hash(Book(a2, a3)) == hash(Book(a2, a3)));
        BEAST_EXPECT(hash(Book(a2, a4)) == hash(Book(a2, a4)));
        BEAST_EXPECT(hash(Book(a3, a4)) == hash(Book(a3, a4)));

        BEAST_EXPECT(hash(Book(a1, a2)) != hash(Book(a1, a3)));
        BEAST_EXPECT(hash(Book(a1, a2)) != hash(Book(a1, a4)));
        BEAST_EXPECT(hash(Book(a1, a2)) != hash(Book(a2, a3)));
        BEAST_EXPECT(hash(Book(a1, a2)) != hash(Book(a2, a4)));
        BEAST_EXPECT(hash(Book(a1, a2)) != hash(Book(a3, a4)));
    }

    //--------------------------------------------------------------------------

    template <class Set>
    void
    testBookSet()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        IOUIssue const a1(c1, i1);
        IOUIssue const a2(c2, i2);
        Book const b1(a1, a2);
        Book const b2(a2, a1);

        {
            Set c;

            c.insert(b1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(b2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a1)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a1, a2)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Set c;

            c.insert(b1);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.insert(b2);
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a1)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a1, a2)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;

#if STL_SET_HAS_EMPLACE
            c.emplace(a1, a2);
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            c.emplace(a2, a1);
            if (!BEAST_EXPECT(c.size() == 2))
                return;
#endif
        }
    }

    template <class Map>
    void
    testBookMap()
    {
        Currency const c1(1);
        AccountID const i1(1);
        Currency const c2(2);
        AccountID const i2(2);
        IOUIssue const a1(c1, i1);
        IOUIssue const a2(c2, i2);
        Book const b1(a1, a2);
        Book const b2(a2, a1);

        // typename Map::value_type value_type;
        // std::pair <Book const, int> value_type;

        {
            Map c;

            // c.insert (value_type (b1, 1));
            c.insert(std::make_pair(b1, 1));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            // c.insert (value_type (b2, 2));
            c.insert(std::make_pair(b2, 1));
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a1)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a1, a2)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }

        {
            Map c;

            // c.insert (value_type (b1, 1));
            c.insert(std::make_pair(b1, 1));
            if (!BEAST_EXPECT(c.size() == 1))
                return;
            // c.insert (value_type (b2, 2));
            c.insert(std::make_pair(b2, 1));
            if (!BEAST_EXPECT(c.size() == 2))
                return;

            if (!BEAST_EXPECT(c.erase(Book(a1, a1)) == 0))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a1, a2)) == 1))
                return;
            if (!BEAST_EXPECT(c.erase(Book(a2, a1)) == 1))
                return;
            if (!BEAST_EXPECT(c.empty()))
                return;
        }
    }

    void
    testBookSets()
    {
        testcase("std::set <Book>");
        testBookSet<std::set<Book>>();

        testcase("std::set <Book>");
        testBookSet<std::set<Book>>();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        testcase("std::unordered_set <Book>");
        testBookSet<std::unordered_set<Book>>();

        testcase("std::unordered_set <Book>");
        testBookSet<std::unordered_set<Book>>();
#endif

        testcase("hash_set <Book>");
        testBookSet<hash_set<Book>>();

        testcase("hash_set <Book>");
        testBookSet<hash_set<Book>>();
    }

    void
    testBookMaps()
    {
        testcase("std::map <Book, int>");
        testBookMap<std::map<Book, int>>();

        testcase("std::map <Book, int>");
        testBookMap<std::map<Book, int>>();

#if RIPPLE_ASSETS_ENABLE_STD_HASH
        testcase("std::unordered_map <Book, int>");
        testBookMap<std::unordered_map<Book, int>>();

        testcase("std::unordered_map <Book, int>");
        testBookMap<std::unordered_map<Book, int>>();

        testcase("hash_map <Book, int>");
        testBookMap<hash_map<Book, int>>();

        testcase("hash_map <Book, int>");
        testBookMap<hash_map<Book, int>>();
#endif
    }

    //--------------------------------------------------------------------------

    void
    run() override
    {
        testcase("Currency");
        testUnsigned<Currency>();

        testcase("AccountID");
        testUnsigned<AccountID>();

        // ---

        testcase("Issue");
        testIssue<IOUIssue>();

        testcase("Issue");
        testIssue<IOUIssue>();

        testIssueSets();
        testIssueMaps();

        // ---

        testcase("Book");
        testBook<Book>();

        testcase("Book");
        testBook<Book>();

        testBookSets();
        testBookMaps();
    }
};

BEAST_DEFINE_TESTSUITE(Issue, protocol, ripple);

}  // namespace ripple
