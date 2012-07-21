/*
    Copyright 2005-2012 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks.

    Threading Building Blocks is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    version 2 as published by the Free Software Foundation.

    Threading Building Blocks is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Threading Building Blocks; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

#include "harness.h"

#if TBB_USE_EXCEPTIONS
#include "tbb/flow_graph.h"
#include "tbb/task_scheduler_init.h"
#include <iostream>
#include <vector>
#include "harness_assert.h"

tbb::atomic<unsigned> nExceptions;

class Foo {
private:
    // std::vector<int>& m_vec;
    std::vector<int>* m_vec;
public:
    Foo(std::vector<int>& vec) : m_vec(&vec) { }
    void operator() (tbb::flow::continue_msg) const {
        ++nExceptions;
        m_vec->at(m_vec->size()); // Will throw out_of_range exception
        ASSERT(false, "Exception not thrown by invalid access");
    }
};

// test from user ahelwer: http://software.intel.com/en-us/forums/showthread.php?t=103786 
// exception thrown in graph node, not caught in wait_for_all()
void
test_flow_graph_exception0() {
    // Initializes body
    std::vector<int> vec;
    vec.push_back(0);
    Foo f(vec);
    nExceptions = 0;

    // Construct graph and nodes
    tbb::flow::graph g;
    tbb::flow::broadcast_node<tbb::flow::continue_msg> start(g);
    tbb::flow::continue_node<tbb::flow::continue_msg> fooNode(g, f); 

    // Construct edge
    tbb::flow::make_edge(start, fooNode);

    // Execute graph
    ASSERT(!g.exception_thrown(), "exception_thrown flag already set");
    ASSERT(!g.is_cancelled(), "canceled flag already set");
    try {
        start.try_put(tbb::flow::continue_msg());
        g.wait_for_all();
        ASSERT(false, "Exception not thrown");
    }
    catch(std::out_of_range& ex) {
        REMARK("Exception: %s\n", ex.what());
    }
    catch(...) {
        REMARK("Unknown exception caught\n");
    }
    ASSERT(g.exception_thrown(), "Exception not intercepted");
    // if exception set, cancellation also set.
    ASSERT(g.is_cancelled(), "Exception cancellation not signaled");
    // in case we got an exception
    try {
        g.wait_for_all();  // context still signalled canceled, my_exception still set.
    }
    catch(...) {
        ASSERT(false, "Second exception thrown but no task executing");
    }
    ASSERT(!g.exception_thrown(), "exception_thrown flag not reset");
    ASSERT(!g.is_cancelled(), "canceled flag not reset");
}
#endif // TBB_USE_EXCEPTIONS

#if TBB_USE_EXCEPTIONS
int TestMain() {
    for(int nThread=MinThread; nThread<= MaxThread; ++nThread) {
        tbb::task_scheduler_init init(nThread); 
        test_flow_graph_exception0();
    }
    return Harness::Done;
}
#else  // !TBB_USE_EXCEPTION
int TestMain() {
    return Harness::Skipped;
}
#endif

