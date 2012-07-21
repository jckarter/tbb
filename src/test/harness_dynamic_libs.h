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

#if _WIN32 || _WIN64
#include "tbb/machine/windows_api.h"
#else
#include <dlfcn.h>
#endif

namespace Harness {

#if _WIN32 || _WIN64
typedef  HMODULE LIBRARY_HANDLE;
#else
typedef void *LIBRARY_HANDLE;
#endif

#if _WIN32 || _WIN64
#define TEST_LIBRARY_NAME(base) base".dll"
#elif __APPLE__
#define TEST_LIBRARY_NAME(base) base".dylib"
#else
#define TEST_LIBRARY_NAME(base) base".so"
#endif

LIBRARY_HANDLE OpenLibrary(const char *name)
{
#if _WIN32 || _WIN64
    return ::LoadLibrary(name);
#else
    return dlopen(name, RTLD_NOW|RTLD_GLOBAL);
#endif
}

void CloseLibrary(LIBRARY_HANDLE lib)
{
#if _WIN32 || _WIN64
    BOOL ret = FreeLibrary(lib);
    ASSERT(ret, "FreeLibrary must be successful");
#else
    int ret = dlclose(lib);
    ASSERT(ret == 0, "dlclose must be successful");
#endif
}

typedef void (*FunctionAddress)();

FunctionAddress GetAddress(Harness::LIBRARY_HANDLE lib, const char *name)
{
    union { FunctionAddress func; void *symb; } converter;
#if _WIN32 || _WIN64
    converter.symb = (void*)GetProcAddress(lib, name);
#else
    converter.symb = (void*)dlsym(lib, name);
#endif
    ASSERT(converter.func, "Can't find required symbol in dynamic library");
    return converter.func;
}

}  // namespace Harness
