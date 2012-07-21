# Copyright 2005-2012 Intel Corporation.  All Rights Reserved.
#
# This file is part of Threading Building Blocks.
#
# Threading Building Blocks is free software; you can redistribute it
# and/or modify it under the terms of the GNU General Public License
# version 2 as published by the Free Software Foundation.
#
# Threading Building Blocks is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Threading Building Blocks; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As a special exception, you may use this file as part of a free software
# library without restriction.  Specifically, if other files instantiate
# templates or use macros or inline functions from this file, or you compile
# this file and link it with other files to produce an executable, this
# file does not by itself cause the resulting executable to be covered by
# the GNU General Public License.  This exception does not however
# invalidate any other reasons why the executable file might be covered by
# the GNU General Public License.

# GNU Makefile that builds and runs example.
PROG=sub_string_finder_extended
ARGS=
LIGHT_PROG=sub_string_finder

# The C++ compiler
ifneq (,$(shell which icc 2>/dev/null))
CXX=icc
endif # icc

ifeq ($(offload), mic)
override CXXFLAGS += -opt-report-phase:offload -D__TBB_MIC=1
endif

ifeq ($(CXX), icc)
TBBLIB = -tbb
TBBLIB_DEBUG = -tbb
else
TBBLIB = -ltbb
TBBLIB_DEBUG = -ltbb_debug
endif

ifeq ($(shell uname), Linux)
LIBS+= -lrt 
endif

all:	release test

release: *.cpp
ifeq ($(offload),)
	$(CXX) -O2 -DNDEBUG $(CXXFLAGS) -o sub_string_finder sub_string_finder.cpp $(TBBLIB) $(LIBS)
	$(CXX) -O2 -DNDEBUG $(CXXFLAGS) -o sub_string_finder_pretty sub_string_finder_pretty.cpp $(TBBLIB) $(LIBS)
endif
	$(CXX) -O2 -DNDEBUG $(CXXFLAGS) -o sub_string_finder_extended sub_string_finder_extended.cpp $(TBBLIB) $(LIBS)

debug: *.cpp
ifeq ($(offload),)
	$(CXX) -O0 -g -DTBB_USE_DEBUG $(CXXFLAGS) -o sub_string_finder sub_string_finder.cpp $(TBBLIB_DEBUG) $(LIBS)
	$(CXX) -O0 -g -DTBB_USE_DEBUG $(CXXFLAGS) -o sub_string_finder_pretty sub_string_finder_pretty.cpp  $(TBBLIB_DEBUG) $(LIBS)
endif
	$(CXX) -O0 -g -DTBB_USE_DEBUG $(CXXFLAGS) -o sub_string_finder_extended sub_string_finder_extended.cpp $(TBBLIB_DEBUG) $(LIBS)

clean:
	$(RM) sub_string_finder sub_string_finder_extended sub_string_finder_pretty *.o *.d

test:
	./$(PROG) $(ARGS)

light_test:
	./$(LIGHT_PROG) $(ARGS)
