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
#ifndef __AUTO_HASHLIST__
#define __AUTO_HASHLIST__


#include "AutoDefs.h"
#include "AutoHashTable.h"

#include <new>

namespace Auto {


    //----- HashList -----//

    //
    // Manage an unordered growable list of Locations searched via a hashtable.
    //
    
    template <class T> class HashList : public HashTable {

      private:

        enum {
            hash_list_growth = 256                          // default growth rate
        };

        T         *_entries;                                // array of entries
        usword_t  _length;                                  // virtual length of list
        usword_t  _maximum;                                 // actual length of list
        usword_t  _growth;                                  // rate of growth
        
      public:
      
        //
        // Constructors
        //
        HashList(usword_t growth = hash_list_growth) : HashTable(), _entries(NULL), _length(0), _maximum(0), _growth(growth) {}
        
        HashList(HashList &original) {
            _length = original.length;
            _maximum = original.maximum;
            _growth = original.growth;
            _entries = aux_malloc(_length * sizeof(T));
            memmove(_entries, original.entries, length*sizeof(T));
        }
        
        //
        // Destructor
        //
        ~HashList() { dispose(); }
        
        
        //
        // initialize
        //
        // Set up the list for use.
        //
        inline void initialize(usword_t growth = hash_list_growth) {
            HashTable::initialize();
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
            HashTable::dispose();
            if (_entries) {
                for (usword_t i = 0; i < _length; ++i)
                    _entries[i].~T();
                aux_free(_entries);
            }
            _entries = NULL;
            _length = 0;
            _maximum = 0;
        }
        
        
        //
        // set_growth
        //
        // Change the default growth rate.
        //
        inline void set_growth(const usword_t growth) { _growth = growth; }
        
        
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
        inline usword_t length() { return _length; }
        
        
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
        inline const size_t index(T *entry) const {
            const size_t i = entry - _entries;
            ASSERTION(0 <= i && i < _length);
            return i;
        }
        
        
        //
        // find
        //
        // Locate an entry in the list via the hash table.
        //
        inline T *find(void *address) { return (T *)HashTable::find(address); }
      
        
        //
        // [] operator
        //
        // Simplify use of the list.
        //
        inline T& operator[](const size_t i) {
            ASSERTION(0 <= i && i < _length);
            return _entries[i];
        }
        
        
        //
        // add
        //
        // Adds a new range to the list.
        //
        inline T *add() {
            // if need more room
            if (_length >= _maximum) {
                _maximum += _growth;
                _entries = (T *)aux_realloc(_entries, _maximum * sizeof(T));
                
                // have to rehash the entries
                HashTable::clear();
                for (usword_t i = 0; i < _length; i++) HashTable::add(_entries + i);
            }
            
            return ::new(_entries + _length++) T();
        }
        inline T *add(T *entry) {
            T *slot = add();
            *slot = *entry;
            HashTable::add(slot);
            return slot;
        }
        inline T *add(T entry) {
            T *slot = add();
            *slot = entry;
            HashTable::add(slot);
            return slot;
        }
        inline T *addRange(Range entry) {
            T *slot = add();
            *(Range*)slot = entry;
            HashTable::add(slot);
            return slot;
        }
        
        //
        // remove
        //
        // Remove an entry from the list.
        //
        inline void remove(size_t i) {
            HashTable::remove(_entries + i);
            if (i != --_length) {
                HashTable::remove(_entries + _length);
                _entries[i] = _entries[_length];
                HashTable::add(_entries + i);
            }
        }
        inline void remove(T *entry) { if (entry) remove(index(entry)); }
        
        
        //
        // is_empty
        //
        // Return true if the list contains no elements.
        //
        inline bool is_empty() const { return _length == 0; }
        
        
    };
    
    
};

#endif // __AUTO_HASHLIST__

