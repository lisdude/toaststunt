/*
 * file i/o server modification
 * Based on File Utilities Package (FIO) v1.5
 */

#define FILE_IO 1
#define _WITH_GETLINE 1

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
/* some things are not defined in stdio on all systems -- AAB 06/03/97 */
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include "structures.h"
#include "bf_register.h"
#include "functions.h"
#include "list.h"
#include "storage.h"
#include "utils.h"
#include "streams.h"
#include "server.h"
#include "network.h"
#include <unordered_map>
#include "tasks.h"
#include "log.h"
#include "fileio.h"

/******************************************************
 * Module-internal data structures
 *****************************************************/

/*
 *  File types are either TEXT or BINARY
 */

typedef struct fileio_file_type *file_type;

struct fileio_file_type {
    const char* (*in_filter)(const char *data, int buflen);
    const char* (*out_filter)(const char *data, int *buflen);
};

file_type file_type_binary = nullptr;
file_type file_type_text = nullptr;

#define FILE_O_READ       1
#define FILE_O_WRITE      2
#define FILE_O_FLUSH      4

typedef unsigned char file_mode;

typedef struct file_handle file_handle;

struct file_handle {
    char  valid;               /* Is this a valid entry?   */
    char *name;                /* pathname of the file     */
    file_type type;            /* text or binary, sir?     */
    file_mode mode;            /* readin', writin' or both */
    FILE  *file;               /* the actual file handle   */
};

typedef struct line_buffer line_buffer;

struct line_buffer {
    char *line;
    struct line_buffer *next;
};

/***************************************************************
 * File <-> FHANDLE descriptor table interface
 ***************************************************************/


static std::unordered_map <Num, file_handle> file_table;
static Num next_handle = 1;

static char file_handle_valid(Var fhandle) {
    if (fhandle.type != TYPE_INT)
        return 0;
    
    Num i = fhandle.v.num;
    
    if ((i < 0) || (i >= next_handle))
        return 0;
    
    if (file_table.count(i) == 0)
        return 0;
    
    return file_table[i].valid;
}

static FILE *file_handle_file(Var fhandle) {
    Num i = fhandle.v.num;
    return file_table[i].file;
}

static const char *file_handle_name(Var fhandle) {
    Num i = fhandle.v.num;
    return file_table[i].name;
}

static file_type file_handle_type(Var fhandle) {
    Num i = fhandle.v.num;
    return file_table[i].type;
}

static file_mode file_handle_mode(Var fhandle) {
    Num i = fhandle.v.num;
    return file_table[i].mode;
}

static void file_handle_destroy(Var fhandle) {
    Num i = fhandle.v.num;
    free_str(file_table[i].name);
    file_table.erase(i);
    if (file_table.size() == 0)
        next_handle = 1;
}

static Num file_allocate_next_handle(void) {
    while (file_table.count(next_handle) != 0)
        next_handle++;

    return next_handle;
}

static Var file_handle_new(const char *name, file_type type, file_mode mode) {
    Num handle = file_allocate_next_handle();

    if (file_table.size() >= server_int_option("file_io_max_files", FILE_IO_MAX_FILES))
        handle = -1;

    Var r;
    r.type = TYPE_INT;
    r.v.num = handle;

    if (handle >= 0) {
        file_handle file;
        file.valid = 1;
        file.name = str_dup(name);
        file.type = type;
        file.mode = mode;
        file.file = nullptr;
        file_table[handle] = file;
        next_handle++;
    }

    return r;
}

static void file_handle_set_file(Var fhandle, FILE *f) {
    Num i = fhandle.v.num;
    file_table[i].file = f;
}


/***************************************************************
 * Interface for modestrings
 ***************************************************************/

/*
 *  Convert modestring to settings for type and mode.
 *  Returns pointer to stdio modestring if successfull and
 *  NULL if not.
 */

static const char *file_modestr_to_mode(const char *s, file_type *type, file_mode *mode) {
    static char buffer[4] = {0, 0, 0, 0};
    int p = 0;
    file_type t;
    file_mode m = 0;

    if (!file_type_binary) {
        file_type_binary = (struct fileio_file_type *)mymalloc(sizeof(struct fileio_file_type), M_STRING);
        file_type_text = (struct fileio_file_type *)mymalloc(sizeof(struct fileio_file_type), M_STRING);
        file_type_binary->in_filter = raw_bytes_to_binary;
        file_type_binary->out_filter = binary_to_raw_bytes;
        file_type_text->in_filter = raw_bytes_to_clean;
        file_type_text->out_filter = clean_to_raw_bytes;
    }

    if (strlen(s) != 4)
        return nullptr;

    if (s[0] == 'r')           m |= FILE_O_READ;
    else if (s[0] == 'w')      m |= FILE_O_WRITE;
    else if (s[0] == 'a')      m |= FILE_O_WRITE;
    else
        return nullptr;

    buffer[p++] = s[0];

    if (s[1] == '+') {
        m |= (s[0] == 'r') ? FILE_O_WRITE : FILE_O_READ;
        buffer[p++] = '+';
    } else if (s[1] != '-') {
        return nullptr;
    }

    if (s[2] == 't')            t = file_type_text;
    else if (s[2] == 'b') {
        t = file_type_binary;
        buffer[p++] = 'b';
    } else
        return nullptr;

    if (s[3] == 'f')            m |= FILE_O_FLUSH;
    else if (s[3] != 'n')
        return nullptr;

    *type = t;    *mode = m;
    buffer[p] = 0;
    return buffer;
}


/***************************************************************
 * Various error handlers
 ***************************************************************/

static package
file_make_error(const char *errtype, const char *msg) {
    package p;
    Var value;

    value.type = TYPE_STR;
    value.v.str = str_dup(errtype);

    p.kind = package::BI_RAISE;
    p.u.raise.code.type = TYPE_ERR;
    p.u.raise.code.v.err = E_FILE;
    p.u.raise.msg = str_dup(msg);
    p.u.raise.value = value;

    return p;
}

static package file_raise_errno(const char *value_str) {
    char *strerr;

    if (errno) {
        strerr = strerror(errno);
        return file_make_error(value_str, strerr);
    }  else {
        return file_make_error("End of file", "End of file");
    }

}

static package file_raise_notokcall(const char *funcid, Objid progr) {
    return make_error_pack(E_PERM);
}

static package file_raise_notokfilename(const char *funcid, const char *pathname) {
    Var p;

    p.type = TYPE_STR;
    p.v.str = str_dup(pathname);
    return make_raise_pack(E_INVARG, "Invalid pathname", p);
}


/***************************************************************
 * Security verification
 ***************************************************************/

static int file_verify_caller(Objid progr) {
    return is_wizard(progr);
}

static int file_verify_path(const char *pathname) {
    /*
    *  A pathname is OK does not contain a
     *  any of instances the substring "/."
     */

    if (pathname[0] == '\0')
        return 1;

    if ((strlen(pathname) > 1) && (pathname[0] == '.') && (pathname[1] == '.'))
        return 0;

    if (strindex(pathname, strlen(pathname), "/.", 2, 0))
        return 0;

    return 1;
}


/***************************************************************
 * Common code for FHANDLE-using functions
 **************************************************************/

static FILE *file_handle_file_safe(Var handle) {
    if (!file_handle_valid(handle))
        return nullptr;
    else
        return file_handle_file(handle);
}

static const char *file_handle_name_safe(Var handle) {
    if (!file_handle_valid(handle))
        return nullptr;
    else
        return file_handle_name(handle);
}


/***************************************************************
 * Common code for file opening functions
 ***************************************************************/

const char *file_resolve_path(const char *pathname) {
    static Stream *s = nullptr;

    if (!s)
        s = new_stream(strlen(pathname) + strlen(file_subdir) + 1);

    if (!file_verify_path(pathname))
        return nullptr;

    stream_add_string(s, file_subdir);
    if (pathname[0] == '/')
        stream_add_string(s, pathname + 1);
    else
        stream_add_string(s, pathname);

    return reset_stream(s);

}


/***************************************************************
 * Built in functions

/***************************************************************
 * File open and close.
 ***************************************************************/


/*
 * FHANDLE file_open(STR name, STR mode)
 */

static package
bf_file_open(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle;
    const char *real_filename;
    const char *filename = arglist.v.list[1].v.str;
    const char *mode = arglist.v.list[2].v.str;
    const char *fmode;
    file_mode rmode;
    file_type type;
    FILE *f;

    errno = 0;

    if (!file_verify_caller(progr))
        r = file_raise_notokcall("file_open", progr);
    else if ((real_filename = file_resolve_path(filename)) == nullptr)
        r = file_raise_notokfilename("file_open", filename);
    else if ((fmode = file_modestr_to_mode(mode, &type, &rmode)) == nullptr)
        r = make_raise_pack(E_INVARG, "Invalid mode string", var_ref(arglist.v.list[2]));
    else if ((fhandle = file_handle_new(filename, type, rmode)).v.num < 0)
        r = make_raise_pack(E_QUOTA, "Too many files open", zero);
    else if ((f = fopen(real_filename, fmode)) == nullptr) {
        file_handle_destroy(fhandle);
        r = file_raise_errno("file_open");
    } else {
        /* phew, we actually got a successfull open */
        file_handle_set_file(fhandle, f);
        r = make_var_pack(fhandle);
    }
    free_var(arglist);
    return r;
}

/*
 * void file_close(FHANDLE handle);
 */

static package
bf_file_close(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    FILE *f;

    errno = 0;

    if (!file_verify_caller(progr))
        r = file_raise_notokcall("file_close", progr);
    else if ((f = file_handle_file_safe(fhandle)) == nullptr)
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    else {
        fclose(f);
        file_handle_destroy(fhandle);
        r = no_var_pack();
    }
    free_var(arglist);
    return r;
}

/*
 * STR file_name(FHANDLE handle)
 */

static package
bf_file_name(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    const char *name;
    Var rv;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_name", progr);
    } else if ((name = file_handle_name_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else {
        rv.type = TYPE_STR;
        rv.v.str = str_dup(name);
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

static package
bf_file_openmode(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    char buffer[5] = {0, 0, 0, 0, 0};
    file_mode mode;
    file_type type;
    Var rv;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_name", progr);
    } else if (!file_handle_valid(fhandle)) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else {
        type = file_handle_type(fhandle);
        mode = file_handle_mode(fhandle);
        if (mode & FILE_O_READ) {
            buffer[0] = 'r';
        } else if (mode & FILE_O_WRITE) {
            buffer[0] = 'w';
        }
        if (mode & (FILE_O_READ | FILE_O_WRITE))
            buffer[1] = '+';
        else
            buffer[1] = '-';

        if (type == file_type_binary)
            buffer[2] = 'b';
        else
            buffer[2] = 't';

        if (mode & FILE_O_FLUSH)
            buffer[3] = 'f';
        else
            buffer[3] = 'n';


        rv.type = TYPE_STR;
        rv.v.str = str_dup(buffer);
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}


/**********************************************************
 * string (line-based) i/o
 **********************************************************/

/*
 * common functionality of file_readline and file_readlines
 */

static const char *file_get_line(Var fhandle, int *count)
{
    FILE *fp = file_handle_file(fhandle);

    ssize_t read = getline(&line_read, &line_size, fp);

    if (read == -1)
    {
        *count = 0;
        return nullptr;
    }

    *count = read;
    return line_read;
}


/*
 * STR file_readline(FHANDLE handle)
 */

static package
bf_file_readline(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    Var rv;
    int len;
    file_mode mode;
    const char *line;

    errno = 0;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_readline", progr);
    } else if (!file_handle_valid(fhandle)) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else if (!((mode = file_handle_mode(fhandle)) & FILE_O_READ))
        r = make_raise_pack(E_INVARG, "File is open write-only", var_ref(fhandle));
    else {
        if ((line = file_get_line(fhandle, &len)) == nullptr)
            r = file_raise_errno("readline");
        else {
            rv.type = TYPE_STR;
#ifndef UNSAFE_FIO
            file_type type = file_handle_type(fhandle);
            rv.v.str = str_dup((type->in_filter)(line, len));
#else
            /* For good reason, you can't modify a const char. But shh, we're doing it anyway. */
            char *dirty_hack = (char*)line;
            dirty_hack[len - 1] = '\0';
            rv.v.str = str_dup(dirty_hack);
#endif
            r = make_var_pack(rv);
        }
    }
    free_var(arglist);
    return r;
}

/*
 * STR file_readlines(FHANDLE handle, INT start, INT end)
 */

static void free_line_buffer(line_buffer *head, int strings_too) {
    line_buffer *next;
    if (head) {
        next = head->next;
        myfree(head, M_STRUCT);
        head = next;
        while (head != nullptr) {
            next = head->next;
            if (strings_too)
                free_str(head->line);
            myfree(head, M_STRUCT);
            head = next;
        }
    }
}

static line_buffer *new_line_buffer(char *line) {
    line_buffer *p = (line_buffer *)mymalloc(sizeof(line_buffer), M_STRUCT);
    p->line = line;
    p->next = nullptr;
    return p;
}

static package
bf_file_readlines(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    Num begin = arglist.v.list[2].v.num;
    Num end   = arglist.v.list[3].v.num;
    Num begin_loc = 0, linecount = 0;
    file_mode mode;
    Var rv;
    int current_line = 0, len = 0, i = 0;
    const char *line = nullptr;
    FILE *f;
    line_buffer *linebuf_head = nullptr, *linebuf_cur = nullptr;

    errno = 0;

    if ((begin < 1) || (begin > end))
        return make_error_pack(E_INVARG);
    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_readlines", progr);
    } else if ((f = file_handle_file_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else if (!((mode = file_handle_mode(fhandle)) & FILE_O_READ))
        r = make_raise_pack(E_INVARG, "File is open write-only", var_ref(fhandle));
    else {
#ifndef UNSAFE_FIO
        file_type type = file_handle_type(fhandle); /* Quiet warning */
#endif
        /* Back to the beginning ... */
        rewind(f);

        /* "seek" to that line */
        begin--;
        while ((current_line != begin)
                && ((line = file_get_line(fhandle, &len)) != nullptr))
            current_line++;

        if (((begin != 0) && (line == nullptr)) || ((begin_loc = ftell(f)) == -1))
            r = file_raise_errno("read_line");
        else {
            /*
             * now that we have where to begin, it's time to slurp lines
             * and seek to EOF or to the end_line, whichever comes first
             */

            linebuf_head = linebuf_cur = new_line_buffer(nullptr);

            while ((current_line != end)
                    && ((line = file_get_line(fhandle, &len)) != nullptr)) {
#ifndef UNSAFE_FIO
                linebuf_cur->next = new_line_buffer(str_dup((type->in_filter)(line, len)));
#else
                /* For good reason, you can't modify a const char. But shh, we're doing it anyway. */
                char *dirty_hack = (char*)line;
                if (dirty_hack[len - 1] == '\n')
                    dirty_hack[len - 1] = '\0';
                else
                    dirty_hack[len] = '\0';
                linebuf_cur->next = new_line_buffer(str_dup(dirty_hack));
#endif
                linebuf_cur = linebuf_cur->next;

                current_line++;
            }
            linecount =  current_line - begin;

            linebuf_cur = linebuf_head->next;

            if (fseek(f, begin_loc, SEEK_SET) == -1) {
                free_line_buffer(linebuf_head, 1);
                r = file_raise_errno("seeking");
            } else {
                rv = new_list(linecount);
                i = 1;
                while (linebuf_cur != nullptr) {
                    rv.v.list[i].type = TYPE_STR;
                    rv.v.list[i].v.str = linebuf_cur->line;
                    linebuf_cur = linebuf_cur->next;
                    i++;
                }
                free_line_buffer(linebuf_head, 0);
                r = make_var_pack(rv);
            }
        }
    }

    free_var(arglist);
    return r;
}

/*
 * void file_writeline(FHANDLE handle, STR line)
 */

static package
bf_file_writeline(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    const char *buffer = arglist.v.list[2].v.str;
    const char *rawbuffer;
    file_mode mode;
    file_type type;
    int len;
    FILE *f;

    errno = 0;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_writeline", progr);
    } else if ((f = file_handle_file_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else if (!((mode = file_handle_mode(fhandle)) & FILE_O_WRITE))
        r = make_raise_pack(E_INVARG, "File is open read-only", var_ref(fhandle));
    else {
        type = file_handle_type(fhandle);
        if ((rawbuffer = (type->out_filter)(buffer, &len)) == nullptr)
            r = make_raise_pack(E_INVARG, "Invalid binary string", var_ref(fhandle));
        else if ((fputs(rawbuffer, f) == EOF) || (fputc('\n', f) != '\n'))
            r = file_raise_errno(file_handle_name(fhandle));
        else {
            if (mode & FILE_O_FLUSH) {
                fflush(f);
            }
            r = no_var_pack();
        }
    }
    free_var(arglist);
    return r;
}

/********************************************************
 * binary i/o
 ********************************************************/

/*
 * STR file_read(FHANDLE handle, INT record_length)
 */

static package
bf_file_read(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;

    Var fhandle = arglist.v.list[1];
    file_mode mode;
    file_type type;
    Num record_length = arglist.v.list[2].v.num;
    Num read_length;

    char buffer[FILE_IO_BUFFER_LENGTH];

    Var rv;

    static Stream *str = nullptr;
    int len = 0, read = 0;

    FILE *f;

    errno = 0;

    read_length = (record_length > sizeof(buffer)) ? sizeof(buffer) : record_length;

    if (str == nullptr)
        str = new_stream(FILE_IO_BUFFER_LENGTH);

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_read", progr);
    } else if ((f = file_handle_file_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else if (!((mode = file_handle_mode(fhandle)) & FILE_O_READ))
        r = make_raise_pack(E_INVARG, "File is open write-only", var_ref(fhandle));
    else {
        type = file_handle_type(fhandle);

try_again:
        read = fread(buffer, sizeof(char), MIN(read_length, (record_length - len)), f);
        if (!read && !len) {
            /*
             * No more to read.  This is only an error if nothing
             * has been read so far.
             *
             */
            r = file_raise_errno(file_handle_name(fhandle));
        } else if (read && ((len += read) < record_length)) {
            /*
             * We got something this time, but it isn't enough.
             */
            stream_add_string(str, (type->in_filter)(buffer, read));
            read = 0;
            goto try_again;
        } else {
            /*
             * We didn't get anything last time, but we have something already
             * OR
             * We got everything we need.
             */

            stream_add_string(str, (type->in_filter)(buffer, read));

            rv.type = TYPE_STR;
            rv.v.str = str_dup(reset_stream(str));

            r = make_var_pack(rv);
        }
    }
    free_var(arglist);
    return r;
}

/*
 * void file_flush(FHANDLE handle)
 */

static package
bf_file_flush(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    FILE *f;

    errno = 0;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_flush", progr);
    } else if ((f = file_handle_file_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else {
        if (fflush(f))
            r = file_raise_errno("flushing");
        else
            r = no_var_pack();
    }
    free_var(arglist);
    return r;
}


/*
 * INT file_write(FHANDLE fh, STR data)
 */

static package
bf_file_write(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1], rv;
    const char *buffer = arglist.v.list[2].v.str;
    const char *rawbuffer;
    file_mode mode;
    file_type type;
    int len;
    int written;
    FILE *f;

    errno = 0;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_write", progr);
    } else if ((f = file_handle_file_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else if (!((mode = file_handle_mode(fhandle)) & FILE_O_WRITE))
        r = make_raise_pack(E_INVARG, "File is open read-only", var_ref(fhandle));
    else {
        type = file_handle_type(fhandle);
        if ((rawbuffer = (type->out_filter)(buffer, &len)) == nullptr)
            r = make_raise_pack(E_INVARG, "Invalid binary string", var_ref(fhandle));
        else if (!(written = fwrite(rawbuffer, sizeof(char), len, f)))
            r = file_raise_errno(file_handle_name(fhandle));
        else {
            if (mode & FILE_O_FLUSH)
                fflush(f);
            rv.type = TYPE_INT;
            rv.v.num = written;
            r = make_var_pack(rv);
        }
    }
    free_var(arglist);
    return r;
}


/************************************************
 * navigating the file
 ************************************************/

/*
 * void file_seek(FHANDLE handle, FLOC location, STR whence)
 * whence in {"SEEK_SET", "SEEK_CUR", "SEEK_END"}
 */

static package
bf_file_seek(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    Num seek_to = arglist.v.list[2].v.num;
    const char *whence = arglist.v.list[3].v.str;
    int whnce = 0, whence_ok = 1;
    FILE *f;

    errno = 0;

    if (!strcasecmp(whence, "SEEK_SET"))
        whnce = SEEK_SET;
    else if (!strcasecmp(whence, "SEEK_CUR"))
        whnce = SEEK_CUR;
    else if (!strcasecmp(whence, "SEEK_END"))
        whnce = SEEK_END;
    else
        whence_ok = 0;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_seek", progr);
    } else if ((f = file_handle_file_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else if (!whence_ok) {
        r = make_raise_pack(E_INVARG, "Invalid whence", zero);
    } else {
        if (fseek(f, seek_to, whnce))
            r = file_raise_errno(file_handle_name(fhandle));
        else
            r = no_var_pack();
    }
    free_var(arglist);
    return r;
}

/*
 * FLOC file_tell(FHANDLE handle)
 */

static package
bf_file_tell(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    Var rv;
    FILE *f;

    errno = 0;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_tell", progr);
    } else if ((f = file_handle_file_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else {
        rv.type = TYPE_INT;
        if ((rv.v.num = ftell(f)) < 0)
            r = file_raise_errno(file_handle_name(fhandle));
        else
            r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

/*
 * INT file_eof(FHANDLE handle)
 */

static package
bf_file_eof(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    Var rv;
    FILE *f;

    errno = 0;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_eof", progr);
    } else if ((f = file_handle_file_safe(fhandle)) == nullptr) {
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    } else {
        rv.type = TYPE_INT;
        rv.v.num = feof(f);
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

/*****************************************************************
 * Functions that stat()
 *****************************************************************/

/*
 * (internal) int(statok) file_stat(Var filespec, package *r, struct stat *buf)
 */

static int file_stat(Objid progr, Var filespec, package *r, struct stat *buf) {
    int statok = 0;

    if (!file_verify_caller(progr)) {
        *r = file_raise_notokcall("file_stat", progr);
    } else if (filespec.type == TYPE_STR) {
        const char *filename = filespec.v.str;
        const char *real_filename;

        if ((real_filename = file_resolve_path(filename)) == nullptr) {
            *r =  file_raise_notokfilename("file_stat", filename);
        } else {
            if (stat(real_filename, buf) != 0)
                *r = file_raise_errno(filename);
            else {
                statok = 1;
            }
        }
    } else {
        FILE *f;
        if ((f = file_handle_file_safe(filespec)) == nullptr)
            * r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(filespec));
        else {
            if (fstat(fileno(f), buf) != 0)
                *r = file_raise_errno(file_handle_name(filespec));
            else {
                statok = 1;
            }
        }
    }
    return statok;
}

static const char *file_type_string(mode_t st_mode) {
    if (S_ISREG(st_mode))
        return "reg";
    else if (S_ISDIR(st_mode))
        return "dir";
    else if (S_ISFIFO(st_mode))
        return "fifo";
    else if (S_ISBLK(st_mode))
        return "block";
    else if (S_ISSOCK(st_mode))
        return "socket";
    else
        return "unknown";
}

static const char *file_mode_string(mode_t st_mode) {
    static Stream *s = nullptr;
    if (!s)
        s = new_stream(4);
    stream_printf(s, "%03o", st_mode & 0777);
    return reset_stream(s);
}

/*
 * INT file_size(STR filename)
 * INT file_size(FHANDLE fh)
 */

static package
bf_file_size(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var     rv;
    Var filespec = arglist.v.list[1];
    struct stat buf;

    if (file_stat(progr, filespec, &r, &buf)) {
        rv.type = TYPE_INT;
        rv.v.num = buf.st_size;
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

/*
 * STR file_mode(STR filename)
 * STR file_mode(FHANDLE fh)
 */

static package
bf_file_mode(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var     rv;
    Var filespec = arglist.v.list[1];
    struct stat buf;

    if (file_stat(progr, filespec, &r, &buf)) {
        rv.type = TYPE_STR;
        rv.v.str = str_dup(file_mode_string(buf.st_mode));
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

/*
 * STR file_type(STR filename)
 * STR file_type(FHANDLE fh)
 */

static package
bf_file_type(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var     rv;
    Var filespec = arglist.v.list[1];
    struct stat buf;

    if (file_stat(progr, filespec, &r, &buf)) {
        rv.type = TYPE_STR;
        rv.v.str = str_dup(file_type_string(buf.st_mode));
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

/*
 * INT file_last_access(STR filename)
 * INT file_last_access(FHANDLE fh)
 */

static package
bf_file_last_access(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var     rv;
    Var filespec = arglist.v.list[1];
    struct stat buf;

    if (file_stat(progr, filespec, &r, &buf)) {
        rv.type = TYPE_INT;
        rv.v.num = buf.st_atime;
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

/*
 * INT file_last_modify(STR filename)
 * INT file_last_modify(FHANDLE fh)
 */

static package
bf_file_last_modify(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var     rv;
    Var filespec = arglist.v.list[1];
    struct stat buf;

    if (file_stat(progr, filespec, &r, &buf)) {
        rv.type = TYPE_INT;
        rv.v.num = buf.st_mtime;
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

/*
 * INT file_last_change(STR filename)
 * INT file_last_change(FHANDLE fh)
 */

static package
bf_file_last_change(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var     rv;
    Var filespec = arglist.v.list[1];
    struct stat buf;

    if (file_stat(progr, filespec, &r, &buf)) {
        rv.type = TYPE_INT;
        rv.v.num = buf.st_ctime;
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}

/*
 * INT file_stat(STR filename)
 * INT file_stat(FHANDLE fh)
 */

static package
bf_file_stat(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var     rv;
    Var filespec = arglist.v.list[1];
    struct stat buf;

    if (file_stat(progr, filespec, &r, &buf)) {
        rv = new_list(8);
        rv.v.list[1].type = TYPE_INT;
        rv.v.list[1].v.num = buf.st_size;
        rv.v.list[2].type = TYPE_STR;
        rv.v.list[2].v.str = str_dup(file_type_string(buf.st_mode));
        rv.v.list[3].type = TYPE_STR;
        rv.v.list[3].v.str = str_dup(file_mode_string(buf.st_mode));
        rv.v.list[4].type = TYPE_STR;
        rv.v.list[4].v.str = str_dup("");
        rv.v.list[5].type = TYPE_STR;
        rv.v.list[5].v.str = str_dup("");
        rv.v.list[6].type = TYPE_INT;
        rv.v.list[6].v.num = buf.st_atime;
        rv.v.list[7].type = TYPE_INT;
        rv.v.list[7].v.num = buf.st_mtime;
        rv.v.list[8].type = TYPE_INT;
        rv.v.list[8].v.num = buf.st_ctime;
        r = make_var_pack(rv);
    }
    free_var(arglist);
    return r;
}


/*****************************************************************
 * Housekeeping functions
 *****************************************************************/

/*
 * LIST file_list(STR pathname, [ANY detailed])
 */

static int file_list_select(const struct dirent *d) {
    const char *name = d->d_name;
    int l = strlen(name);
    if ((l == 1) && (name[0] == '.'))
        return 0;
    else if ((l == 2) && (name[0] == '.') && (name[1] == '.'))
        return 0;
    else
        return 1;
}

static package
bf_file_list(Var arglist, Byte next, void *vdata, Objid progr)
{
    /* modified to use opendir/readdir which is slightly more "standard"
       than the original scandir method.   -- AAB 06/03/97
     */
    package r;
    const char *pathspec = arglist.v.list[1].v.str;
    const char *real_pathname;
    int   detailed = (arglist.v.list[0].v.num > 1
                      ? is_true(arglist.v.list[2])
                      : 0);

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_list", progr);
    } else if ((real_pathname = file_resolve_path(pathspec)) == nullptr) {
        r =  file_raise_notokfilename("file_list", pathspec);
    } else {
        DIR *curdir;
        Stream *s = new_stream(64);
        int failed = 0;
        struct stat buf;
        Var     rv, detail;
        struct dirent *curfile;

        if (!(curdir = opendir (real_pathname)))
            r = file_raise_errno(pathspec);
        else {
            rv = new_list(0);
            while ( (curfile = readdir(curdir)) != nullptr ) {
                if (strncmp(curfile->d_name, ".", 2) != 0 && strncmp(curfile->d_name, "..", 3) != 0) {
                    if (detailed) {
                        stream_add_string(s, real_pathname);
                        stream_add_char(s, '/');
                        stream_add_string(s, curfile->d_name);
                        if (stat(reset_stream(s), &buf) != 0) {
                            failed = 1;
                            break;
                        } else {
                            detail = new_list(4);
                            detail.v.list[1].type = TYPE_STR;
                            detail.v.list[1].v.str = str_dup(curfile->d_name);
                            detail.v.list[2].type = TYPE_STR;
                            detail.v.list[2].v.str = str_dup(file_type_string(buf.st_mode));
                            detail.v.list[3].type = TYPE_STR;
                            detail.v.list[3].v.str = str_dup(file_mode_string(buf.st_mode));
                            detail.v.list[4].type = TYPE_INT;
                            detail.v.list[4].v.num = buf.st_size;
                        }
                    } else {
                        detail.type = TYPE_STR;
                        detail.v.str = str_dup(curfile->d_name);
                    }
                    rv = listappend(rv, detail);
                }
            }
            if (failed) {
                free_var(rv);
                r = file_raise_errno(pathspec);
            } else
                r = make_var_pack(rv);
            closedir(curdir);
        }
        free_stream(s);
    }
    free_var(arglist);
    return r;
}


/*
 * void file_mkdir(STR pathname)
 */

static package
bf_file_mkdir(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    const char *pathspec = arglist.v.list[1].v.str;
    const char *real_pathname;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_mkdir", progr);
    } else if ((real_pathname = file_resolve_path(pathspec)) == nullptr) {
        r =  file_raise_notokfilename("file_mkdir", pathspec);
    } else {
        if (mkdir(real_pathname, 0777) != 0)
            r = file_raise_errno(pathspec);
        else
            r = no_var_pack();

    }
    free_var(arglist);
    return r;
}

/*
 * void file_rmdir(STR pathname)
 */

static package
bf_file_rmdir(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    const char *pathspec = arglist.v.list[1].v.str;
    const char *real_pathname;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_rmdir", progr);
    } else if ((real_pathname = file_resolve_path(pathspec)) == nullptr) {
        r =  file_raise_notokfilename("file_rmdir", pathspec);
    } else {
        if (rmdir(real_pathname) != 0)
            r = file_raise_errno(pathspec);
        else
            r = no_var_pack();

    }
    free_var(arglist);
    return r;
}

/*
 * void file_remove(STR pathname)
 */

static package
bf_file_remove(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    const char *pathspec = arglist.v.list[1].v.str;
    const char *real_pathname;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_remove", progr);
    } else if ((real_pathname = file_resolve_path(pathspec)) == nullptr) {
        r =  file_raise_notokfilename("file_remove", pathspec);
    } else {
        if (remove(real_pathname) != 0)
            r = file_raise_errno(pathspec);
        else
            r = no_var_pack();
    }
    free_var(arglist);
    return r;
}

/*
 * void file_rename(STR pathname)
 */

static package
bf_file_rename(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    const char *fromspec = arglist.v.list[1].v.str;
    const char *tospec = arglist.v.list[2].v.str;
    char *real_fromspec = nullptr;
    const char *real_tospec;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_rename", progr);
    } else if ((real_fromspec = str_dup(file_resolve_path(fromspec))) == nullptr) {
        r =  file_raise_notokfilename("file_rename", fromspec);
    } else if ((real_tospec = file_resolve_path(tospec)) == nullptr) {
        r =  file_raise_notokfilename("file_rename", tospec);
    } else {
        if (rename(real_fromspec, real_tospec) != 0)
            r = file_raise_errno("rename");
        else
            r = no_var_pack();
    }
    if (real_fromspec)
        free_str(real_fromspec);
    free_var(arglist);
    return r;
}



/*
 * void file_chmod(STR pathname, STR mode)
 */


static int file_chmodstr_to_mode(const char *modespec, mode_t *newmode) {
    mode_t m = 0;
    int i = 0, fct = 1;

    if (strlen(modespec) != 3)
        return 0;
    else {
        for (i = 2; i >= 0; i--) {
            char c = modespec[i];
            if (!((c  >= '0') && (c <= '7')))
                return 0;
            else {
                m += fct * (c - '0');
            }
            fct *= 8;
        }
    }
    *newmode = m;
    return 1;
}

static package
bf_file_chmod(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    const char *pathspec = arglist.v.list[1].v.str;
    const char *modespec = arglist.v.list[2].v.str;
    mode_t newmode;
    const char *real_filename;

    if (!file_verify_caller(progr)) {
        r = file_raise_notokcall("file_chmod", progr);
    } else if (!file_chmodstr_to_mode(modespec, &newmode)) {
        r = make_raise_pack(E_INVARG, "Invalid mode string", zero);
    } else if ((real_filename = file_resolve_path(pathspec)) == nullptr) {
        r =  file_raise_notokfilename("file_chmod", pathspec);
    } else {
        if (chmod(real_filename, newmode) != 0)
            r = file_raise_errno("chmod");
        else
            r = no_var_pack();
    }
    free_var(arglist);
    return r;
}

static package bf_file_handles(Var arglist, Byte next, void *vdata, Objid progr)
{
    free_var(arglist);

    if (!is_wizard(progr))
        return make_error_pack(E_PERM);

    // We can't use file_table.size() here because it will create space for
    // invalid file handles. Easier just to listappend, I guess.
    Var r = new_list(0);
    std::unordered_map <Num, file_handle>::iterator it;
    for (it = file_table.begin(); it != file_table.end(); it++)
    {
        if (it->second.valid == 1)
            r = listappend(r, Var::new_int(it->first));
    }

    return make_var_pack(r);
}

static package
bf_file_grep(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    int len;
    file_mode mode;
    const char *line = nullptr;
    int line_num = 0;
    int match_all = 0;
    int arg_length = 0;
    Var ret, tmp, tmp_name, tmp_num;

    if (!file_verify_caller(progr))
        r = file_raise_notokcall("file_readline", progr);
    else if (!file_handle_valid(fhandle))
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    else if (!((mode = file_handle_mode(fhandle)) & FILE_O_READ))
        r = make_raise_pack(E_INVARG, "File is open write-only", var_ref(fhandle));
    else
    {
        tmp_name.type = TYPE_STR;
        tmp_num.type = TYPE_INT;
        arg_length = memo_strlen(arglist.v.list[2].v.str);
        ret = new_list(0);

        if (arglist.v.list[0].v.num >= 3 && is_true(arglist.v.list[3]))
            match_all = 1;

        rewind(file_handle_file_safe(fhandle));

        while ((line = file_get_line(fhandle, &len)) != nullptr)
        {
            line_num++;
            if (len > 0 && strindex(line, len, arglist.v.list[2].v.str, arg_length, 0))
            {
                tmp = new_list(0);
                // Have to get rid of the newline, woops
//        line[strcspn(line, "\r\n")] = 0;
                // strcspn is more elegant but slower
                /* For good reason, you can't modify a const char. But shh, we're doing it anyway. */
                char *dirty_hack = (char*)line;
                dirty_hack[strlen(dirty_hack) - 1] = '\0';
                tmp_name.v.str = str_dup(dirty_hack);
                tmp = listappend(tmp, tmp_name);
                tmp_num.v.num = line_num;
                tmp = listappend(tmp, tmp_num);
                ret = listappend(ret, tmp);

                if (!match_all)
                    break;
            }
        }
        r = make_var_pack(ret);
    }
    free_var(arglist);
    return r;
}

/*
 * STR file_count_lines(FHANDLE handle)
 */

static package
bf_file_count_lines(Var arglist, Byte next, void *vdata, Objid progr)
{
    package r;
    Var fhandle = arglist.v.list[1];
    free_var(arglist);
    file_mode mode;
    Var rv;

    if (!file_verify_caller(progr))
        r = file_raise_notokcall("file_readline", progr);
    else if (!file_handle_valid(fhandle))
        r = make_raise_pack(E_INVARG, "Invalid FHANDLE", var_ref(fhandle));
    else if (!((mode = file_handle_mode(fhandle)) & FILE_O_READ))
        r = make_raise_pack(E_INVARG, "File is open write-only", var_ref(fhandle));
    else
    {
        int throwaway = 0;
        FILE *fp = file_handle_file_safe(fhandle);

        rewind(fp);

        long count = 0;
        while (file_get_line(fhandle, &throwaway) != nullptr)
            count++;

        rv.type = TYPE_INT;
        rv.v.num = count;
        r = make_var_pack(rv);
    }

    return r;
}

/************************************************************************/

void
register_fileio(void)
{
#if FILE_IO

    register_function("file_handles", 0, 0, bf_file_handles);

    register_function("file_open", 2, 2, bf_file_open, TYPE_STR, TYPE_STR);
    register_function("file_close", 1, 1, bf_file_close, TYPE_INT);
    register_function("file_name", 1, 1, bf_file_name, TYPE_INT);
    register_function("file_openmode", 1, 1, bf_file_openmode, TYPE_INT);


    register_function("file_readline", 1, 1, bf_file_readline, TYPE_INT);
    register_function("file_readlines", 3, 3, bf_file_readlines, TYPE_INT, TYPE_INT, TYPE_INT);
    register_function("file_writeline", 2, 2, bf_file_writeline, TYPE_INT, TYPE_STR);
    register_function("file_grep", 2, 3, bf_file_grep, TYPE_INT, TYPE_STR, TYPE_INT);

    register_function("file_read", 2, 2, bf_file_read, TYPE_INT, TYPE_INT);
    register_function("file_write", 2, 2, bf_file_write, TYPE_INT, TYPE_STR);
    register_function("file_flush", 1, 1, bf_file_flush, TYPE_INT);


    register_function("file_seek", 3, 3, bf_file_seek, TYPE_INT, TYPE_INT, TYPE_STR);
    register_function("file_tell", 1, 1, bf_file_tell, TYPE_INT);

    register_function("file_eof", 1, 1, bf_file_eof, TYPE_INT);
    register_function("file_count_lines", 1, 1, bf_file_count_lines, TYPE_INT);

    register_function("file_list", 1, 2, bf_file_list, TYPE_STR, TYPE_ANY);
    register_function("file_mkdir", 1, 1, bf_file_mkdir, TYPE_STR);
    register_function("file_rmdir", 1, 1, bf_file_rmdir, TYPE_STR);
    register_function("file_remove", 1, 1, bf_file_remove, TYPE_STR);
    register_function("file_rename", 2, 2, bf_file_rename, TYPE_STR, TYPE_STR);
    register_function("file_chmod", 2, 2, bf_file_chmod, TYPE_STR, TYPE_STR);

    register_function("file_size", 1, 1, bf_file_size, TYPE_ANY);
    register_function("file_mode", 1, 1, bf_file_mode, TYPE_ANY);
    register_function("file_type", 1, 1, bf_file_type, TYPE_ANY);
    register_function("file_last_access", 1, 1, bf_file_last_access, TYPE_ANY);
    register_function("file_last_modify", 1, 1, bf_file_last_modify, TYPE_ANY);
    register_function("file_last_change", 1, 1, bf_file_last_change, TYPE_ANY);
    register_function("file_stat", 1, 1, bf_file_stat, TYPE_ANY);

#endif
}
