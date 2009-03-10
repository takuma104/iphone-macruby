/*
 * Copyright (c) 2004-2008 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */

#pragma once
#ifndef __AUTO_LOCK__
#define __AUTO_LOCK__


#include <assert.h>

#include "AutoDefs.h"
#include "auto_impl_utilities.h"

namespace Auto {

    //----- SpinLock -----//
    
    // Scoped uses of spin_lock() / spin_unlock().

    class SpinLock {
        spin_lock_t *_lock;
    public:
        SpinLock(spin_lock_t *lock) : _lock(lock)  { spin_lock(_lock); }
        ~SpinLock()                         { spin_unlock(_lock); }
    };
    
    // Scoped conditional uses of spin_lock() / spin_unlock().

    class ConditionBarrier {
        spin_lock_t *_lock;
    public:
        ConditionBarrier(bool volatile *condition, spin_lock_t *lock) : _lock(NULL) {
            if (*condition) {
                spin_lock(lock);
                if (!*condition) {
                    spin_unlock(lock);
                } else {
                    _lock = lock;
                }
            }
        }
        operator int() { return _lock != NULL; }
        ~ConditionBarrier() { if (_lock) spin_unlock(_lock); }
    };

    class UnconditionalBarrier {
        bool volatile *_condition;
        spin_lock_t *_lock;
    public:
        UnconditionalBarrier(bool volatile *condition, spin_lock_t *lock) : _condition(condition), _lock(lock) {
            spin_lock(lock);
        }
        operator int() { return (*_condition != false); }
        ~UnconditionalBarrier() { spin_unlock(_lock); }
    };
    
    // Scoped uses of pthread_mutex_t.
    
    class Mutex {
        pthread_mutex_t *_mutex;
    public:
        Mutex(pthread_mutex_t *mutex) : _mutex(mutex) { if (_mutex) pthread_mutex_lock(_mutex); }
        ~Mutex() { if (_mutex) pthread_mutex_unlock(_mutex); }
    };
};

#endif // __AUTO_LOCK__
