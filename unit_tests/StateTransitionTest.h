// ======================================================================
// Copyright (C) 2015 Associated Universities, Inc. Washington DC, USA.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
// Correspondence concerning GBT software should be addressed as follows:
//  GBT Operations
//  National Radio Astronomy Observatory
//  P. O. Box 2
//  Green Bank, WV 24944-0002 USA

#ifndef StateTransitionTest_h
#define StateTransitionTest_h

#include <cppunit/extensions/HelperMacros.h>

class StateTransitionTest : public CppUnit::TestCase
{
    CPPUNIT_TEST_SUITE(StateTransitionTest);
    CPPUNIT_TEST(test_cbs);
    CPPUNIT_TEST(test_simple_fsm);
    CPPUNIT_TEST(test_medium_fsm);
    CPPUNIT_TEST(test_fancy_fsm);
    CPPUNIT_TEST(test_consistency_check);
    CPPUNIT_TEST(test_sequence_fsm);
    CPPUNIT_TEST_SUITE_END();
    
    public:
    void test_cbs();
    void test_simple_fsm();
    void test_medium_fsm();
    void test_fancy_fsm();
    void test_consistency_check();
    void test_sequence_fsm();
};


#endif
