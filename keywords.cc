/* C++ code produced by gperf version 3.0.3 */
/* Command-line: gperf --language=C++ --ignore-case --readonly-tables --struct-type --omit-struct-type --key-positions='1,3,$' keywords.gperf  */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 1 "keywords.gperf"
	/* -*- C -*- */

/******************************************************************************
  Copyright (c) 1992, 1995, 1996 Xerox Corporation.  All rights reserved.
  Portions of this code were written by Stephen White, aka ghond.
  Use and copying of this software and preparation of derivative works based
  upon this software are permitted.  Any distribution of this software or
  derivative works must comply with all applicable United States export
  control laws.  This software is made available AS IS, and Xerox Corporation
  makes no warranty about the software, its performance or its conformity to
  any specification.  Any person obtaining a copy of this software is requested
  to send their name and post office or electronic mail address to:
    Pavel Curtis
    Xerox PARC
    3333 Coyote Hill Rd.
    Palo Alto, CA 94304
    Pavel@Xerox.Com
 *****************************************************************************/

#include "my-string.h"

#include "config.h"
#include "keywords.h"
#include "tokens.h"
#include "utils.h"


#define TOTAL_KEYWORDS 38
#define MIN_WORD_LENGTH 2
#define MAX_WORD_LENGTH 9
#define MIN_HASH_VALUE 3
#define MAX_HASH_VALUE 88
/* maximum key range = 86, duplicates = 0 */

#ifndef GPERF_DOWNCASE
#define GPERF_DOWNCASE 1
static unsigned char gperf_downcase[256] =
  {
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
     30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
     45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
     60,  61,  62,  63,  64,  97,  98,  99, 100, 101, 102, 103, 104, 105, 106,
    107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
    122,  91,  92,  93,  94,  95,  96,  97,  98,  99, 100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209,
    210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224,
    225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239,
    240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254,
    255
  };
#endif

#ifndef GPERF_CASE_STRCMP
#define GPERF_CASE_STRCMP 1
static int
gperf_case_strcmp (const char *s1, const char *s2)
{
  for (;;)
    {
      unsigned char c1 = gperf_downcase[(unsigned char)*s1++];
      unsigned char c2 = gperf_downcase[(unsigned char)*s2++];
      if (c1 != 0 && c1 == c2)
        continue;
      return (int)c1 - (int)c2;
    }
}
#endif

class Perfect_Hash
{
private:
  static inline unsigned int hash (const char *str, unsigned int len);
public:
  static const struct keyword *in_word_set (const char *str, unsigned int len);
};

inline unsigned int
Perfect_Hash::hash (const char *str, unsigned int len)
{
  static const unsigned char asso_values[] =
    {
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 15,  5, 20,  5,  0,
       0,  0, 89, 10, 89, 10, 89, 50, 15, 89,
       0, 15,  0, 45, 10, 89, 25,  0, 89, 35,
      89, 89, 89, 89, 89, 89, 89, 15,  5, 20,
       5,  0,  0,  0, 89, 10, 89, 10, 89, 50,
      15, 89,  0, 15,  0, 45, 10, 89, 25,  0,
      89, 35, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89, 89, 89, 89, 89,
      89, 89, 89, 89, 89, 89
    };
  int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[2]];
      /*FALLTHROUGH*/
      case 2:
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval + asso_values[(unsigned char)str[len - 1]];
}

const struct keyword *
Perfect_Hash::in_word_set (const char *str, unsigned int len)
{
  static const struct keyword wordlist[] =
    {
      {""}, {""}, {""},
#line 34 "keywords.gperf"
      {"for",		DBV_Prehistory, tFOR},
      {""}, {""},
#line 65 "keywords.gperf"
      {"E_FILE",		DBV_FileIO,	tERROR,	E_FILE},
#line 59 "keywords.gperf"
      {"E_RANGE",	DBV_Prehistory, tERROR,	E_RANGE},
#line 53 "keywords.gperf"
      {"E_PROPNF",	DBV_Prehistory, tERROR,	E_PROPNF},
#line 57 "keywords.gperf"
      {"E_RECMOVE",	DBV_Prehistory, tERROR,	E_RECMOVE},
#line 33 "keywords.gperf"
      {"endif",		DBV_Prehistory, tENDIF},
#line 36 "keywords.gperf"
      {"endfor",		DBV_Prehistory, tENDFOR},
#line 30 "keywords.gperf"
      {"if",		DBV_Prehistory, tIF},
#line 41 "keywords.gperf"
      {"endwhile",	DBV_Prehistory, tENDWHILE},
#line 37 "keywords.gperf"
      {"fork",		DBV_Prehistory, tFORK},
#line 40 "keywords.gperf"
      {"while",		DBV_Prehistory, tWHILE},
#line 50 "keywords.gperf"
      {"E_TYPE",		DBV_Prehistory, tERROR,	E_TYPE},
#line 64 "keywords.gperf"
      {"E_FLOAT",	DBV_Float,	tERROR,	E_FLOAT},
#line 62 "keywords.gperf"
      {"E_INVARG",	DBV_Prehistory, tERROR,	E_INVARG},
      {""},
#line 47 "keywords.gperf"
      {"break",		DBV_BreakCont, tBREAK},
#line 49 "keywords.gperf"
      {"E_NONE",		DBV_Prehistory, tERROR,	E_NONE},
#line 38 "keywords.gperf"
      {"endfork",	DBV_Prehistory, tENDFORK},
#line 56 "keywords.gperf"
      {"E_INVIND",	DBV_Prehistory, tERROR,	E_INVIND},
      {""}, {""},
#line 66 "keywords.gperf"
      {"E_EXEC",		DBV_Exec,	tERROR,	E_EXEC},
#line 35 "keywords.gperf"
      {"in",		DBV_Prehistory, tIN},
#line 67 "keywords.gperf"
      {"E_INTRPT",	DBV_Interrupt,	tERROR,	E_INTRPT},
      {""}, {""},
#line 39 "keywords.gperf"
      {"return",		DBV_Prehistory, tRETURN},
#line 55 "keywords.gperf"
      {"E_VARNF",	DBV_Prehistory, tERROR,	E_VARNF},
#line 54 "keywords.gperf"
      {"E_VERBNF",	DBV_Prehistory, tERROR,	E_VERBNF},
      {""},
#line 51 "keywords.gperf"
      {"E_DIV",		DBV_Prehistory, tERROR,	E_DIV},
#line 43 "keywords.gperf"
      {"except",		DBV_Exceptions, tEXCEPT},
#line 63 "keywords.gperf"
      {"E_QUOTA",	DBV_Prehistory, tERROR,	E_QUOTA},
      {""}, {""}, {""},
#line 61 "keywords.gperf"
      {"E_NACC",		DBV_Prehistory, tERROR,	E_NACC},
      {""},
#line 48 "keywords.gperf"
      {"continue",	DBV_BreakCont, tCONTINUE},
      {""}, {""},
#line 45 "keywords.gperf"
      {"endtry",		DBV_Exceptions, tENDTRY},
      {""}, {""},
#line 31 "keywords.gperf"
      {"else",		DBV_Prehistory, tELSE},
      {""},
#line 32 "keywords.gperf"
      {"elseif",		DBV_Prehistory, tELSEIF},
      {""}, {""}, {""}, {""},
#line 52 "keywords.gperf"
      {"E_PERM",		DBV_Prehistory, tERROR,	E_PERM},
#line 44 "keywords.gperf"
      {"finally",	DBV_Exceptions, tFINALLY},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
#line 60 "keywords.gperf"
      {"E_ARGS",		DBV_Prehistory, tERROR,	E_ARGS},
      {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""}, {""},
      {""}, {""},
#line 58 "keywords.gperf"
      {"E_MAXREC",	DBV_Prehistory, tERROR,	E_MAXREC},
      {""}, {""}, {""}, {""},
#line 42 "keywords.gperf"
      {"try",		DBV_Exceptions, tTRY},
      {""}, {""}, {""}, {""},
#line 46 "keywords.gperf"
      {"ANY",		DBV_Exceptions, tANY}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      int key = hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          const char *s = wordlist[key].name;

          if ((((unsigned char)*str ^ (unsigned char)*s) & ~32) == 0 && !gperf_case_strcmp (str, s))
            return &wordlist[key];
        }
    }
  return 0;
}
#line 68 "keywords.gperf"


const struct keyword *
find_keyword(const char *word)
{
    return Perfect_Hash::in_word_set(word, strlen(word));
}
