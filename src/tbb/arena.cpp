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

#include "arena.h"
#include "governor.h"
#include "scheduler.h"
#include "itt_notify.h"

#if !__TBB_CPU_CTL_ENV_PRESENT
inline void __TBB_get_cpu_ctl_env ( __TBB_cpu_ctl_env_t* ctl ) { fegetenv(ctl); }
inline void __TBB_set_cpu_ctl_env ( const __TBB_cpu_ctl_env_t* ctl ) { fesetenv(ctl); }
#endif /* !__TBB_CPU_CTL_ENV_PRESENT */

#include <functional>

#if __TBB_STATISTICS_STDOUT
#include <cstdio>
#endif

namespace tbb {
namespace internal {

void arena::process( generic_scheduler& s ) {
    __TBB_ASSERT( is_alive(my_guard), NULL );
    __TBB_ASSERT( governor::is_set(&s), NULL );
    __TBB_ASSERT( !s.my_innermost_running_task, NULL );
    __TBB_ASSERT( !s.my_dispatching_task, NULL );

    __TBB_ASSERT( my_num_slots != 1, NULL );
    // Start search for an empty slot from the one we occupied the last time
    unsigned index = s.my_arena_index < my_num_slots ? s.my_arena_index : s.my_random.get() % (my_num_slots - 1) + 1,
             end = index;
    __TBB_ASSERT( index != 0, "A worker cannot occupy slot 0" );
    __TBB_ASSERT( index < my_num_slots, NULL );

    // Find a vacant slot
    for ( ;; ) {
        if ( !my_slots[index].my_scheduler && __TBB_CompareAndSwapW( &my_slots[index].my_scheduler, (intptr_t)&s, 0 ) == 0 )
            break;
        if ( ++index == my_num_slots )
            index = 1;
        if ( index == end ) {
            // Likely this arena is already saturated
            goto quit;
        }
    }
    ITT_NOTIFY(sync_acquired, my_slots + index);
    s.my_arena = this;
    s.my_arena_index = index;
#if __TBB_TASK_PRIORITY
    s.my_local_reload_epoch = my_reload_epoch;
    __TBB_ASSERT( !s.my_offloaded_tasks, NULL );
#endif /* __TBB_TASK_PRIORITY */
    s.attach_mailbox( affinity_id(index+1) );

    my_slots[index].hint_for_push = index ^ unsigned(&s-(generic_scheduler*)NULL)>>16; // randomizer seed
    my_slots[index].hint_for_pop  = index; // initial value for round-robin

    __TBB_set_cpu_ctl_env(&my_cpu_ctl_env);

    atomic_update( my_limit, index + 1, std::less<unsigned>() );

    for ( ;; ) {
        // Try to steal a task.
        // Passing reference count is technically unnecessary in this context,
        // but omitting it here would add checks inside the function.
        __TBB_ASSERT( is_alive(my_guard), NULL );
        task* t = s.receive_or_steal_task( s.my_dummy_task->prefix().ref_count, /*return_if_no_work=*/true );
        if (t) {
            // A side effect of receive_or_steal_task is that my_innermost_running_task can be set.
            // But for the outermost dispatch loop of a worker it has to be NULL.
            s.my_innermost_running_task = NULL;
            __TBB_ASSERT( !s.my_dispatching_task, NULL );
            s.local_wait_for_all(*s.my_dummy_task,t);
        }
        __TBB_ASSERT ( __TBB_load_relaxed(my_slots[index].head) == __TBB_load_relaxed(my_slots[index].tail), "Worker cannot leave arena while its task pool is not empty" );
        __TBB_ASSERT( my_slots[index].task_pool == EmptyTaskPool, "Empty task pool is not marked appropriately" );
        // Revalidate quitting condition
        // This check prevents relinquishing more than necessary workers because 
        // of the non-atomicity of the decision making procedure
        if ( num_workers_active() >= my_num_workers_allotted || !my_num_workers_requested )
            break;
    }
#if __TBB_TASK_PRIORITY
    if ( s.my_offloaded_tasks ) {
        GATHER_STATISTIC( ++s.my_counters.prio_orphanings );
        ++my_abandonment_epoch;
        __TBB_ASSERT( s.my_offloaded_task_list_tail_link && !*s.my_offloaded_task_list_tail_link, NULL );
        task* orphans;
        do {
            orphans = const_cast<task*>(my_orphaned_tasks);
            *s.my_offloaded_task_list_tail_link = orphans;
        } while ( __TBB_CompareAndSwapW(&my_orphaned_tasks, (ptrdiff_t)s.my_offloaded_tasks, (ptrdiff_t)orphans) != (ptrdiff_t)orphans );
        s.my_offloaded_tasks = NULL;
#if TBB_USE_ASSERT
        s.my_offloaded_task_list_tail_link = NULL;
#endif /* TBB_USE_ASSERT */
    }
#endif /* __TBB_TASK_PRIORITY */
#if __TBB_STATISTICS
    ++s.my_counters.arena_roundtrips;
    *my_slots[index].my_counters += s.my_counters;
    s.my_counters.reset();
#endif /* __TBB_STATISTICS */
    __TBB_store_with_release( my_slots[index].my_scheduler, (generic_scheduler*)NULL );
    s.my_inbox.detach();
    __TBB_ASSERT( s.my_inbox.is_idle_state(true), NULL );
    __TBB_ASSERT( !s.my_innermost_running_task, NULL );
    __TBB_ASSERT( !s.my_dispatching_task, NULL );
    __TBB_ASSERT( is_alive(my_guard), NULL );
quit:
    // In contrast to earlier versions of TBB (before 3.0 U5) now it is possible
    // that arena may be temporarily left unpopulated by threads. See comments in
    // arena::on_thread_leaving() for more details.
#if !__TBB_TRACK_PRIORITY_LEVEL_SATURATION
    on_thread_leaving();
#endif /* !__TBB_TRACK_PRIORITY_LEVEL_SATURATION */
}

arena::arena ( market& m, unsigned max_num_workers ) {
    __TBB_ASSERT( !my_guard, "improperly allocated arena?" );
    __TBB_ASSERT( sizeof(my_slots[0]) % NFS_GetLineSize()==0, "arena::slot size not multiple of cache line size" );
    __TBB_ASSERT( (uintptr_t)this % NFS_GetLineSize()==0, "arena misaligned" );
#if __TBB_TASK_PRIORITY
    __TBB_ASSERT( !my_reload_epoch && !my_orphaned_tasks && !my_skipped_fifo_priority, "New arena object is not zeroed" );
#endif /* __TBB_TASK_PRIORITY */
    my_market = &m;
    my_limit = 1;
    // Two slots are mandatory: for the master, and for 1 worker (required to support starvation resistant tasks).
    my_num_slots = num_slots_to_reserve(max_num_workers);
    my_max_num_workers = max_num_workers;
    my_num_threads_active = 1; // accounts for the master
    __TBB_get_cpu_ctl_env(&my_cpu_ctl_env);
#if __TBB_TASK_PRIORITY
    my_bottom_priority = my_top_priority = normalized_normal_priority;
#endif /* __TBB_TASK_PRIORITY */
    my_aba_epoch = m.my_arenas_aba_epoch;
    __TBB_ASSERT ( my_max_num_workers < my_num_slots, NULL );
    // Construct mailboxes. Mark internal synchronization elements for the tools.
    for( unsigned i = 0; i < my_num_slots; ++i ) {
        __TBB_ASSERT( !my_slots[i].my_scheduler && !my_slots[i].task_pool, NULL );
        ITT_SYNC_CREATE(my_slots + i, SyncType_Scheduler, SyncObj_WorkerTaskPool);
        mailbox(i+1).construct();
        ITT_SYNC_CREATE(&mailbox(i+1), SyncType_Scheduler, SyncObj_Mailbox);
#if __TBB_STATISTICS
        my_slots[i].my_counters = new ( NFS_Allocate(sizeof(statistics_counters), 1, NULL) ) statistics_counters;
#endif /* __TBB_STATISTICS */
    }
#if __TBB_TASK_PRIORITY
    for ( intptr_t i = 0; i < num_priority_levels; ++i ) {
        my_task_stream[i].initialize(my_num_slots);
        ITT_SYNC_CREATE(my_task_stream + i, SyncType_Scheduler, SyncObj_TaskStream);
    }
#else /* !__TBB_TASK_PRIORITY */
    my_task_stream.initialize(my_num_slots);
    ITT_SYNC_CREATE(&my_task_stream, SyncType_Scheduler, SyncObj_TaskStream);
#endif /* !__TBB_TASK_PRIORITY */
    my_mandatory_concurrency = false;
#if __TBB_TASK_GROUP_CONTEXT
    my_master_default_ctx = NULL;
#endif
}

arena& arena::allocate_arena( market& m, unsigned max_num_workers ) {
    __TBB_ASSERT( sizeof(base_type) + sizeof(arena_slot) == sizeof(arena), "All arena data fields must go to arena_base" );
    __TBB_ASSERT( sizeof(base_type) % NFS_GetLineSize() == 0, "arena slots area misaligned: wrong padding" );
    __TBB_ASSERT( sizeof(mail_outbox) == NFS_MaxLineSize, "Mailbox padding is wrong" );
    size_t n = allocation_size(max_num_workers);
    unsigned char* storage = (unsigned char*)NFS_Allocate( n, 1, NULL );
    // Zero all slots to indicate that they are empty
    memset( storage, 0, n );
    return *new( storage + num_slots_to_reserve(max_num_workers) * sizeof(mail_outbox) ) arena(m, max_num_workers);
}

void arena::free_arena () {
    __TBB_ASSERT( is_alive(my_guard), NULL );
    __TBB_ASSERT( !my_num_threads_active, "There are threads in the dying arena" );
    __TBB_ASSERT( !my_num_workers_requested && !my_num_workers_allotted, "Dying arena requests workers" );
    __TBB_ASSERT( my_pool_state == SNAPSHOT_EMPTY || !my_max_num_workers, "Inconsistent state of a dying arena" );
#if !__TBB_STATISTICS_EARLY_DUMP
    GATHER_STATISTIC( dump_arena_statistics() );
#endif
    poison_value( my_guard );
    intptr_t drained = 0;
    for ( unsigned i = 1; i <= my_num_slots; ++i )
        drained += mailbox(i).drain();
#if __TBB_TASK_PRIORITY && TBB_USE_ASSERT
    for ( intptr_t i = 0; i < num_priority_levels; ++i )
        __TBB_ASSERT(my_task_stream[i].empty() && my_task_stream[i].drain()==0, "Not all enqueued tasks were executed");
#elif !__TBB_TASK_PRIORITY
    __TBB_ASSERT(my_task_stream.empty() && my_task_stream.drain()==0, "Not all enqueued tasks were executed");
#endif /* !__TBB_TASK_PRIORITY */
#if __TBB_COUNT_TASK_NODES
    my_market->update_task_node_count( -drained );
#endif /* __TBB_COUNT_TASK_NODES */
    my_market->release();
#if __TBB_TASK_GROUP_CONTEXT
    __TBB_ASSERT( my_master_default_ctx, "Master thread never entered the arena?" );
    my_master_default_ctx->~task_group_context();
    NFS_Free(my_master_default_ctx);
#endif /* __TBB_TASK_GROUP_CONTEXT */
#if __TBB_STATISTICS
    for( unsigned i = 0; i < my_num_slots; ++i )
        NFS_Free( my_slots[i].my_counters );
#endif /* __TBB_STATISTICS */
    void* storage  = &mailbox(my_num_slots);
    __TBB_ASSERT( my_num_threads_active == 0, NULL );
    __TBB_ASSERT( my_pool_state == SNAPSHOT_EMPTY || !my_max_num_workers, NULL );
    this->~arena();
#if TBB_USE_ASSERT > 1
    memset( storage, 0, allocation_size(my_max_num_workers) );
#endif /* TBB_USE_ASSERT */
    NFS_Free( storage );
}

#if __TBB_STATISTICS
void arena::dump_arena_statistics () {
    statistics_counters total;
    for( unsigned i = 0; i < my_num_slots; ++i ) {
#if __TBB_STATISTICS_EARLY_DUMP
        generic_scheduler* s = my_slots[i].my_scheduler;
        if ( s )
            *my_slots[i].my_counters += s->my_counters;
#else
        __TBB_ASSERT( !my_slots[i].my_scheduler, NULL );
#endif
        if ( i != 0 ) {
            total += *my_slots[i].my_counters;
            dump_statistics( *my_slots[i].my_counters, i );
        }
    }
    dump_statistics( *my_slots[0].my_counters, 0 );
#if __TBB_STATISTICS_STDOUT
#if !__TBB_STATISTICS_TOTALS_ONLY
    printf( "----------------------------------------------\n" );
#endif
    dump_statistics( total, workers_counters_total );
    total += *my_slots[0].my_counters;
    dump_statistics( total, arena_counters_total );
#if !__TBB_STATISTICS_TOTALS_ONLY
    printf( "==============================================\n" );
#endif
#endif /* __TBB_STATISTICS_STDOUT */
}
#endif /* __TBB_STATISTICS */

#if __TBB_TASK_PRIORITY
// TODO: This function seems deserving refactoring
inline bool arena::may_have_tasks ( generic_scheduler* s, arena_slot& slot, bool& tasks_present, bool& dequeuing_possible ) {
    suppress_unused_warning(slot);
    if ( !s ) {
        // This slot is vacant
        __TBB_ASSERT( slot.task_pool == EmptyTaskPool, NULL );
        __TBB_ASSERT( slot.tail == slot.head, "Someone is tinkering with a vacant arena slot" );
        return false;
    }
    dequeuing_possible |= s->worker_outermost_level();
    if ( s->my_pool_reshuffling_pending ) {
        // This primary task pool is nonempty and may contain tasks at the current
        // priority level. Its owner is winnowing lower priority tasks at the moment.
        tasks_present = true;
        return true;
    }
    if ( s->my_offloaded_tasks ) {
        tasks_present = true;
        if ( s->my_local_reload_epoch < *s->my_ref_reload_epoch ) {
            // This scheduler's offload area is nonempty and may contain tasks at the
            // current priority level.
            return true;
        }
    }
    return false;
}
#endif /* __TBB_TASK_PRIORITY */

bool arena::is_out_of_work() {
    // TODO: rework it to return at least a hint about where a task was found; better if the task itself.
    for(;;) {
        pool_state_t snapshot = my_pool_state;
        switch( snapshot ) {
            case SNAPSHOT_EMPTY:
                return true;
            case SNAPSHOT_FULL: {
                // Use unique id for "busy" in order to avoid ABA problems.
                const pool_state_t busy = pool_state_t(&busy);
                // Request permission to take snapshot
                if( my_pool_state.compare_and_swap( busy, SNAPSHOT_FULL )==SNAPSHOT_FULL ) {
                    // Got permission. Take the snapshot.
                    // NOTE: This is not a lock, as the state can be set to FULL at 
                    //       any moment by a thread that spawns/enqueues new task.
                    size_t n = my_limit;
                    // Make local copies of volatile parameters. Their change during
                    // snapshot taking procedure invalidates the attempt, and returns
                    // this thread into the dispatch loop.
#if __TBB_TASK_PRIORITY
                    intptr_t top_priority = my_top_priority;
                    uintptr_t reload_epoch = my_reload_epoch;
                    // Inspect primary task pools first
#endif /* __TBB_TASK_PRIORITY */
                    size_t k; 
                    for( k=0; k<n; ++k ) {
                        if( my_slots[k].task_pool != EmptyTaskPool &&
                            __TBB_load_relaxed(my_slots[k].head) < __TBB_load_relaxed(my_slots[k].tail) )
                        {
                            // k-th primary task pool is nonempty and does contain tasks.
                            break;
                        }
                    }
                    __TBB_ASSERT( k <= n, NULL );
                    bool work_absent = k == n;
#if __TBB_TASK_PRIORITY
                    // Variable tasks_present indicates presence of tasks at any priority
                    // level, while work_absent refers only to the current priority.
                    bool tasks_present = !work_absent || my_orphaned_tasks;
                    bool dequeuing_possible = false;
                    if ( work_absent ) {
                        // Check for the possibility that recent priority changes
                        // brought some tasks to the current priority level

                        uintptr_t abandonment_epoch = my_abandonment_epoch;
                        // Master thread's scheduler needs special handling as it 
                        // may be destroyed at any moment (workers' schedulers are 
                        // guaranteed to be alive while at least one thread is in arena).
                        // Have to exclude concurrency with task group state change propagation too.
                        my_market->my_arenas_list_mutex.lock();
                        generic_scheduler *s = my_slots[0].my_scheduler;
                        if ( s && __TBB_CompareAndSwapW(&my_slots[0].my_scheduler, (intptr_t)LockedMaster, (intptr_t)s) == (intptr_t)s ) {
                            __TBB_ASSERT( my_slots[0].my_scheduler == LockedMaster && s != LockedMaster, NULL );
                            work_absent = !may_have_tasks( s, my_slots[0], tasks_present, dequeuing_possible );
                            __TBB_store_with_release( my_slots[0].my_scheduler, s );
                        }
                        my_market->my_arenas_list_mutex.unlock();
                        // The following loop is subject to data races. While k-th slot's
                        // scheduler is being examined, corresponding worker can either
                        // leave to RML or migrate to another arena.
                        // But the races are not prevented because all of them are benign.
                        // First, the code relies on the fact that worker thread's scheduler
                        // object persists until the whole library is deinitialized.  
                        // Second, in the worst case the races can only cause another 
                        // round of stealing attempts to be undertaken. Introducing complex 
                        // synchronization into this coldest part of the scheduler's control 
                        // flow does not seem to make sense because it both is unlikely to 
                        // ever have any observable performance effect, and will require 
                        // additional synchronization code on the hotter paths.
                        for( k = 1; work_absent && k < n; ++k )
                            work_absent = !may_have_tasks( my_slots[k].my_scheduler, my_slots[k], tasks_present, dequeuing_possible );
                        // Preclude premature switching arena off because of a race in the previous loop.
                        work_absent = work_absent
                                      && !__TBB_load_with_acquire(my_orphaned_tasks)
                                      && abandonment_epoch == my_abandonment_epoch;
                    }
#endif /* __TBB_TASK_PRIORITY */
                    // Test and test-and-set.
                    if( my_pool_state==busy ) {
#if __TBB_TASK_PRIORITY
                        bool no_fifo_tasks = my_task_stream[top_priority].empty();
                        work_absent = work_absent && (!dequeuing_possible || no_fifo_tasks)
                                      && top_priority == my_top_priority && reload_epoch == my_reload_epoch;
#else
                        bool no_fifo_tasks = my_task_stream.empty();
                        work_absent = work_absent && no_fifo_tasks;
#endif /* __TBB_TASK_PRIORITY */
                        if( work_absent ) {
#if __TBB_TASK_PRIORITY
                            if ( top_priority > my_bottom_priority ) {
                                if ( my_market->lower_arena_priority(*this, top_priority - 1, top_priority)
                                     && !my_task_stream[top_priority].empty() )
                                {
                                    atomic_update( my_skipped_fifo_priority, top_priority, std::less<intptr_t>());
                                }
                            }
                            else if ( !tasks_present && !my_orphaned_tasks && no_fifo_tasks ) {
#endif /* __TBB_TASK_PRIORITY */
                                // save current demand value before setting SNAPSHOT_EMPTY,
                                // to avoid race with advertise_new_work.
                                int current_demand = (int)my_max_num_workers;
                                if( my_pool_state.compare_and_swap( SNAPSHOT_EMPTY, busy )==busy ) {
                                    // This thread transitioned pool to empty state, and thus is 
                                    // responsible for telling RML that there is no other work to do.
                                    my_market->adjust_demand( *this, -current_demand );
#if __TBB_TASK_PRIORITY
                                    // Check for the presence of enqueued tasks "lost" on some of
                                    // priority levels because updating arena priority and switching
                                    // arena into "populated" (FULL) state happen non-atomically.
                                    // Imposing atomicity would require task::enqueue() to use a lock,
                                    // which is unacceptable. 
                                    bool switch_back = false;
                                    for ( int p = 0; p < num_priority_levels; ++p ) {
                                        if ( !my_task_stream[p].empty() ) {
                                            switch_back = true;
                                            if ( p < my_bottom_priority || p > my_top_priority )
                                                my_market->update_arena_priority(*this, p);
                                        }
                                    }
                                    if ( switch_back )
                                        advertise_new_work</*Spawned*/false>();
#endif /* __TBB_TASK_PRIORITY */
                                    return true;
                                }
                                return false;
#if __TBB_TASK_PRIORITY
                            }
#endif /* __TBB_TASK_PRIORITY */
                        }
                        // Undo previous transition SNAPSHOT_FULL-->busy, unless another thread undid it.
                        my_pool_state.compare_and_swap( SNAPSHOT_FULL, busy );
                    }
                } 
                return false;
            }
            default:
                // Another thread is taking a snapshot.
                return false;
        }
    }
}

#if __TBB_COUNT_TASK_NODES 
intptr_t arena::workers_task_node_count() {
    intptr_t result = 0;
    for( unsigned i = 1; i < my_num_slots; ++i ) {
        generic_scheduler* s = my_slots[i].my_scheduler;
        if( s )
            result += s->my_task_node_count;
    }
    return result;
}
#endif /* __TBB_COUNT_TASK_NODES */

} // namespace internal
} // namespace tbb
