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

#include "AutoLock.h"

namespace Auto {

    // ----- Lock -----//

    //
    // lock
    //
    // Acquire the lock.
    //
    void Lock::lock() {
#if 0 // rely on auto_zone locks for now
        // get the current thread
        pthread_t new_thread = pthread_self();
        // existing thread in lock
        pthread_t old_thread;
        
        do {
            // atomically swap new_thread with existing NULL
            old_thread = (pthread_t)compare_and_exchange((intptr_t *)&_thread, (intptr_t)NULL, (intptr_t)new_thread);
            // spin until we own the lock
        } while (old_thread && old_thread != new_thread);
        
        // increment nesting
         _nesting++;
#endif
    }
    
    
    //
    // unlock
    //
    // Release the lock.
    //
    void Lock::unlock() {
#if 0 // rely on auto_zone locks for now
        // get the current thread
        pthread_t new_thread = pthread_self();
        
        ASSERTION(_nesting > 0);
        if (--_nesting == 0) {
            // atomically swap NULL with existing new_thread (probably overkill but provides process notification)
            pthread_t old_thread = (pthread_t)compare_and_exchange((intptr_t *)&_thread, (intptr_t)new_thread, (intptr_t)NULL);
            ASSERTION(old_thread == new_thread);
        }
#endif
    }



    //----- Synchronize -----//

    Lock Synchronize::_global_lock;
    
    
};


