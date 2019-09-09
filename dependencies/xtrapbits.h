/* lovingly lifted, with mods, from the X sources */
/*****************************************************************************
 Copyright 1987, 1988, 1989, 1990, 1994 by Digital Equipment Corporation,
 Maynard, MA

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and
 that both that copyright notice and this permission notice appear in
 supporting documentation, and that the name of Digital not be used in
 advertising or publicity pertaining to distribution of the software
 without specific, written prior permission.

 DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 NO EVENT SHALL DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
 USE OR PERFORMANCE OF THIS SOFTWARE.
*****************************************************************************/
/*
 *
 *  CONTRIBUTORS:
 *
 *      Dick Annicchiarico
 *      Robert Chesler
 *      Dan Coutu
 *      Gene Durso
 *      Marc Evans
 *      Alan Jamison
 *      Mark Henry
 *      Ken Miller
 *
 */
#ifndef __XTRAPBITS__
#define __XTRAPBITS__

#define BITS_IN_BYTE    8L      /* The number of bits in a byte */

#define bit_in_byte(bit)        /* Returns the bit mask of a byte */ \
    (1L << (((bit) % BITS_IN_BYTE)))

#define bit_in_word(bit)        /* Returns the bit mask of a word */ \
    (1L << (((bit) % (BITS_IN_BYTE * 2L))))

#define bit_in_long(bit)        /* Returns the bit mask of a long */ \
    (1L << (((bit) % (BITS_IN_BYTE * 4L))))

#define byte_in_array(array,bit) /* Returns the byte offset to get to a bit */ \
    (((unsigned char *)(array))[(bit) / BITS_IN_BYTE])

#define bit_is_true(array,bit)  /* Test to see if a specific bit is true */ \
    (byte_in_array(array,bit) & bit_in_byte(bit))

#define bit_is_false(array,bit) /* Test to see if a specific bit is false */ \
    (!(bit_is_true(array,bit)))

#define bit_true(array,bit)     /* Set a specific bit to be true */ \
    (byte_in_array(array,bit) |= bit_in_byte(bit))

#define bit_false(array,bit)    /* Set a specific bit to be false */ \
    (byte_in_array(array,bit) &= ~bit_in_byte(bit))

#define bit_toggle(array,bit)   /* Toggle a specific bit */ \
    (byte_in_array(array,bit) ^= bit_in_byte(bit))

#define bit_copy(dest,src,bit)  /* Copy a specific bit */ \
    bit_is_true((src),(bit)) ? bit_true((dest),(bit)) : bit_false((dest),(bit))

#define bit_value(array,bit)    /* Return true or false depending on bit */ \
    (bit_is_true((array),(bit)) ? 1 : 0)

#define bit_set(array,bit,value) /* Set bit to given value in array */ \
    (value) ? bit_true((array),(bit)) : bit_false((array),(bit))

#endif /* __XTRAPBITS__ */
