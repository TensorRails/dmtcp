/****************************************************************************
 *   Copyright (C) 2006-2013 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include "alloc.h"
#include "pluginmanager.h"
#include "threadinfo.h"
#include "wrapperlock.h"

using namespace dmtcp;

EXTERNC int
dmtcp_alloc_enabled()
{
  static const int enabled =
    internalPluginEnabled(INTERNAL_PLUGIN_ALLOC) ? 1 : 0;
  return enabled;
}

// Deadlock happens when one thread has called pthread_create, and a
// second thread is exiting, and then a checkpoint request arrives.
// Note that glibc contains a cache of stacks so that when it creates
// a new thread, it can reuse an old stack instead of doing an allocation.
// glibc maintains a lock for the cached stacks GL(dl_stack_cache_lock).
// DMTCP maintains a wrapper lock.
// 
// Next, the glibc cache of stacks is limited to a certain maximum.
// When a thread is exiting and the cache of stacks is already at the
// limit, glibc will evict the oldest stack in the cache, and places
// the stack of the exiting thread into the cache.
// 
// So, the thread being created holds read access to the DMTCP wrapper
// lock, and wants glibc's cache-of-stacks lock in order to complete
// the real pthread_create.  The exiting thread holds glibc's
// cache-of-stacks lock, but wants read access to the DMTCP wrapper
// lock in order to evict a stack from the cache-of-stacks.  The exiting
// thread is waiting on the checkpoint thread because the DMTCP wrapper
// lock is writer-prefered, and the checkpoint thread wants write
// access to the DMTCP wrapper lock. The checkpoint thread can't gain
// write access to the DMTCP wrapper lock because the thread being
// created holds read access to the DMTCP wrapper lock.
// 
// This happens often when using OpenMP (libgomp), which rapidly creates
// and destroys threads.
//
// If isExistingThread() returns true, we can skip acquiring read access
// to the DMTCP wrapper lock in DMTCP's memory allocation wrappers.
static bool
isExitingThread()
{
  // Read curThread directly: this check runs on every allocation, and
  // dmtcp_get_current_thread() may create the thread list.  A thread with no
  // Thread yet is not exiting.
  Thread *thread = curThread;
  return thread != nullptr && thread->exiting;
}

// The exiting thread must also not resolve the real function.  NEXT_FNC()
// resolves on first use through dmtcp_dlsym(), which takes read access to the
// DMTCP wrapper lock (and glibc's dl_load_lock in dladdr1()).  The real free()
// used to be looked up from a separate NEXT_FNC() call site in the exiting
// branch below, so the first exiting thread that evicted a cached stack did
// that lookup while holding the cache-of-stacks lock, and the deadlock above
// came back through dmtcp_dlsym().
//
// So each wrapper's function has exactly one NEXT_FNC() call site, in the
// accessors below, shared by the locked and the exiting branches, and
// resolveRealAllocFns() resolves all of them while libdmtcp.so is loaded:
// before the program can create, let alone end, a thread.  The lookup stays
// lazy too, because allocations happen before that constructor runs.
static inline __typeof__(&malloc) realMalloc() { return _real_malloc; }
static inline __typeof__(&calloc) realCalloc() { return _real_calloc; }
static inline __typeof__(&valloc) realValloc() { return _real_valloc; }
static inline __typeof__(&realloc) realRealloc() { return _real_realloc; }
static inline __typeof__(&free) realFree() { return _real_free; }
static inline __typeof__(&memalign) realMemalign() { return _real_memalign; }
static inline __typeof__(&posix_memalign) realPosixMemalign()
{
  return _real_posix_memalign;
}

static void __attribute__((constructor))
resolveRealAllocFns()
{
  realMalloc();
  realCalloc();
  realValloc();
  realRealloc();
  realFree();
  realMemalign();
  realPosixMemalign();
}

extern "C" void *calloc(size_t nmemb, size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return realCalloc()(nmemb, size);
  }

  WrapperLock wrapperLock;
  return realCalloc()(nmemb, size);
}

extern "C" void *malloc(size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return realMalloc()(size);
  }

  WrapperLock wrapperLock;
  return realMalloc()(size);
}

extern "C" void *memalign(size_t boundary, size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return realMemalign()(boundary, size);
  }

  WrapperLock wrapperLock;
  return realMemalign()(boundary, size);
}

extern "C" int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return realPosixMemalign()(memptr, alignment, size);
  }

  WrapperLock wrapperLock;
  return realPosixMemalign()(memptr, alignment, size);
}

extern "C" void *valloc(size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return realValloc()(size);
  }

  WrapperLock wrapperLock;
  return realValloc()(size);
}

extern "C" void
free(void *ptr)
{
  // Add for performance.
  if (ptr == NULL) {
    return;
  }
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    realFree()(ptr);
    return;
  }

  WrapperLock wrapperLock;
  realFree()(ptr);
}

extern "C" void *realloc(void *ptr, size_t size)
{
  if (!dmtcp_alloc_enabled() || isExitingThread()) {
    return realRealloc()(ptr, size);
  }

  WrapperLock wrapperLock;
  return realRealloc()(ptr, size);
}
