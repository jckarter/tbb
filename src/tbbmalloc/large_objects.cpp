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

#include "tbbmalloc_internal.h"

/********* Allocation of large objects ************/


namespace rml {
namespace internal {

static struct LargeBlockCacheStat {
    uintptr_t age;
} loCacheStat;

bool LargeObjectCache::CacheBin::put(ExtMemoryPool *extMemPool,
                                     LargeMemoryBlock *ptr)
{
    bool blockCached = true;
    ptr->prev = NULL;
    ptr->age  = extMemPool->loc.cleanupCacheIfNeed(extMemPool);

    {
        MallocMutex::scoped_lock scoped_cs(lock);
        if (lastCleanedAge) {
            ptr->next = first;
            first = ptr;
            if (ptr->next) ptr->next->prev = ptr;
            if (!last) {
                MALLOC_ASSERT(0 == oldest, ASSERT_TEXT);
                oldest = ptr->age;
                last = ptr;
            }
        } else {
            // 1st object of such size was released.
            // Not cache it, and remeber when this occurs
            // to take into account during cache miss.
            lastCleanedAge = ptr->age;
            blockCached = false;
        }
    }
    return blockCached;
}

LargeMemoryBlock *LargeObjectCache::CacheBin::get(ExtMemoryPool *extMemPool,
                                                  size_t size)
{
    uintptr_t currAge = extMemPool->loc.cleanupCacheIfNeed(extMemPool);
    LargeMemoryBlock *result=NULL;
    {
        MallocMutex::scoped_lock scoped_cs(lock);
        if (first) {
            result = first;
            first = result->next;
            if (first)
                first->prev = NULL;
            else {
                last = NULL;
                oldest = 0;
            }
        } else {
            /* If cache miss occured, set ageThreshold to twice the difference
               between current time and last time cache was cleaned. */
            ageThreshold = 2*(currAge - lastCleanedAge);
        }
    }
    return result;
}

bool LargeObjectCache::CacheBin::cleanToThreshold(ExtMemoryPool *extMemPool,
                                                  uintptr_t currAge)
{
    LargeMemoryBlock *toRelease = NULL;
    bool released = false;

    /* oldest may be more recent then age, that's why cast to signed type
       was used. age overflow is also processed correctly. */
    if (last && (intptr_t)(currAge - oldest) > ageThreshold) {
        MallocMutex::scoped_lock scoped_cs(lock);
        // double check
        if (last && (intptr_t)(currAge - last->age) > ageThreshold) {
            do {
                last = last->prev;
            } while (last && (intptr_t)(currAge - last->age) > ageThreshold);
            if (last) {
                toRelease = last->next;
                oldest = last->age;
                last->next = NULL;
            } else {
                toRelease = first;
                first = NULL;
                oldest = 0;
            }
            MALLOC_ASSERT( toRelease, ASSERT_TEXT );
            lastCleanedAge = toRelease->age;
        }
        else
            return false;
    }
    released = toRelease;

    while ( toRelease ) {
        LargeMemoryBlock *helper = toRelease->next;
        removeBackRef(toRelease->backRefIdx);
        extMemPool->backend.putLargeBlock(toRelease);
        toRelease = helper;
    }
    return released;
}

bool LargeObjectCache::CacheBin::cleanAll(ExtMemoryPool *extMemPool)
{
    LargeMemoryBlock *toRelease = NULL;
    bool released = false;

    if (last) {
        MallocMutex::scoped_lock scoped_cs(lock);
        // double check
        if (last) {
            toRelease = first;
            last = NULL;
            first = NULL;
            oldest = 0;
        }
        else
            return false;
    }
    released = toRelease;

    while ( toRelease ) {
        LargeMemoryBlock *helper = toRelease->next;
        removeBackRef(toRelease->backRefIdx);
        extMemPool->backend.putLargeBlock(toRelease);
        toRelease = helper;
    }
    return released;
}

// release from cache blocks that are older than ageThreshold
bool LargeObjectCache::regularCleanup(ExtMemoryPool *extMemPool, uintptr_t currAge)
{
    bool released = false;

    for (int i = numLargeBlockBins-1; i >= 0; i--)
        if (bin[i].cleanToThreshold(extMemPool, currAge))
            released = true;
    return released;
}

bool ExtMemoryPool::softCachesCleanup()
{
    // TODO: cleanup small objects as well
    return loc.regularCleanup(this, FencedLoad((intptr_t&)loCacheStat.age));
}

uintptr_t LargeObjectCache::cleanupCacheIfNeed(ExtMemoryPool *extMemPool)
{
    /* loCacheStat.age overflow is OK, as we only want difference between
     * its current value and some recent.
     *
     * Both malloc and free should increment loCacheStat.age, as in
     * a different case multiple cached blocks would have same age,
     * and accuracy of predictors suffers.
     */
    uintptr_t currAge = (uintptr_t)AtomicIncrement((intptr_t&)loCacheStat.age);

    if ( 0 == currAge % cacheCleanupFreq )
        regularCleanup(extMemPool, currAge);

    return currAge;
}

LargeMemoryBlock *LargeObjectCache::get(ExtMemoryPool *extMemPool, size_t size)
{
    MALLOC_ASSERT( size%largeBlockCacheStep==0, ASSERT_TEXT );
    LargeMemoryBlock *lmb = NULL;
    size_t idx = sizeToIdx(size);
    if (idx<numLargeBlockBins) {
        lmb = bin[idx].get(extMemPool, size);
        if (lmb) {
            MALLOC_ITT_SYNC_ACQUIRED(bin+idx);
            STAT_increment(getThreadId(), ThreadCommonCounters, allocCachedLargeBlk);
        }
    }
    return lmb;
}

void *ExtMemoryPool::mallocLargeObject(size_t size, size_t alignment)
{
    size_t headersSize = sizeof(LargeMemoryBlock)+sizeof(LargeObjectHdr);
    // TODO: take into account that they are already largeObjectAlignment-aligned
    size_t allocationSize = alignUp(size+headersSize+alignment, largeBlockCacheStep);

    if (allocationSize < size) // allocationSize is wrapped around after alignUp
        return NULL;

    LargeMemoryBlock* lmb = loc.get(this, allocationSize);
    if (!lmb) {
        BackRefIdx backRefIdx = BackRefIdx::newBackRef(/*largeObj=*/true);
        if (backRefIdx.isInvalid())
            return NULL;

        // unalignedSize is set in getLargeBlock
        lmb = backend.getLargeBlock(allocationSize);
        if (!lmb) {
            removeBackRef(backRefIdx);
            return NULL;
        }
        lmb->backRefIdx = backRefIdx;
        STAT_increment(getThreadId(), ThreadCommonCounters, allocNewLargeObj);
    }

    void *alignedArea = (void*)alignUp((uintptr_t)lmb+headersSize, alignment);
    LargeObjectHdr *header = (LargeObjectHdr*)alignedArea-1;
    header->memoryBlock = lmb;
    header->backRefIdx = lmb->backRefIdx;
    setBackRef(header->backRefIdx, header);

    lmb->objectSize = size;

    MALLOC_ASSERT( isLargeObject(alignedArea), ASSERT_TEXT );
    return alignedArea;
}

bool LargeObjectCache::put(ExtMemoryPool *extMemPool, LargeMemoryBlock *largeBlock)
{
    size_t idx = sizeToIdx(largeBlock->unalignedSize);
    if (idx<numLargeBlockBins) {
        MALLOC_ITT_SYNC_RELEASING(bin+idx);
        if (bin[idx].put(extMemPool, largeBlock)) {
            STAT_increment(getThreadId(), ThreadCommonCounters, cacheLargeBlk);
            return true;
        } else
            return false;
    }
    return false;
}

void ExtMemoryPool::freeLargeObject(void *object)
{
    LargeObjectHdr *header = (LargeObjectHdr*)object - 1;

    // overwrite backRefIdx to simplify double free detection
    header->backRefIdx = BackRefIdx();
    if (!loc.put(this, header->memoryBlock)) {
        removeBackRef(header->memoryBlock->backRefIdx);
        backend.putLargeBlock(header->memoryBlock);
        STAT_increment(getThreadId(), ThreadCommonCounters, freeLargeObj);
    }
}

/*********** End allocation of large objects **********/

} // namespace internal
} // namespace rml

