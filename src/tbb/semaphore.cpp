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

#include "semaphore.h"
#if _WIN32||_WIN64
#if defined(RTL_SRWLOCK_INIT)
#include "dynamic_link.h" // Refers to src/tbb, not include/tbb
#include "tbb_misc.h"
#endif
#endif

namespace tbb {
namespace internal {

#if _WIN32||_WIN64
#if defined(RTL_SRWLOCK_INIT)

static atomic<do_once_state> concmon_module_inited;

void WINAPI init_binsem_using_event( SRWLOCK* h_ )
{
    srwl_or_handle* shptr = (srwl_or_handle*) h_;
    shptr->h = CreateEvent( NULL, FALSE/*manual reset*/, FALSE/*not signalled initially*/, NULL);
}

void WINAPI acquire_binsem_using_event( SRWLOCK* h_ )
{
    srwl_or_handle* shptr = (srwl_or_handle*) h_;
    WaitForSingleObject( shptr->h, INFINITE );
}

void WINAPI release_binsem_using_event( SRWLOCK* h_ )
{
    srwl_or_handle* shptr = (srwl_or_handle*) h_;
    SetEvent( shptr->h );
}

static void (WINAPI *__TBB_init_binsem)( SRWLOCK* ) = (void (WINAPI *)(SRWLOCK*))&init_binsem_using_event;
static void (WINAPI *__TBB_acquire_binsem)( SRWLOCK* ) = (void (WINAPI *)(SRWLOCK*))&acquire_binsem_using_event;
static void (WINAPI *__TBB_release_binsem)( SRWLOCK* ) = (void (WINAPI *)(SRWLOCK*))&release_binsem_using_event;

//! Table describing the how to link the handlers.
static const dynamic_link_descriptor SRWLLinkTable[] = {
    DLD(InitializeSRWLock,       __TBB_init_binsem),
    DLD(AcquireSRWLockExclusive, __TBB_acquire_binsem),
    DLD(ReleaseRWLockExclusive,  __TBB_release_binsem)
};

inline void init_concmon_module()
{
    __TBB_ASSERT( (uintptr_t)__TBB_init_binsem==(uintptr_t)&init_binsem_using_event, NULL );
    dynamic_link( "Kernel32.dll", SRWLLinkTable, 3 );
}

binary_semaphore::binary_semaphore() {
    atomic_do_once( &init_concmon_module, concmon_module_inited );

    __TBB_init_binsem( &my_sem.lock ); 
    if( (uintptr_t)__TBB_init_binsem!=(uintptr_t)&init_binsem_using_event )
        P();
}

binary_semaphore::~binary_semaphore() {
    if( (uintptr_t)__TBB_init_binsem==(uintptr_t)&init_binsem_using_event )
        CloseHandle( my_sem.h );
}

void binary_semaphore::P() { __TBB_acquire_binsem( &my_sem.lock ); }

void binary_semaphore::V() { __TBB_release_binsem( &my_sem.lock ); }

#endif /* defined(RTL_SRWLOCK_INIT) */
#endif /* _WIN32||_WIN64 */

} // namespace internal
} // namespace tbb
