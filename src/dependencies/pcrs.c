/*********************************************************************
 *
 * File        :  $Source: /cvsroot/ijbswa/current/pcrs.c,v $
 *
 * Purpose     :  pcrs is a supplement to the pcre library by Philip Hazel
 *                <ph10@cam.ac.uk> and adds Perl-style substitution. That
 *                is, it mimics Perl's 's' operator. See pcrs(3) for details.
 *
 *                WARNING: This file contains additional functions and bug
 *                fixes that aren't part of the latest official pcrs package
 *                (which apparently is no longer maintained).
 *
 * Copyright   :  Written and Copyright (C) 2000, 2001 by Andreas S. Oesterhelt
 *                <andreas@oesterhelt.org>
 *
 *                Copyright (C) 2006, 2007 Fabian Keil <fk@fabiankeil.de>
 * 
 * Portions Copyright   :  Written by and Copyright (C) 2001 the
 *                Privoxy team. https://www.privoxy.org/
 *
 *                This program is free software; you can redistribute it
 *                and/or modify it under the terms of the GNU General
 *                Public License as published by the Free Software
 *                Foundation; either version 2 of the License, or (at
 *                your option) any later version.
 *
 *                This program is distributed in the hope that it will
 *                be useful, but WITHOUT ANY WARRANTY; without even the
 *                implied warranty of MERCHANTABILITY or FITNESS FOR A
 *                PARTICULAR PURPOSE.  See the GNU General Public
 *                License for more details.
 *
 *                The GNU General Public License should be included with
 *                this file.  If not, you can view it at
 *                http://www.gnu.org/copyleft/gpl.html
 *                or write to the Free Software Foundation, Inc., 59
 *                Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *********************************************************************/


#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdio.h>

#include "pcrs.h"

/*
 * Internal prototypes
 */

static int              pcrs_parse_perl_options(const char *optstring, int *flags);
static pcrs_substitute *pcrs_compile_replacement(const char *replacement, int trivialflag,
                        int capturecount, int *errptr);
static int              is_hex_sequence(const char *sequence);
static int              xtoi(const char *s);
static int              xdtoi(const int d);

/*********************************************************************
 *
 * Function    :  pcrs_strerror
 *
 * Description :  Return a string describing a given error code.
 *
 * Parameters  :
 *          1  :  error = the error code
 *
 * Returns     :  char * to the descriptive string
 *
 *********************************************************************/
const char *pcrs_strerror(const int error)
{
   static char buf[100];

   if (error != 0)
   {
      switch (error)
      {
         /* Passed-through PCRE error: */
         case PCRE_ERROR_NOMEMORY:     return "No memory";

         /* Shouldn't happen unless PCRE or PCRS bug, or user messed with compiled job: */
         case PCRE_ERROR_NULL:         return "NULL code or subject or ovector";
         case PCRE_ERROR_BADOPTION:    return "Unrecognized option bit";
         case PCRE_ERROR_BADMAGIC:     return "Bad magic number in code";
         case PCRE_ERROR_UNKNOWN_NODE: return "Bad node in pattern";

         /* Can't happen / not passed: */
         case PCRE_ERROR_NOSUBSTRING:  return "PCRE error: No substring";
         case PCRE_ERROR_NOMATCH:      return "PCRE error: No match";

#ifdef PCRE_ERROR_MATCHLIMIT
         case PCRE_ERROR_MATCHLIMIT:   return "Match limit reached";
#endif /* def PCRE_ERROR_MATCHLIMIT */

         /* PCRS errors: */
         case PCRS_ERR_NOMEM:          return "No memory";
         case PCRS_ERR_CMDSYNTAX:      return "Syntax error while parsing command";
         case PCRS_ERR_STUDY:          return "PCRE error while studying the pattern";
         case PCRS_ERR_BADJOB:         return "Bad job - NULL job, pattern or substitute";
         case PCRS_WARN_BADREF:        return "Backreference out of range";
         case PCRS_WARN_TRUNCATION:
            return "At least one variable was too big and has been truncated before compilation";
         
         default:
            snprintf(buf, sizeof(buf), "Unknown error (%d)", error);
            return buf;
      }
   }
   /* error >= 0: No error */
   return "Everything's just fine. Thanks for asking.";

}


/*********************************************************************
 *
 * Function    :  pcrs_parse_perl_options
 *
 * Description :  This function parses a string containing the options to
 *                Perl's s/// operator. It returns an integer that is the
 *                pcre equivalent of the symbolic optstring.
 *                Since pcre doesn't know about Perl's 'g' (global) or pcrs',
 *                'T' (trivial) options but pcrs needs them, the corresponding
 *                flags are set if 'g'or 'T' is encountered.
 *                Note: The 'T' and 'U' options do not conform to Perl.
 *
 * Parameters  :
 *          1  :  optstring = string with options in perl syntax
 *          2  :  flags = see description
 *
 * Returns     :  option integer suitable for pcre
 *
 *********************************************************************/
static int pcrs_parse_perl_options(const char *optstring, int *flags)
{
   size_t i;
   int rc = 0;
   *flags = 0;

   if (NULL == optstring) return 0;

   for (i = 0; i < strlen(optstring); i++)
   {
      switch(optstring[i])
      {
         case 'e': break; /* ToDo ;-) */
         case 'g': *flags |= PCRS_GLOBAL; break;
         case 'i': rc |= PCRE_CASELESS; break;
         case 'm': rc |= PCRE_MULTILINE; break;
         case 'o': break;
         case 's': rc |= PCRE_DOTALL; break;
         case 'x': rc |= PCRE_EXTENDED; break;
         case 'D': *flags |= PCRS_DYNAMIC; break;
         case 'U': rc |= PCRE_UNGREEDY; break;
         case 'T': *flags |= PCRS_TRIVIAL; break;
         default: break;
      }
   }
   return rc;

}

/*********************************************************************
 *
 * Function    :  pcrs_compile_replacement
 *
 * Description :  This function takes a Perl-style replacement (2nd argument
 *                to the s/// operator and returns a compiled pcrs_substitute,
 *                or NULL if memory allocation for the substitute structure
 *                fails.
 *
 * Parameters  :
 *          1  :  replacement = replacement part of s/// operator
 *                              in perl syntax
 *          2  :  trivialflag = Flag that causes backreferences to be
 *                              ignored.
 *          3  :  capturecount = Number of capturing subpatterns in
 *                               the pattern. Needed for $+ handling.
 *          4  :  errptr = pointer to an integer in which error
 *                         conditions can be returned.
 *
 * Returns     :  pcrs_substitute data structure, or NULL if an
 *                error is encountered. In that case, *errptr has
 *                the reason.
 *
 *********************************************************************/
static pcrs_substitute *pcrs_compile_replacement(const char *replacement, int trivialflag, int capturecount, int *errptr)
{
   int i, k, l, quoted;
   char *text;
   pcrs_substitute *r;
   size_t length;
   i = k = l = quoted = 0;

   /*
    * Sanity check
    */
   if (NULL == replacement)
   {
      replacement = "";
   }

   /*
    * Get memory or fail
    */
   if (NULL == (r = (pcrs_substitute *)malloc(sizeof(pcrs_substitute))))
   {
      *errptr = PCRS_ERR_NOMEM;
      return NULL;
   }
   memset(r, '\0', sizeof(pcrs_substitute));

   length = strlen(replacement);

   if (NULL == (text = (char *)malloc(length + 1)))
   {
      free(r);
      *errptr = PCRS_ERR_NOMEM;
      return NULL;
   }
   memset(text, '\0', length + 1);


   /*
    * In trivial mode, just copy the substitute text
    */
   if (trivialflag)
   {
      strncpy(text, replacement, length + 1);
      k = (int)length;
   }

   /*
    * Else, parse, cut out and record all backreferences
    */
   else
   {
      while (i < (int)length)
      {
         /* Quoting */
         if (replacement[i] == '\\')
         {
            if (quoted)
            {
               text[k++] = replacement[i++];
               quoted = 0;
            }
            else
            {
               if (replacement[i+1] && strchr("tnrfae0", replacement[i+1]))
               {
                  switch (replacement[++i])
                  {
                  case 't':
                     text[k++] = '\t';
                     break;
                  case 'n':
                     text[k++] = '\n';
                     break;
                  case 'r':
                     text[k++] = '\r';
                     break;
                  case 'f':
                     text[k++] = '\f';
                     break;
                  case 'a':
                     text[k++] = 7;
                     break;
                  case 'e':
                     text[k++] = 27;
                     break;
                  case '0':
                     text[k++] = '\0';
                     break;
                  }
                  i++;
               }
               else if (is_hex_sequence(&replacement[i]))
               {
                  /*
                   * Replace a hex sequence with a single
                   * character with the sequence's ascii value.
                   * e.g.: '\x7e' => '~'
                   */
                  const int ascii_value = xtoi(&replacement[i+2]);

                  assert(ascii_value >= 0);
                  assert(ascii_value < 256);
                  text[k++] = (char)ascii_value;
                  i += 4;
               }
               else
               {
                  quoted = 1;
                  i++;
               }
            }
            continue;
         }

         /* Backreferences */
         if (replacement[i] == '$' && !quoted && i < (int)(length - 1))
         {
            char *symbol, symbols[] = "'`+&";
            if (l >= PCRS_MAX_SUBMATCHES)
            {
               freez(text);
               freez(r);
               *errptr = PCRS_WARN_BADREF;
               return NULL;
            }
            r->block_length[l] = (size_t)(k - r->block_offset[l]);

            /* Numerical backreferences */
            if (isdigit((int)replacement[i + 1]))
            {
               while (i < (int)length && isdigit((int)replacement[++i]))
               {
                  r->backref[l] = r->backref[l] * 10 + replacement[i] - 48;
               }
               if (r->backref[l] > capturecount)
               {
                  freez(text);
                  freez(r);
                  *errptr = PCRS_WARN_BADREF;
                  return NULL;
               }
            }

            /* Symbolic backreferences: */
            else if (NULL != (symbol = strchr(symbols, replacement[i + 1])))
            {

               if (symbol - symbols == 2) /* $+ */
               {
                  r->backref[l] = capturecount;
               }
               else if (symbol - symbols == 3) /* $& */
               {
                  r->backref[l] = 0;
               }
               else /* $' or $` */
               {
                  r->backref[l] = (int)(PCRS_MAX_SUBMATCHES + 1 - (symbol - symbols));
               }
               i += 2;
            }

            /* Invalid backref -> plain '$' */
            else
            {
               goto plainchar;
            }

            assert(r->backref[l] < PCRS_MAX_SUBMATCHES + 2);
            /* Valid and in range? -> record */
            if ((0 <= r->backref[l]) &&
               (r->backref[l] < PCRS_MAX_SUBMATCHES + 2) &&
               (l < PCRS_MAX_SUBMATCHES - 1))
            {
               r->backref_count[r->backref[l]] += 1;
               r->block_offset[++l] = k;
            }
            else
            {
               freez(text);
               freez(r);
               *errptr = PCRS_WARN_BADREF;
               return NULL;
            }
            continue;
         }

plainchar:
         /* Plain chars are copied */
         text[k++] = replacement[i++];
         quoted = 0;
      }
   } /* -END- if (!trivialflag) */

   /*
    * Finish & return
    */
   r->text = text;
   r->backrefs = l;
   r->length = (size_t)k;
   r->block_length[l] = (size_t)(k - r->block_offset[l]);

   return r;

}


/*********************************************************************
 *
 * Function    :  pcrs_free_job
 *
 * Description :  Frees the memory used by a pcrs_job struct and its
 *                dependent structures.
 *
 * Parameters  :
 *          1  :  job = pointer to the pcrs_job structure to be freed
 *
 * Returns     :  a pointer to the next job, if there was any, or
 *                NULL otherwise.
 *
 *********************************************************************/
pcrs_job *pcrs_free_job(pcrs_job *job)
{
   pcrs_job *next;

   if (job == NULL)
   {
      return NULL;
   }
   else
   {
      next = job->next;
      if (job->pattern != NULL) free(job->pattern);
      if (job->hints != NULL)
      {
#ifdef PCRE_CONFIG_JIT
         pcre_free_study(job->hints);
#else
         free(job->hints);
#endif
      }
      if (job->substitute != NULL)
      {
         if (job->substitute->text != NULL) free(job->substitute->text);
         free(job->substitute);
      }
      free(job);
   }
   return next;

}


/*********************************************************************
 *
 * Function    :  pcrs_free_joblist
 *
 * Description :  Iterates through a chained list of pcrs_job's and
 *                frees them using pcrs_free_job.
 *
 * Parameters  :
 *          1  :  joblist = pointer to the first pcrs_job structure to
 *                be freed
 *
 * Returns     :  N/A
 *
 *********************************************************************/
void pcrs_free_joblist(pcrs_job *joblist)
{
   while (NULL != (joblist = pcrs_free_job(joblist))) {};

   return;

}


/*********************************************************************
 *
 * Function    :  pcrs_compile_command
 *
 * Description :  Parses a string with a Perl-style s/// command,
 *                calls pcrs_compile, and returns a corresponding
 *                pcrs_job, or NULL if parsing or compiling the job
 *                fails.
 *
 * Parameters  :
 *          1  :  command = string with perl-style s/// command
 *          2  :  errptr = pointer to an integer in which error
 *                         conditions can be returned.
 *
 * Returns     :  a corresponding pcrs_job data structure, or NULL
 *                if an error was encountered. In that case, *errptr
 *                has the reason.
 *
 *********************************************************************/
pcrs_job *pcrs_compile_command(const char *command, int *errptr)
{
   int i, k, l, quoted = FALSE;
   size_t limit;
   char delimiter;
   char *tokens[4];
   pcrs_job *newjob;

   k = l = 0;

   /*
    * Tokenize the perl command
    */
   limit = strlen(command);
   if (limit < 4)
   {
      *errptr = PCRS_ERR_CMDSYNTAX;
      return NULL;
   }
   else
   {
      delimiter = command[1];
   }

   tokens[l] = (char *) malloc(limit + 1);

   for (i = 0; i <= (int)limit; i++)
   {

      if (command[i] == delimiter && !quoted)
      {
         if (l == 3)
         {
            l = -1;
            break;
         }
         tokens[0][k++] = '\0';
         tokens[++l] = tokens[0] + k;
         continue;
      }

      else if (command[i] == '\\' && !quoted)
      {
         quoted = TRUE;
         if (command[i+1] == delimiter) continue;
      }
      else
      {
         quoted = FALSE;
      }
      tokens[0][k++] = command[i];
   }

   /*
    * Syntax error ?
    */
   if (l != 3)
   {
      *errptr = PCRS_ERR_CMDSYNTAX;
      free(tokens[0]);
      return NULL;
   }

   newjob = pcrs_compile(tokens[1], tokens[2], tokens[3], errptr);
   free(tokens[0]);
   return newjob;

}


/*********************************************************************
 *
 * Function    :  pcrs_compile
 *
 * Description :  Takes the three arguments to a perl s/// command
 *                and compiles a pcrs_job structure from them.
 *
 * Parameters  :
 *          1  :  pattern = string with perl-style pattern
 *          2  :  substitute = string with perl-style substitute
 *          3  :  options = string with perl-style options
 *          4  :  errptr = pointer to an integer in which error
 *                         conditions can be returned.
 *
 * Returns     :  a corresponding pcrs_job data structure, or NULL
 *                if an error was encountered. In that case, *errptr
 *                has the reason.
 *
 *********************************************************************/
pcrs_job *pcrs_compile(const char *pattern, const char *substitute, const char *options, int *errptr)
{
   pcrs_job *newjob;
   int flags;
   int capturecount;
   const char *error;
   int pcre_study_options = 0;

   *errptr = 0;

   /*
    * Handle NULL arguments
    */
   if (pattern == NULL) pattern = "";
   if (substitute == NULL) substitute = "";


   /*
    * Get and init memory
    */
   if (NULL == (newjob = (pcrs_job *)malloc(sizeof(pcrs_job))))
   {
      *errptr = PCRS_ERR_NOMEM;
      return NULL;
   }
   memset(newjob, '\0', sizeof(pcrs_job));


   /*
    * Evaluate the options
    */
   newjob->options = pcrs_parse_perl_options(options, &flags);
   newjob->flags = flags;


   /*
    * Compile the pattern
    */
   newjob->pattern = pcre_compile(pattern, newjob->options, &error, errptr, NULL);
   if (newjob->pattern == NULL)
   {
      pcrs_free_job(newjob);
      return NULL;
   }


#ifdef PCRE_STUDY_JIT_COMPILE
   if (!(flags & PCRS_DYNAMIC))
   {
      pcre_study_options = PCRE_STUDY_JIT_COMPILE;
   }
#endif

   /*
    * Generate hints. This has little overhead, since the
    * hints will be NULL for a boring pattern anyway.
    */
   newjob->hints = pcre_study(newjob->pattern, pcre_study_options, &error);
   if (error != NULL)
   {
      *errptr = PCRS_ERR_STUDY;
      pcrs_free_job(newjob);
      return NULL;
   }


   /*
    * Determine the number of capturing subpatterns.
    * This is needed for handling $+ in the substitute.
    */
   if (0 > (*errptr = pcre_fullinfo(newjob->pattern, newjob->hints, PCRE_INFO_CAPTURECOUNT, &capturecount)))
   {
      pcrs_free_job(newjob);
      return NULL;
   }


   /*
    * Compile the substitute
    */
   if (NULL == (newjob->substitute = pcrs_compile_replacement(substitute, newjob->flags & PCRS_TRIVIAL, capturecount, errptr)))
   {
      pcrs_free_job(newjob);
      return NULL;
   }

   return newjob;

}


/*********************************************************************
 *
 * Function    :  pcrs_execute_list
 *
 * Description :  This is a multiple job wrapper for pcrs_execute().
 *                Apply the regular substitutions defined by the jobs in
 *                the joblist to the subject.
 *                The subject itself is left untouched, memory for the result
 *                is malloc()ed and it is the caller's responsibility to free
 *                the result when it's no longer needed.
 *
 *                Note: For convenient string handling, a null byte is
 *                      appended to the result. It does not count towards the
 *                      result_length, though.
 *
 *
 * Parameters  :
 *          1  :  joblist = the chained list of pcrs_jobs to be executed
 *          2  :  subject = the subject string
 *          3  :  subject_length = the subject's length
 *          4  :  result = char** for returning  the result
 *          5  :  result_length = size_t* for returning the result's length
 *
 * Returns     :  On success, the number of substitutions that were made.
 *                 May be > 1 if job->flags contained PCRS_GLOBAL
 *                On failure, the (negative) pcre error code describing the
 *                 failure, which may be translated to text using pcrs_strerror().
 *
 *********************************************************************/
int pcrs_execute_list(pcrs_job *joblist, char *subject, size_t subject_length, char **result, size_t *result_length)
{
   pcrs_job *job;
   char *old, *new = NULL;
   int hits, total_hits;

   old = subject;
   *result_length = subject_length;
   total_hits = 0;

   for (job = joblist; job != NULL; job = job->next)
   {
      hits = pcrs_execute(job, old, *result_length, &new, result_length);

      if (old != subject) free(old);

      if (hits < 0)
      {
         return(hits);
      }
      else
      {
         total_hits += hits;
         old = new;
      }
   }

   *result = new;
   return(total_hits);

}


/*********************************************************************
 *
 * Function    :  pcrs_execute
 *
 * Description :  Apply the regular substitution defined by the job to the
 *                subject.
 *                The subject itself is left untouched, memory for the result
 *                is malloc()ed and it is the caller's responsibility to free
 *                the result when it's no longer needed.
 *
 *                Note: For convenient string handling, a null byte is
 *                      appended to the result. It does not count towards the
 *                      result_length, though.
 *
 * Parameters  :
 *          1  :  job = the pcrs_job to be executed
 *          2  :  subject = the subject (== original) string
 *          3  :  subject_length = the subject's length
 *          4  :  result = char** for returning the result (NULL on error)
 *          5  :  result_length = size_t* for returning the result's length
 *
 * Returns     :  On success, the number of substitutions that were made.
 *                 May be > 1 if job->flags contained PCRS_GLOBAL
 *                On failure, the (negative) pcre error code describing the
 *                 failure, which may be translated to text using pcrs_strerror().
 *
 *********************************************************************/
int pcrs_execute(pcrs_job *job, const char *subject, size_t subject_length, char **result, size_t *result_length)
{
   int offsets[3 * PCRS_MAX_SUBMATCHES],
       offset,
       i, k,
       matches_found,
       submatches,
       max_matches = PCRS_MAX_MATCH_INIT;
   size_t newsize;
   pcrs_match *matches, *dummy;
   char *result_offset;

   offset = i = 0;
   *result = NULL;

   /*
    * Sanity check & memory allocation
    */
   if (job == NULL || job->pattern == NULL || job->substitute == NULL || NULL == subject)
   {
      return(PCRS_ERR_BADJOB);
   }

   if (NULL == (matches = (pcrs_match *)malloc((size_t)max_matches * sizeof(pcrs_match))))
   {
      return(PCRS_ERR_NOMEM);
   }
   memset(matches, '\0', (size_t)max_matches * sizeof(pcrs_match));


   /*
    * Find the pattern and calculate the space
    * requirements for the result
    */
   newsize = subject_length;

   while ((submatches = pcre_exec(job->pattern, job->hints, subject, (int)subject_length, offset, 0, offsets, 3 * PCRS_MAX_SUBMATCHES)) > 0)
   {
      job->flags |= PCRS_SUCCESS;
      matches[i].submatches = submatches;

      for (k = 0; k < submatches; k++)
      {
         matches[i].submatch_offset[k] = offsets[2 * k];

         /* Note: Non-found optional submatches have length -1-(-1)==0 */
         matches[i].submatch_length[k] = (size_t)(offsets[2 * k + 1] - offsets[2 * k]);

         /* reserve mem for each submatch as often as it is ref'd */
         newsize += matches[i].submatch_length[k] * (size_t)job->substitute->backref_count[k];
      }
      /* plus replacement text size minus match text size */
      newsize += job->substitute->length - matches[i].submatch_length[0];

      /* chunk before match */
      matches[i].submatch_offset[PCRS_MAX_SUBMATCHES] = 0;
      matches[i].submatch_length[PCRS_MAX_SUBMATCHES] = (size_t)offsets[0];
      newsize += (size_t)offsets[0] * (size_t)job->substitute->backref_count[PCRS_MAX_SUBMATCHES];

      /* chunk after match */
      matches[i].submatch_offset[PCRS_MAX_SUBMATCHES + 1] = offsets[1];
      matches[i].submatch_length[PCRS_MAX_SUBMATCHES + 1] = subject_length - (size_t)offsets[1] - 1;
      newsize += (subject_length - (size_t)offsets[1]) * (size_t)job->substitute->backref_count[PCRS_MAX_SUBMATCHES + 1];

      /* Storage for matches exhausted? -> Extend! */
      if (++i >= max_matches)
      {
         max_matches = (int)(max_matches * PCRS_MAX_MATCH_GROW);
         if (NULL == (dummy = (pcrs_match *)realloc(matches, (size_t)max_matches * sizeof(pcrs_match))))
         {
            free(matches);
            return(PCRS_ERR_NOMEM);
         }
         matches = dummy;
      }

      /* Non-global search or limit reached? */
      if (!(job->flags & PCRS_GLOBAL)) break;

      /* Don't loop on empty matches */
      if (offsets[1] == offset)
         if ((size_t)offset < subject_length)
            offset++;
         else
            break;
      /* Go find the next one */
      else
         offset = offsets[1];
   }
   /* Pass pcre error through if (bad) failure */
   if (submatches < PCRE_ERROR_NOMATCH)
   {
      free(matches);
      return submatches;
   }
   matches_found = i;


   /*
    * Get memory for the result (must be freed by caller!)
    * and append terminating null byte.
    */
   if ((*result = (char *)malloc(newsize + 1)) == NULL)
   {
      free(matches);
      return PCRS_ERR_NOMEM;
   }
   else
   {
      (*result)[newsize] = '\0';
   }


   /*
    * Replace
    */
   offset = 0;
   result_offset = *result;

   for (i = 0; i < matches_found; i++)
   {
      /* copy the chunk preceding the match */
      memcpy(result_offset, subject + offset, (size_t)(matches[i].submatch_offset[0] - offset));
      result_offset += matches[i].submatch_offset[0] - offset;

      /* For every segment of the substitute.. */
      for (k = 0; k <= job->substitute->backrefs; k++)
      {
         /* ...copy its text.. */
         memcpy(result_offset, job->substitute->text + job->substitute->block_offset[k], job->substitute->block_length[k]);
         result_offset += job->substitute->block_length[k];

         /* ..plus, if it's not the last chunk, i.e.: There *is* a backref.. */
         if (k != job->substitute->backrefs
             /* ..in legal range.. */
             && job->substitute->backref[k] < PCRS_MAX_SUBMATCHES + 2
             /* ..and referencing a real submatch.. */
             && job->substitute->backref[k] < matches[i].submatches
             /* ..that is nonempty.. */
             && matches[i].submatch_length[job->substitute->backref[k]] > 0)
         {
            /* ..copy the submatch that is ref'd. */
            memcpy(
               result_offset,
               subject + matches[i].submatch_offset[job->substitute->backref[k]],
               matches[i].submatch_length[job->substitute->backref[k]]
            );
            result_offset += matches[i].submatch_length[job->substitute->backref[k]];
         }
      }
      offset =  matches[i].submatch_offset[0] + (int)matches[i].submatch_length[0];
   }

   /* Copy the rest. */
   memcpy(result_offset, subject + offset, subject_length - (size_t)offset);

   *result_length = newsize;
   free(matches);
   return matches_found;

}


/*********************************************************************
 *
 * Function    :  is_hex_sequence
 *
 * Description :  Checks the first four characters of a string
 *                and decides if they are a valid hex sequence
 *                (like '\x40').
 *
 * Parameters  :
 *          1  :  sequence = The string to check
 *
 * Returns     :  Non-zero if it's valid sequence, or
 *                Zero if it isn't.
 *
 *********************************************************************/
static int is_hex_sequence(const char *sequence)
{
   return (sequence[0] == '\\' &&
           sequence[1] == 'x'  &&
           is_hex_digit(sequence[2]) &&
           is_hex_digit(sequence[3]));
}

/*********************************************************************
 *
 * Function    :  xdtoi
 *
 * Description :  Converts a single hex digit to an integer.
 *
 * Parameters  :
 *          1  :  d = in the range of ['0'..'9', 'A'..'F', 'a'..'f']
 *
 * Returns     :  The integer value, or -1 for non-hex characters.
 *
 *********************************************************************/
static int xdtoi(const int d)
{
   if ((d >= '0') && (d <= '9'))
   {
      return(d - '0');
   }
   else if ((d >= 'a') && (d <= 'f'))
   {
      return(d - 'a' + 10);
   }
   else if ((d >= 'A') && (d <= 'F'))
   {
      return(d - 'A' + 10);
   }
   else
   {
      return(-1);
   }
}

/*********************************************************************
 *
 * Function    :  xtoi
 *
 * Description :  Hex string to integer conversion.
 *
 * Parameters  :
 *          1  :  s = a 2 digit hex string (e.g. "1f").  Only the
 *                    first two characters will be looked at.
 *
 * Returns     :  The integer value, or 0 for non-hex strings.
 *
 *********************************************************************/
static int xtoi(const char *s)
{
   int d1;

   d1 = xdtoi(*s);
   if (d1 >= 0)
   {
      int d2 = xdtoi(*(s+1));
      if (d2 >= 0)
      {
         return (d1 << 4) + d2;
      }
   }

   return 0;
}
