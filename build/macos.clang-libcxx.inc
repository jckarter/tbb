include $(tbb_root)/build/macos.clang.inc

LIBS += -stdlib=libc++
CPLUS_FLAGS += -stdlib=libc++

CPP11_FLAGS = -std=c++11 -D_TBB_CPP0X

ifneq (00,$(lambdas)$(cpp0x))
    CXX_ONLY_FLAGS += $(CPP11_FLAGS)
endif

