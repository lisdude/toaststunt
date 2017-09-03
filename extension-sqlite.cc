/* Copyright (c) 2016 lisdude (9118852A2974A1E3E00B2B7A38B024A72248E3ECA4CE2A6B8F595E76AAFF90C3) All rights reserved.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * TODO:
 *      * Make SQLITE_CONNECTIONS a linked list with a server option controlling how many
 *        connections are allowed to be open at once.
 */

#include "extension-sqlite.h"

/* Open an SQLite database.
 * Args: STR <path to database>, [INT options] */
    static package
bf_sqlite_open(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int index = find_next_index();
    if (index == -1)
    {
        // There's no room left in the connection array.
        free_var(arglist);
        return make_raise_pack(E_QUOTA, "Too many database connections open.", var_ref(zero));
    }

    SQLITE_CONN *handle = &SQLITE_CONNECTIONS[index];

    /* NOTE: This relies on having FileIO. If you don't, you'll need
     *       a function to resolve a SAFE path. */
    const char *path = file_resolve_path(arglist.v.list[1].v.str);

    if (arglist.v.list[0].v.num >= 2)
        handle->options = arglist.v.list[2].v.num;

    free_var(arglist);

    if (path == NULL)
        return make_error_pack(E_INVARG);

    int dup_check = database_already_open(path);
    if (dup_check != -1)
    {
        char ohno[256];
        sprintf(ohno, "Database already open with handle: %i", dup_check);
        return make_raise_pack(E_INVARG, ohno, var_ref(zero));
    }

    int rc = sqlite3_open(path, &handle->id);

    if (rc != SQLITE_OK)
    {
        const char *err = sqlite3_errmsg(handle->id);
        sqlite3_close(handle->id);
        return make_raise_pack(E_NONE, err, var_ref(zero));
    } else {
        handle->active = true;
        handle->path = str_dup(path);
        Var r;
        r.type = TYPE_INT;
        r.v.num = index;
        return make_var_pack(r);
    }
}

/* Close an SQLite database.
 * Args: INT <database handle> */
    static package
bf_sqlite_close(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int index = arglist.v.list[1].v.num;
    free_var(arglist);

    if (index < 0 || index >= SQLITE_MAX_CON || SQLITE_CONNECTIONS[index].active == false)
        return make_raise_pack(E_INVARG, "Invalid database handle", var_ref(zero));

    SQLITE_CONN *handle = &SQLITE_CONNECTIONS[index];

    sqlite3_close(handle->id);
    handle->active = false;
    free_str(handle->path);
    handle->path = NULL;

    return no_var_pack();
}

/* Return a list of open SQLite database handles. */
    static package
bf_sqlite_handles(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    Var r = new_list(0);

    int x = 0;
    for (x = 0; x < SQLITE_MAX_CON; x++)
    {
        if (SQLITE_CONNECTIONS[x].active)
        {
            Var tmp;
            tmp.type = TYPE_INT;
            tmp.v.num = x;
            r = setadd(r, tmp);
        }
    }

    free_var(arglist);
    return make_var_pack(r);
}

/* Return information about the specified SQLite database handle.
 * Args: <INT database handle> */
    static package
bf_sqlite_info(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int index = arglist.v.list[1].v.num;
    free_var(arglist);

    if (index < 0 || index > SQLITE_MAX_CON)
        return make_error_pack(E_INVARG);

    SQLITE_CONN *handle = &SQLITE_CONNECTIONS[index];

    if (handle->active == false)
        return make_raise_pack(E_NONE, "Database handle not open", var_ref(zero));

    // Currently: {path, ?parse types, ?parse objects, ?sanitize strings}
    Var ret = new_list(0);
    Var name;
    name.type = TYPE_STR;
    name.v.str = str_dup(handle->path);
    ret = listappend(ret, name);
    Var work;
    work.type = TYPE_INT;
    work.v.num = (handle->options & SQLITE_PARSE_TYPES) ? 1 : 0;
    ret = listappend(ret, work);
    work.v.num = (handle->options & SQLITE_PARSE_OBJECTS) ? 1 : 0;
    ret = listappend(ret, work);
    work.v.num = (handle->options & SQLITE_SANITIZE_STRINGS) ? 1 : 0;
    ret = listappend(ret, work);

    return make_var_pack(ret);
}

/* Creates and executes a prepared statement.
 * Args: INT <database handle>, STR <SQL query>, LIST <values>
 * e.g. sqlite_query(0, 'INSERT INTO test VALUES (?, ?);', {5, #5})
 * TODO: Check the cache for an existing prepared statement */
    static package
bf_sqlite_execute(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int index = arglist.v.list[1].v.num;
    if (index < 0 || index >= SQLITE_MAX_CON || SQLITE_CONNECTIONS[index].active == false)
    {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    const char *query = arglist.v.list[2].v.str;
    SQLITE_CONN *handle = &SQLITE_CONNECTIONS[index];
    sqlite3_stmt *stmt;

    int rc = sqlite3_prepare_v2(handle->id, query, -1, &stmt, 0);
    if (rc != SQLITE_OK)
    {
        const char *err = sqlite3_errmsg(handle->id);
        free_var(arglist);
        return make_raise_pack(E_NONE, err, var_ref(zero));
    }

    /* Take args[3] and bind it into the appropriate locations for SQLite
     * (e.g. in the query values (?, ?, ?) args[3] would be {5, "oh", "hello"}) */
    int x = 0;
    for (x = 1; x <= arglist.v.list[3].v.list[0].v.num; x++)
    {
        switch (arglist.v.list[3].v.list[x].type)
        {
            case TYPE_STR:
                sqlite3_bind_text(stmt, x, arglist.v.list[3].v.list[x].v.str, -1, NULL);
                break;
            case TYPE_INT:
                sqlite3_bind_int(stmt, x, arglist.v.list[3].v.list[x].v.num);
                break;
            case TYPE_FLOAT:
                sqlite3_bind_double(stmt, x, *arglist.v.list[3].v.list[x].v.fnum);
                break;
            case TYPE_OBJ:
                sqlite3_bind_text(stmt, x, reset_stream(object_to_string(&arglist.v.list[3].v.list[x])),  -1, NULL);
                break;
        }
    }

    rc = sqlite3_step(stmt);
    int col = sqlite3_column_count(stmt);

    Var r = new_list(0);

    while (rc == SQLITE_ROW)
    {
        Var row = new_list(0);
        int x = 0;
        for (x = 0; x < col; x++)
        {
            // Ideally we would know the type and use sqlite3_column<TYPE> but we don't!
            char *str = (char*)sqlite3_column_text(stmt, x);

            if (handle->options & SQLITE_SANITIZE_STRINGS)
                sanitize_string_for_moo(str);

            Var s;
            if (!(handle->options & SQLITE_PARSE_TYPES))
            {
                s.type = TYPE_STR;
                s.v.str = str_dup(str);
            } else {
                s = string_to_moo_type(str, handle->options & SQLITE_PARSE_OBJECTS, handle->options & SQLITE_SANITIZE_STRINGS);
            }
            row = listappend(row, s);
        }
        r = listappend(r, row);
        rc = sqlite3_step(stmt);
    }

    /* TODO: Reset the prepared statement bindings and cache it.
     *       (Remove finalize when that happens) */
    sqlite3_finalize(stmt);

    free_var(arglist);
    return make_var_pack(r);
}

/* Execute an SQL command.
 * Args: INT <database handle>, STR <query> */
    static package
bf_sqlite_query(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int index = arglist.v.list[1].v.num;

    if (index < 0 || index >= SQLITE_MAX_CON || SQLITE_CONNECTIONS[index].active == false)
    {
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }

    const char *query = arglist.v.list[2].v.str;
    char *err_msg = 0;

    SQLITE_CONN *handle = &SQLITE_CONNECTIONS[index];

    int rc = sqlite3_exec(handle->id, query, callback, handle, &err_msg);
    free_var(arglist);

    Var r;

    if (rc != SQLITE_OK)
    {
        r.type = TYPE_STR;
        r.v.str = str_dup(err_msg);
        sqlite3_free(err_msg);

        return make_var_pack(r);
    } else {
        r = var_dup(last_result);
        free_var(last_result);
        last_result = new_list(0);

        return make_var_pack(r);
    }
}

/* Identifies the row ID of the last insert command.
 * (this was an early API test, I don't really see the usefulness) */
    static package
bf_sqlite_last_insert_row_id(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int index = arglist.v.list[1].v.num;
    free_var(arglist);

    if (index < 0 || index >= SQLITE_MAX_CON || SQLITE_CONNECTIONS[index].active == false)
        return make_error_pack(E_INVARG);

    SQLITE_CONN *handle = &SQLITE_CONNECTIONS[index];

    Var r;
    r.type = TYPE_INT;
    r.v.num = sqlite3_last_insert_rowid(handle->id);

    return make_var_pack(r);
}

/* -------------------------------------------------------- */

/* Set default known good values on the SQLite connections and initialize
 * the last_result list so that SQL results can be appended. */
void initialize_sqlite_connections()
{
    last_result = new_list(0);

    int x = 0;
    for (x = 0; x < SQLITE_MAX_CON; x++)
    {
        SQLITE_CONNECTIONS[x].active = false;
        SQLITE_CONNECTIONS[x].path = NULL;
        SQLITE_CONNECTIONS[x].options = SQLITE_PARSE_TYPES | SQLITE_PARSE_OBJECTS;
    }
}

/* Find the next inactive entry in the connection list and return its index. */
int find_next_index()
{
    int x = 0;
    for (x = 0; x < SQLITE_MAX_CON; x++)
    {
        if (SQLITE_CONNECTIONS[x].active == false)
            return x;
    }

    return -1;
}

/* Check if a database at 'path' is already open.
 * If so, return its handle. Otherwise, return -1. */
int database_already_open(const char *path)
{
    int x = 0;
    for (x = 0; x < SQLITE_MAX_CON; x++)
    {
        SQLITE_CONN *handle = &SQLITE_CONNECTIONS[x];

        if (handle->active && handle->path != NULL && strcmp(handle->path, path) == 0)
            return x;
    }

    return -1;
}

/* Callback function for execute. Shoves our result into an ugly global
 * for the MOO to soak into a Var. */
int callback(void *index, int argc, char **argv, char **azColName)
{
    SQLITE_CONN *handle = (SQLITE_CONN*)index;

    Var ret = new_list(0);

    int i = 0;
    for (i = 0; i < argc; i++)
    {
        Var s;
        if (!(handle->options & SQLITE_PARSE_TYPES))
        {
            s.type = TYPE_STR;

            if (handle->options & SQLITE_SANITIZE_STRINGS)
                sanitize_string_for_moo(argv[i]);

            s.v.str = str_dup(argv[i]);
        } else {
            s = string_to_moo_type(argv[i], handle->options & SQLITE_PARSE_OBJECTS, handle->options & SQLITE_SANITIZE_STRINGS);
        }
        ret = listappend(ret, s);
    }

    last_result = listappend(last_result, ret);

    return 0;
}

/* The MOO database really dislikes newlines, so we'll want to strip them.
 * I like what MOOSQL did here by replacing them with tabs, so we'll do that.
 * TODO: Check the performance impact of this being on by default with long strings. */
void sanitize_string_for_moo(char *string)
{
    char *p = string;

    while (*p)
    {
        if (*p == '\n')
            *p = '\t';

        ++p;
    }
}

/* Take a result string and convert it into a MOO type.
 * Return a Var of the appropriate MOO type for the value.
 * TODO: Try to parse strings containing MOO lists? */
Var string_to_moo_type(char* str, bool parse_objects, bool sanitize_string)
{
    Var s;

    if (str == NULL)
    {
        s.type = TYPE_STR;
        s.v.str = str_dup("NULL");
        return s;
    }

    double double_test = 0.0;
    int int_test = 0;

    if (str[0] == '#' && parse_objects && parse_number(str + 1, &int_test, 0) == 1)
    {
        // Add one to the pointer to skip over the # and check the rest for numeracy
        s.type = TYPE_OBJ;
        s.v.obj = int_test;
    } else if (parse_number(str, &int_test, 0) == 1) {
        s.type = TYPE_INT;
        s.v.num = int_test;
    } else if (parse_float(str, &double_test) == 1)
    {
        s = new_float(double_test);
    } else {
        if (sanitize_string)
            sanitize_string_for_moo(str);
        s.type = TYPE_STR;
        s.v.str = str_dup(str);
    }
    return s;
}

/* Converts a MOO object (supplied to a prepared statement) into a string similar to
 * tostr(#xxx) */
Stream* object_to_string(Var *thing)
{
    static Stream *s = 0;

    if (!s)
        s = new_stream(11);

    stream_printf(s, "#%d", thing->v.num);

    return s;
}

void
register_sqlite() {
    oklog("REGISTER_SQLITE: v%s (SQLite Library v%s)\n", SQLITE_MOO_VERSION, sqlite3_libversion());
    initialize_sqlite_connections();

    register_function("sqlite_open", 1, 2, bf_sqlite_open, TYPE_STR, TYPE_INT);
    register_function("sqlite_close", 1, 1, bf_sqlite_close, TYPE_INT);
    register_function("sqlite_handles", 0, 0, bf_sqlite_handles);
    register_function("sqlite_info", 1, 1, bf_sqlite_info, TYPE_INT);
    register_function("sqlite_query", 2, 2, bf_sqlite_query, TYPE_INT, TYPE_STR);
    register_function("sqlite_execute", 3, 3, bf_sqlite_execute, TYPE_INT, TYPE_STR, TYPE_LIST);
    register_function("sqlite_last_insert_row_id", 1, 1, bf_sqlite_last_insert_row_id, TYPE_INT);
}
