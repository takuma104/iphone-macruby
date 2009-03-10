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

#import "auto_impl_utilities.h"
#import "auto_bitmaps.h"

#import <libc.h>
#import <objc/malloc.h>



/*********  32-bit-at-a-time bitmaps ***************/


//
// FIND BIT SEQUENCE ALGORITHM
//
// The algorithm used here takes advantage of fast bit count available on the PowerPC
// and newer Intel chips.
//
// The find bit sequence algorithm iterates through each word in the bitmap examining
// consecutive runs of 0s or 1s.
// 
// We start by setting up a mask of N one bits (least significant end.)  This will
// be used to detect the run of 0s we need.
// 
// Iterating through each word we look to see if the least significant bits are 0.
// 'trailing_zeroes' returns a consecutive run of 1s for each 0 in the least
// significant end of the word.  If this run is >= than our mask then we can return
// our result.
// 
// To look at the next run of 0s in the word we need to skip the current run of 0s
// and the adjacent run of 1s.  We can do this by merging the run we just
// constructed (1s for 0s) from 'trailing_zeroes' and the original word.  We can
// then generate a run of trailing 1s and count the bits in this.  This count can
// be used to shift the word to the next run of 0s.  We also track the bit position
// in the word ('bit'.)
// 
// If the original word has a run of 0s in the most significant bits then we need
// to set up a barrier of 1s to get an accurate count of 0s.  We can do this by
// inverting the previous run and shifting by the current bit position.
// 
// If the original word has a run of 0s in the most significant bits we may need to
// carry forward those 0s to append the end of the next word.  We do this by
// setting 'bit' to the negative of the number of 0s left over.  This has two
// advantages.  It gives us an indicator of the carry forward ('bit' < 0) and the
// calculation of overall bit position naturally corrects against the next word.
// 
// If we encounter a word of all 0s the we have an immediate result.  If we
// encounter a word of all 1s then we need to reset and skip the word.
// 
// This process continues until we either return or reach the end of the bitmap.
// 
// Example: Find a sequence of seven 0 bits.
//  
//   bitmap[] = {
//     0b0000_1111_1111_1111_1111_1111_1110_0000,
//     0b0000_0000_0000_0000_0000_0000_1111_1000,
//     ... };
//     
//   
//   Setup:
//   
//     index = 0;
//     bit = 0;               // bit position 0 (least significant bit)
//     mask = 0b0111_1111;    // a mask of seven 1s
//     word = bitmap[index];  // get the first word
//     
//   
//   Isolate the trailing 0s:
//   
//     run      = trailing_zeros(word);
//     0b1_1111 = trailing_zeros(0b0000_1111_1111_1111_1111_1111_1110_0000);
//     
//     
//   Check to see if the run is greater or equal to our mask:
//   
//     run      >= mask is not true so we continue.
//     0b1_1111 >= 0b111_1111
//     
//     
//   Merge the run with the original word:
//   
//     run                                       = word                                      | run;
//     0b0000_1111_1111_1111_1111_1111_1111_1111 = 0b0000_1111_1111_1111_1111_1111_1110_0000 | 0b1_1111
//     
//   
//   Isolate the trailing 1s:
//   
//     run                                       = trailing_ones(run);
//     0b0000_1111_1111_1111_1111_1111_1111_1111 = trailing_ones(0b0000_1111_1111_1111_1111_1111_1111_1111);
//     
//   
//   Count the trailing 1s:
//      
//     count = bitmap_count(run);
//     28    = bitmap_count(0b0000_1111_1111_1111_1111_1111_1111_1111);
//     
//     
//   Shift word and increment bit position:
//   
//     word                                      = word                                      >> count;
//     0b0000_0000_0000_0000_0000_0000_0000_0000 = 0b0000_1111_1111_1111_1111_1111_1110_0000 >> 28;
//     
//     bit += count;
//     28
//     
//     
//   Word is zero so we need to check to see if there were enough bits left:
//   
//     run                                       = ~run;
//     0b1111_0000_0000_0000_0000_0000_0000_0000 = 0b0000_1111_1111_1111_1111_1111_1111_1111;
//     
//     run     = run                                       >> count;
//     0xb1111 = 0b1111_0000_0000_0000_0000_0000_0000_0000 >> 28;
//   
//     run     >= mask is not true so we continue.
//     0xb1111 >= 0b111_1111
//     
//     
//   To indicate carry word we set bit negative:
//   
//     bit = bit - 32;
//     -4  = 28  - 32;
//     
//     
//   We get the next word and adjust it to pretend there were extra 0s from the previous word.
//   
//     next = bitmap[++index];
//     word                                      = next                                      << -bit;
//     0b0000_0000_0000_0000_0000_1111_1000_0000 = 0b0000_0000_0000_0000_0000_0000_1111_1000 << -(-4);
//     
//     
//   The process is repeated but this time we have a match:
//   
//     run      >= mask  so we have a match
//     0b111_1111 >= 0b111_1111
//     
//     
//   If it didn't match, bit < 0 indicates that we need to correct from the carry forward.
//     




//
// Return the longest sequence of 0 bits in the bitmap (maximum 'MAX_SEQ'.)
//
// Using the Find Bit Sequence Algorithm (see above) we search for a sequence of MAX_SEQ 0s.
// Along the way we track the longest run of 0s we encounter.  The result is the longest
// run found.
//
unsigned bitmap_max_seq(const auto_bitmap_t bitmap, unsigned num_words) {
    unsigned    max_seq = 0;            // maximum seq found
    unsigned    index = 0;              // index of 32 bit word in bitmap
    unsigned    mask = MASK(MAX_SEQ);   // mask of seq bits
    unsigned    word = bitmap[0];       // current word from bitmap
    signed      bit = 0;                // current bit in word, if bit < 0 then word spans from previous word
    unsigned    next = 0;               // next word from bitmap
    
    if (!num_words) return 0;           // exit early if empty bitmap
    
    while (1) {
        unsigned    run;                // mask of consecutive bits
        unsigned    shift;              // required shift
        
        // exit early if word is all clear
        if (!word) return MAX_SEQ;

        // get the run (mask) of trailing zeros
        run = trailing_zeroes(word);
        
        // if there is a run update the maximum
        if (run) {
            // if the run exceeds or equals mask then we have enough (note: bit may be carry over from previous word)
            if (run >= mask) return MAX_SEQ;
            
            unsigned    lng = bitmap_count(run);
            max_seq = lng > max_seq ? lng : max_seq;
        }
        
        // get the run (mask) of ones including the previous run of zeroes
        run = trailing_ones(word | run);
        
        // if we span from previous word then correct for rest of word
        if (bit < 0) {
            run >>= -bit;
            word = next;
            bit = 0;
        }
        
        // compute the shift we need to get the previous ones and zeroes out of the way
        shift = bitmap_count(run);
        
        // get the previous ones and zeroes out of the way
        word >>= shift;
        
        // update the bit position
        bit += shift;

        // if the rest of the word is zero
        if (!word) {
            run = ~run >> bit;
            
            if (run) {
                // check to see if what is left is big enough
                if (run >= mask) return MAX_SEQ;
            
                unsigned    lng = bitmap_count(run);
                max_seq = lng > max_seq ? lng : max_seq;
            }
            
            while (1) {
                // if there are no more words we are out of luck
                if (++index >= num_words) return max_seq;
                
                // get the next word
                next = bitmap[index];
                
                // continue if some bits clear
                if (~next) break;
                
                // need to reset bit position and try again
                bit = 32;
            }
            
            // set up a negative bit value if there are bits left over from previous word, otherwise start at zero
            bit = bit < 32 ? bit - 32 : 0;
            
            // make adjustment if zeroes in previous word
            word = next << -bit;
        }
    }

    return max_seq;
}

//
// Return the bit position of the first sequence of 0 bits with length >= 'seq'.  If no sequence is 
// found the result is NOT_FOUND.
//
// Using the Find Bit Sequence Algorithm (see above) we search for a sequence of 'seq' 0s.
// When we find a match then we return the result of (index * BITSPERWORD + bit).  Note: if 'bit' < 0
// then the result is still correct since index has been advanced to the next word.
//
unsigned bitmap_find_clear_sequence(const auto_bitmap_t bitmap, unsigned num_words, const unsigned seq) {
    // finds the first sequence in the bitmap with seq (1..MAX_SEQ) bits free (zero)
    // may read up to num_words of bitmap
    // returns bit position
    unsigned    index = 0;              // index of 32 bit word in bitmap
    unsigned    mask = MASK(seq);       // mask of seq bits
    unsigned    word = bitmap[0];       // current word from bitmap
    signed      bit = 0;                // current bit in word, if bit < 0 then word spans from previous word
    unsigned    next = 0;               // next word from bitmap
    
    if (!num_words) return NOT_FOUND;   // exit early if empty bitmap
    
    while (1) {
        unsigned    run;                // mask of consecutive bits
        unsigned    shift;              // required shift
        
        // exit early if word is all clear
        if (!word) return index * BITSPERWORD + bit;

        // get the run (mask) of trailing zeros
        run = trailing_zeroes(word);
        
        // if the run exceeds or equals mask then we have enough (note: bit may be carry over from previous word)
        if (run >= mask) return index * BITSPERWORD + bit;
        
        // get the run (mask) of ones including the previous run of zeroes
        run = trailing_ones(word | run);
        
        // if we span from previous word then correct for rest of word
        if (bit < 0) {
            run >>= -bit;
            word = next;
            bit = 0;
        }
        
        // compute the shift we need to get the previous ones and zeroes out of the way
        shift = bitmap_count(run);
        
        // get the previous ones and zeroes out of the way
        word >>= shift;
        
        // update the bit position
        bit += shift;

        // if the rest of the word is zero
        if (!word) {
            // remaining zeros
            run = ~run >> bit;
            
            // check to see if what is left is big enough
            if (run >= mask) return index * BITSPERWORD + bit;
            
            while (1) {
                // if there are no more words we are out of luck
                if (++index >= num_words) return NOT_FOUND;
                
                // get the next word
                next = bitmap[index];
                
                // continue if some bits clear
                if (~next) break;
                
                // need to reset bit position and try again
                bit = 32;
            }
            
            // set up a negative bit value if there are bits left over from previous word, otherwise start at zero
            bit = bit < 32 ? bit - 32 : 0;
            
            // make adjustment if zeroes in previous word
            word = next << -bit;
        }
    }

    // out of luck
    return NOT_FOUND;
}

//
// Return the number of contiguous 1 bits found in the bitmap 'in_use' starting at 'bit' and bounded by 1 bits 
// found in the bitmap 'ptr_start'.
//
// The bitmap 'in_use' represents block availability; a 1 indicating the block is in use and 0 indicating
// the block is free.
//
// The bitmap 'ptr_start' represents the set of first blocks.  A 1 indicates that the block begins a new block.
//
// Example:
// 
//   in_use[] = {
//     0b0000_0000_0000_0000_0000_1111_1111_1111,
//     ... };
//     
//   ptr_start[] = {
//     0b0000_0000_0000_0000_0000_0000_1001_0001,
//     ... };
//   
//   bit = 4;
//     
//     
// Setup:
//  
//   index = bit >> BITSPERWORDLOG2;
//   0     = 4   >> 5;
//   
//   shift = bit & BITSPERWORDMASK;
//   4     = 4   & 31;
//     
//     
// Get first words and position to bit (spanning words are not necessary in this example):
//   in_use_bits                               = in_use[index]                             >> shift;
//   0b0000_0000_0000_0000_0000_0000_1111_1111 = 0b0000_0000_0000_0000_0000_1111_1111_1111 >> 4;
//   
//   ptr_start_bits                            = ptr_start[index]                          >> shift;
//   0b0000_0000_0000_0000_0000_0000_0000_1001 = 0b0000_0000_0000_0000_0000_0000_1001_0001 >> 4;
// 
// 
// Clear out starts:
// 
//   in_use_bits                               = in_use_bits                               & ~ptr_start_bits;
//   0b0000_0000_0000_0000_0000_0000_1111_0110 = 0b0000_0000_0000_0000_0000_0000_1111_1111 & ~0b0000_0000_0000_0000_0000_0000_0000_1001;
// 
// 
// Add first block back in:
// 
//   in_use_bits                               = in_use_bits                               | 1;
//   0b0000_0000_0000_0000_0000_0000_1111_0111 = 0b0000_0000_0000_0000_0000_0000_1111_0110 | 1;
//   
// 
// Isolate trailing 1s:
// 
//   in_use_bits                               = trailing_ones(in_use_bits);
//   0b0000_0000_0000_0000_0000_0000_0000_0111 = trailing_ones(0b0000_0000_0000_0000_0000_0000_1111_0111);
//   
//   
// Count trailing 1s:
// 
//   return bitmap_count(in_use_bits);
//   3      bitmap_count(0b0000_0000_0000_0000_0000_0000_0000_0111;
//
unsigned bitmap_blocks_used(const auto_bitmap_t in_use, const auto_bitmap_t ptr_start, const unsigned bit) {
    unsigned    index = bit >> BITSPERWORDLOG2;     // index of 32 bit word in bitmap holding bit
    unsigned    shift = bit & BITSPERWORDMASK;      // shift to extract bit

    // extract portion from first word
    unsigned in_use_bits = in_use[index] >> shift;               
    unsigned ptr_start_bits = ptr_start[index] >> shift;               
    
    if ((shift + 7) > BITSPERWORD) {
        // extract spanning part if necessary
        unsigned inverse_shift = 32 - shift;
        in_use_bits |= in_use[index + 1] << inverse_shift;   
        ptr_start_bits |= ptr_start[index + 1] << inverse_shift;   
    }
    
    // clear starting bits (beginning of next)
    in_use_bits &= ~ptr_start_bits;
    
    // add back in this block's first
    in_use_bits |= 1;
    
    // count the in use bits
    return bitmap_count(trailing_ones(in_use_bits));
}

//
// Return the number of 1 bits in the set.
//
// Looking a word at a time we count the run of 1s in the least significant portion
// of the word.  Then we skip over the 1s and the adjacent 0s. repeat until the
// word is zero.
//
unsigned bitmap_count_set(const auto_bitmap_t bitmap, unsigned num_words) {
    unsigned    num_bits = 0;       // running count of set bits
    
    // while there are words
    while (num_words--) {
        // get the next word (last to first)
        unsigned    word = bitmap[num_words];
        
        // while the word has bits set
        while (word) {
            // get the run of trailing ones
            unsigned    run = trailing_ones(word);
            
            // count those bits
            num_bits += bitmap_count(run);
            
            // clear those bits
            word &= ~run;
            
            // shift up to the next sequence of ones
            word >>= bitmap_count(trailing_zeroes(word));
        }
    }

    // return the number of bits
    return num_bits;
}

//
// Returns the bit position of the first and last 1 bits in the set.  If *first > *last the the set is empty
//
// Starting form the beginning we look for a non-zero word in the bitmap.  We then count the trailing 0s to 
// find the first bit position.
//
// Then starting from the end look for a the last non-zero word.   Then we count leading 0s (more or less) to
// find the last bit position.
//
void bitmap_range_set(const auto_bitmap_t bitmap, unsigned num_words, unsigned *first, unsigned *last) {
    unsigned    lo = 0, hi = num_words;     // index range to look
    unsigned    word;                       // current word

    // skip zero words from the first
    for ( word = bitmap[0]; !word && lo < hi; word = bitmap[++lo]) {}

    // if no non zero words found
    if (lo >= hi) {
        *first = UNLIMITED;
        *last = 0;
    }
    
    // set up the first bit
    *first = (lo * BITSPERWORD) + bitmap_count(trailing_zeroes(word));

    // skip zero words from the last
    for (word = bitmap[--hi]; !word && lo <= hi; word = bitmap[--hi]) {}
    
    // set up the last bit
    *last = (hi * BITSPERWORD) + bitmap_count(word);
}

//
// Diagnositic printing of the bitmap.
//
void bitmap_print(auto_bitmap_t bitmap, unsigned num_words) {
    unsigned    num_all_0 = 0;
    unsigned    num_all_1 = 0;
    while (num_words--) {
        unsigned    word = *bitmap++;
        if (!word) {
            if (num_all_1) { printf("1*%d ", num_all_1); num_all_1 = 0; }
            num_all_0++;
        } else if (!~word) {
            if (num_all_0) { printf("0*%d ", num_all_0); num_all_0 = 0; }
            num_all_1++;
        } else {
            if (num_all_0) { printf("0*%d ", num_all_0); num_all_0 = 0; }
            if (num_all_1) { printf("1*%d ", num_all_1); num_all_1 = 0; }
            unsigned    bit = 0;
            while (bit < 32) { 
                printf("%d", bitmap_bit(&word, bit));
                bit++;
            }
            printf(" ");
        }
    }
    if (num_all_0) { printf("0*%d ", num_all_0); num_all_0 = 0; }
    if (num_all_1) { printf("1*%d ", num_all_1); num_all_1 = 0; }
    printf("\n");
}
