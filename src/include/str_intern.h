
/* A string intern table.
 * 
 * The intern table holds a list of strings.  Given a new string, we
 * either str_dup it and add it to the table or return a ref to the
 * existing copy of the string from the table if present.
 *
 * This implementation has one big intern table that's designed to be
 * used during db load then freed all at once.  It might be
 * interesting to intern all strings even during runtime.  Somebody
 * else can do this.
 * */

#ifndef Str_Intern_h
#define Str_Intern_h

/* 0 for a default size */
extern void str_intern_open(int table_size);
extern void str_intern_close(void);

/* Make an immutable copy of s.  If there's an intern table open,
   possibly share storage. */
extern const char *str_intern(const char *s);

#endif
