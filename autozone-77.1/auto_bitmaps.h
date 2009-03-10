/*
 * Copyright (c) 2002-2008 Apple Inc. All rights reserved.
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

#import <architecture/byte_order.h>
#import <libc.h>

#include <assert.h>

/*********  32-bit-at-a-time bitmaps ***************/

//
// Bitmaps (bit vectors) are a fast and lightweight means of showing set
// representation.  A single bit is used to indicate membership in the set; 1
// indicating membership and 0 indicating exclusion.  In this representation we use
// an array of unsigned 32-bit words.  Each word represents a unit of 32 members
// numbered from the least significant bit (endian representation is not
// important.)  Thus for any member k its word index would be equal to (k / 32) or
// (k >> ilog2(32)) or (k >> 5) and its bit position would be equal to (k % 32) or
// (k & 31).
//

typedef unsigned *auto_bitmap_t;                    // bitmap data type


/*********  Constants   ************/


#define UNLIMITED       (~0u)                       // maximum index/size into/of bitmap
#define NOT_FOUND       (~0u)                       // value not found in bitmap

#define BITSPERWORD      32                         // number of bits in word (8 * sizeof(unsigned))
#define BITSPERWORDLOG2  5                          // ilog2(BITSPERWORD)
#define BITSPERWORDMASK  MASK(BITSPERWORDLOG2)      // mask of BITSPERWORDLOG2 bits or 31


/*********  Bitmap Manipulation  ************/

//
// Return the number of one bits at the least significant end of the word
// it is assumed that bits are contiguous and there are no other one bits
// in the word.
//
// Note: Optimization on the powerpc will generate two instructions for this routine.
// cntlzw and subfi (i.e., it's fast)  On Intel newer processors have a popcnt instruction
// that could accomplish the same thing.
//
static inline unsigned bitmap_count(unsigned word) { return ilog2(word) + 1; }

//
// Return the value of the bit at position 'bit'.
//
static inline unsigned bitmap_bit(const auto_bitmap_t bitmap, const unsigned bit) {
    unsigned    index = bit >> BITSPERWORDLOG2;     // index of 32 bit word in bitmap holding bit
    unsigned    shift = bit & BITSPERWORDMASK;      // shift to extract bit
    return (bitmap[index] >> shift) & 1;            // extract one bit
}

//
// Set the value of the bit at position 'bit' to 1.
//
static inline void bitmap_set(const auto_bitmap_t bitmap, const unsigned bit) {
    unsigned    index = bit >> BITSPERWORDLOG2;     // index of 32 bit word in bitmap holding bit
    unsigned    shift = bit & BITSPERWORDMASK;      // shift to extract bit
    bitmap[index] |= 1 << shift;                    // set one bit
}

#if defined(__ppc__)
//
// Thread safe set the value of the bit at position 'bit' to 1
//
static inline void bitmap_set_atomic(const auto_bitmap_t bitmap, const unsigned bit) {
    unsigned    index = bit >> BITSPERWORDLOG2;     // index of 32 bit word in bitmap holding bit
    unsigned    shift = bit & BITSPERWORDMASK;      // shift to extract bit
    
    register unsigned    *address = bitmap + index; // address of word in bitmap
    register unsigned    mask = 1 << shift;         // mask of bit to set
    register unsigned    tmp;                       // temporary value
    
    __asm__ volatile ("1: lwarx %[tmp],0,%[address]" : [tmp] "=r" (tmp) : [address] "r" (address) : "memory");
    tmp |= mask;                                     // set one bit
    __asm__ volatile ("stwcx. %[tmp],0,%[address]" : : [tmp] "r" (tmp), [address] "r" (address) : "memory");
    __asm__ volatile ("bne- 1b");
}
#endif

//
// Set the value of the bit at position 'bit' to 0.
//
static inline void bitmap_clear(const auto_bitmap_t bitmap, const unsigned bit) {
    unsigned    index = bit >> BITSPERWORDLOG2;     // index of 32 bit word in bitmap holding bit
    unsigned    shift = bit & BITSPERWORDMASK;      // shift to extract bit
    bitmap[index] &= ~(1 << shift);                 // clear one bit
}

#if defined(__ppc__)
//
// Thread safe set the value of the bit at position 'bit' to 0
//
static inline void bitmap_clear_atomic(const auto_bitmap_t bitmap, const unsigned bit) {
    unsigned    index = bit >> BITSPERWORDLOG2;     // index of 32 bit word in bitmap holding bit
    unsigned    shift = bit & BITSPERWORDMASK;      // shift to extract bit
    
    register unsigned    *address = bitmap + index; // address of word in bitmap
    register unsigned    mask = 1 << shift;         // mask of bit to clear
    register unsigned    tmp;                       // temporary value
    
    __asm__ volatile ("1: lwarx %[tmp],0,%[address]" : [tmp] "=r" (tmp) : [address] "r" (address) : "memory");
    tmp &= ~mask;                                   // clear one bit
    __asm__ volatile ("stwcx. %[tmp],0,%[address]" : : [tmp] "r" (tmp), [address] "r" (address) : "memory");
    __asm__ volatile ("bne- 1b");
}
#endif

//
// Set a range of 'num_bits' bits to 1 starting at position 'bit'.
//
static inline void bitmap_set_multiple(const auto_bitmap_t bitmap, const unsigned bit, const unsigned num_bits) {
    unsigned    index = bit >> BITSPERWORDLOG2;     // index of 32 bit word in bitmap holding bit
    unsigned    shift = bit & BITSPERWORDMASK;      // shift to extract bit
    unsigned    mask = MASK(num_bits);              // mask of num_bits
    bitmap[index] |= mask << shift;                 // set bits
    if ((num_bits + shift) > BITSPERWORD) bitmap[index + 1] |= mask >> (BITSPERWORD - shift);   // set spanning part if necessary
}

//
// Set a range of 'num_bits' bits to 0 starting at position 'bit'.
//
static inline void bitmap_clear_multiple(const auto_bitmap_t bitmap, const unsigned bit, const unsigned num_bits) {
    unsigned    index = bit >> BITSPERWORDLOG2;     // index of 32 bit word in bitmap holding bit
    unsigned    shift = bit & BITSPERWORDMASK;      // shift to extract bit
    unsigned    mask = MASK(num_bits);              // mask of num_bits
    bitmap[index] &= ~(mask << shift);              // clear bits
    if ((num_bits + shift) > BITSPERWORD) bitmap[index + 1] &= ~(mask >> (BITSPERWORD - shift));    // clear spanning part if necessary
}

//
// Set all the bits in the bitmap to 0.
//
static inline void bitmap_clear_all(const auto_bitmap_t bitmap, unsigned num_words) {
    memset(bitmap, 0, num_words * sizeof(unsigned));
}

//
// Return the longest sequence of 0 bits in the bitmap (maximum 'MAX_SEQ'.)
//
extern unsigned bitmap_max_seq(const auto_bitmap_t bitmap, unsigned num_words);

//
// Return the bit position of the first sequence of 0 bits with length >= 'seq'.  If no sequence is 
// found the result is NOT_FOUND.
//
extern unsigned bitmap_find_clear_sequence(const auto_bitmap_t bitmap, unsigned num_words, const unsigned seq);

//
// Return the number of contiguous 1 bits found in the bitmap 'in_use' starting at 'bit' and bounded by 1 bits 
// found in the bitmap 'ptr_start'.
//
// The bitmap 'in_use' represents block availability; a 1 indicating the block is in use and 0 indicating
// the block is free.
//
// The bitmap 'ptr_start' represents the set of first blocks.  A 1 indicates that the block begins a new block.
//
extern unsigned bitmap_blocks_used(const auto_bitmap_t in_use, const auto_bitmap_t ptr_start, const unsigned bit);

//
// Return the number of 1 bits in the set.
//
extern unsigned bitmap_count_set(const auto_bitmap_t bitmap, unsigned num_words);

//
// Returns the bit position of the first and last 1 bits in the set.  If *first > *last the the set is empty
//
extern void bitmap_range_set(const auto_bitmap_t bitmap, unsigned num_words, unsigned *first, unsigned *last);

//
// Diagnositic printing of the bitmap.
//
extern void bitmap_print(auto_bitmap_t bitmap, unsigned num_words);


