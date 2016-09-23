// Copyright (c) 2014-2016 The Dash Core developers

#include "cachemap.h"

#include "test/test_dash.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(cachemap_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(cachemap_test)
{
    // create a CacheMap limited to 10 items
    CacheMap<int,int> mapTest1(10);

    // check that the max size is 10
    BOOST_CHECK(mapTest1.GetMaxSize() == 10);

    // check that the size is 0
    BOOST_CHECK(mapTest1.GetSize() == 0);

    // insert (-1, -1)
    mapTest1.Insert(-1, -1);

    // make sure that the size is updated
    BOOST_CHECK(mapTest1.GetSize() == 1);

    // make sure the map contains the key
    BOOST_CHECK(mapTest1.HasKey(-1) == true);

    // add 10 items
    for(int i = 0; i < 10; ++i) {
        mapTest1.Insert(i, i);
    }

    // check that the size is 10
    BOOST_CHECK(mapTest1.GetSize() == 10);

    // check that the map contains the expected items
    for(int i = 0; i < 10; ++i) {
        int nVal = 0;
        BOOST_CHECK(mapTest1.Get(i, nVal) == true);
        BOOST_CHECK(nVal == i);
    }

    // check that the map no longer contains the first item
    BOOST_CHECK(mapTest1.HasKey(-1) == false);

}

BOOST_AUTO_TEST_SUITE_END()
