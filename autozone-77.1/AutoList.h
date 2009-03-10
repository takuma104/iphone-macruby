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
#ifndef __AUTO_LIST__
#define __AUTO_LIST__


#include "AutoDefs.h"
#include "AutoHashTable.h"
#include "AutoRange.h"


namespace Auto {

    //----- List -----//

    //
    // Manage an unordered growable list of objects.  Using home grow version because
    // we need to control memory allocation (investigate STL.)
    //
    
    template <typename T> class List {

      private:
      
        enum {
            list_growth = 256                               // default growth rate
        };
      
        T        *_entries;                                 // array of entries
        usword_t _length;                                   // virtual length of list
        usword_t _maximum;                                  // actual length of list
        usword_t _growth;                                   // rate of growth
        
      public:
      
        //
        // Constructor
        //
        List(usword_t growth = list_growth) : _entries(NULL), _length(0), _maximum(0), _growth(growth) {}
        
        
        //
        // Destructor
        //
        ~List() { dispose(); }
        
        
        //
        // initialize
        //
        // Set up the list for use.
        //
        inline void initialize(usword_t growth = list_growth) {
            _entries = NULL;
            _length = 0;
            _maximum = 0;
            _growth = growth;
        }
        
        
        //
        // dispose
        //
        // Release memory used by List.
        //
        inline void dispose() {
            if (_entries) aux_free(_entries);
            _entries = NULL;
            _length = 0;
            _maximum = 0;
        }
        
        
        //
        // grab
        //
        // Grabs the entries for someone else to maintain.
        //
        inline T *grab() {
            T *entries = _entries;
            _entries = NULL;
            _length = 0;
            _maximum = 0;
            return entries;
        }
         
        
        //
        // allocate
        //
        // allocate a new slot in the list
        //
        inline T *allocate() {
            if (_length >= _maximum) {
                _maximum += _growth;
                _entries = (T *)aux_realloc(_entries, _maximum * sizeof(T));
            }
            
            return _entries + _length++;
        }
        
        
        //
        // add
        //
        // Adds a new range to the list.
        //
        inline T *add(T *entry) {
            T *slot = allocate();
            *slot = *entry;
            return slot;
        }
        inline T *add(T entry) {
            T *slot = allocate();
            *slot = entry;
            return slot;
        }


        //
        // set_growth
        //
        // Change the default growth rate.
        //
        inline void set_growth(usword_t growth) { _growth = growth; }
        
        
        //
        // memory
        //
        // Return address of memory used for entries (used by InUseEnumerator.)
        //
        inline void *memory() { return (void *)_entries; }
        
        
        //
        // length
        //
        // Actual number of entries in list.
        //
        inline usword_t length() const { return _length; }
        
        
        //
        // maximum
        //
        // Current maximum number of entries in list.
        //
        inline usword_t maximum() { return _maximum; }
        
        
        //
        // index
        //
        // Return the index of the specified entry.
        //
        inline size_t index(T *entry) const {
            size_t i = entry - _entries;
            ASSERTION(0 <= i && i < _length);
            return i;
        }
        
        
        //
        // find
        //
        // Locate an entry in the list.  Requires that entries define an operator==.
        //
        inline T* find(T entry) {
            for (size_t i = 0; i < _length; i++) {
                if (_entries[i] == entry) return _entries + i;
            }
        
            return NULL;
        }
      
        
        //
        // [] operator
        //
        // Simplify use of the list.
        //
        // An assert would be appropriate here but it slows things down considerably.
        //
        inline T& operator[](size_t i) { return _entries[i]; }
        
        
        //
        // remove
        //
        // Remove an entry from the list.
        //
        inline void remove(size_t i) { if (i != --_length) _entries[i] = _entries[_length]; }
        inline void remove(T *entry) { if (entry) remove(index(entry)); }
        
        
        //
        // is_empty
        //
        // Return true if the list contains no elements.
        //
        inline bool is_empty() const { return _length == 0; }
        
        
        //
        // push
        //
        // Push an element on to the list
        //
        inline void push(T *entry) { add(entry); }
        inline void push(T entry)  { add(entry); }
        

        //
        // pop
        //
        // Push an element on to the list
        //
        inline T pop() { return _entries[--_length]; }
        
        
    };


};


#endif // __AUTO_LIST__

