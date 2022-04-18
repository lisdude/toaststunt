/*
 * sql server modification
 * 
 * brief: Code to support SQL database connections in MOOcode.
 *
 * verbatim:
 * Each sql database that's supported must define its own implementation of the
 * following classes:
 *
 *      SQLSession
 *      SQLSessionPool
 *
 * We define generic functions on these classes which provide an abstraction layer
 * of an individual SQL library's functions. Common convention is to prepend
 * SQL database type to the class name when inheriting. 
 *
 * After implementing these classes, an entry has to be created in:
 *
 * create_session_pool()
 * register_sql()
 *
 */

#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "background.h"
#include "functions.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "numbers.h"
#include "server.h"
#include "storage.h"
#include "pcre_moo.h"
#include "utils.h"

#ifdef SQL_FOUND

#include "sql.h"

#ifdef POSTGRESQL_FOUND
#   include <pqxx/pqxx>
#endif
#ifdef SQLITE3_FOUND
#   include <sqlite3.h>
#endif
#ifdef MYSQL_FOUND
#   include <mysql/mysql.h>
#endif

/* The MOO database really dislikes newlines, so we'll want to strip them.
 * I like what MOOSQL did here by replacing them with tabs, so we'll do that.
 * TODO: Check the performance impact of this being on by default with long strings. */
static void sanitize_string_for_moo(char *string)
{
    if (!string)
        return;

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
static Var string_to_moo_type(char* str, bool parse_objects, bool sanitize_string)
{
    Var s;

    if (str == nullptr)
    {
        s.type = TYPE_STR;
        s.v.str = str_dup("NULL");
        return s;
    }

    double double_test = 0.0;
    Num int_test = 0;

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
        s.type = TYPE_FLOAT;
        s.v.fnum = double_test;
    } else {
        if (sanitize_string)
            sanitize_string_for_moo(str);
        s.type = TYPE_STR;
        s.v.str = str_dup(str);
    }
    return s;
}

class Uri {
    public:
        std::string full_string;
        std::string scheme;
        std::string host;
        unsigned short port;
        std::string path;
        std::string user;
        std::string pass;
        // std::string[] queryParameters;

        Uri(std::string raw_url) {
            this->full_string = raw_url;

            /* Compile the pattern */
            static const struct pcre_cache_entry *entry = get_pcre("(?<scheme>[^:]+):\\/\\/(?:(?:(?<user>[^:]+):(?<pass>[^@]+))(?=@)@)?(?<host>[^?:/]*)(?::(?<port>\\d+))?(?:\\/(?<path>[^?]+))\?(?<params>.+)", PCRE_CASELESS);

            /* Determine how many subpatterns match so we can allocate memory. */
            int oveccount = (entry->captures + 1) * 3;
            int ovector[oveccount];

            int offset = 0, rc = 0, named_substrings;
            unsigned int loops = 0;
            const char *matched_substring;
            int subject_length = raw_url.length();
            const char* subject = raw_url.c_str();

            /* Execute the match. */
            while (offset < subject_length)
            {
                loops++;
                rc = pcre_exec(entry->re, entry->extra, subject, subject_length, offset, 0, ovector, oveccount);
                if (rc < 0 && rc != PCRE_ERROR_NOMATCH)
                {
                    /* Encountered some freaky error. Throw an exception. */
                    throw std::runtime_error("pcre_exec failed to parse uri.");
                } else if (rc == 0) {
                    /* We don't have enough room to store all of these substrings. */
                    throw std::runtime_error("pcre_exec ran out of room when parsing uri.");
                } else if (rc == PCRE_ERROR_NOMATCH) {
                    /* There are no more matches. */
                    break;
                } else if (loops >= 20) {
                    /* The loop has iterated beyond the maximum limit, probably locking the server. Kill it. */
                    throw std::runtime_error("pcre_exec was taking too long to parse uri, and it was killed.");
                }

                /* Get named fields */
                const char* result = "";
                
                pcre_get_named_substring(entry->re, raw_url.c_str(), ovector, rc, "scheme", &result);
                this->scheme = result;

                pcre_get_named_substring(entry->re, raw_url.c_str(), ovector, rc, "host", &result);
                this->host = result;

                pcre_get_named_substring(entry->re, raw_url.c_str(), ovector, rc, "user", &result);
                this->user = result;

                pcre_get_named_substring(entry->re, raw_url.c_str(), ovector, rc, "pass", &result);
                this->pass = result;

                pcre_get_named_substring(entry->re, raw_url.c_str(), ovector, rc, "path", &result);
                this->path = result;

                pcre_get_named_substring(entry->re, raw_url.c_str(), ovector, rc, "port", &result);
                auto port_str = (std::string)result;
                if (!port_str.empty()) {
                    this->port = stoi(port_str);
                }

                /* Begin at the end of the previous match on the next iteration of the loop. */
                offset = ovector[1];
            }
        }
};

class SQLSession {
    public:
        virtual void query(
            std::string statement, 
            Var* bind, 
            Var* ret, 
            unsigned char options = 0)      = 0;
        virtual void shutdown()             = 0;
        void wait() {
            std::unique_lock<std::mutex> lock(busy_mutex);
        }
    protected:
        mutable std::mutex busy_mutex;
};

class SQLSessionPool {
    public:
        std::unique_ptr<Uri> connection_uri;
        int handle_id = -1;
        unsigned char options;

        SQLSessionPool(std::unique_ptr<Uri> uri) {
            connection_uri = std::move(uri);
        }

        SQLSession* get_connection() {
            std::unique_lock<std::mutex> lock(connections_mutex);
            auto connection = this->get_or_create_connection();
            
            if (connection == nullptr) {
                return connection;
            }

            set_connection_busy(connection);
            return connection;
        }

        void release_connection(SQLSession* session) {
            std::unique_lock<std::mutex> lock(connections_mutex);

            // We're over connection cap, release to get back to cap.
            if (size() > SQL_SOFT_MAX_CONNECTIONS) {
                expire_connection(session);
                return;
            }

            // Normal release, bring back to idle pool.
            set_connection_idle(session);
        }

        void expire_connection(SQLSession* session) {
            if (auto it = connections_busy.find(session); it != connections_busy.end()) {
                it->second->wait();
                it->second->shutdown();
                connections_busy.erase(it);
            }

            if (auto it = connections_idle.find(session); it != connections_idle.end()) {
                it->second->shutdown();
                connections_idle.erase(it);
            }
        }

        void stop() {
            std::unique_lock<std::mutex> lock(connections_mutex);            
            for (auto&& connection : connections_idle) {
                connection.second->shutdown();
            }

            for (auto&& connection : connections_busy) {
                connection.second->wait();
                connection.second->shutdown();
            }

            connections_idle.clear();
            connections_busy.clear();
        }

        std::size_t size() const {
            return size_idle() + size_busy();
        }

        std::size_t size_idle() const {
            return connections_idle.size();
        }
        
        std::size_t size_busy() const {
            return connections_busy.size();
        }
    
    protected:
        virtual std::unique_ptr<SQLSession> create_connection() = 0;

        SQLSession* get_or_create_connection() {
            for (auto&& item : this->connections_idle) {
                return item.first;
            }

            auto connection = create_connection();
            auto result = connection.get();
            connections_idle[result] = std::move(connection);
            return result;
        }

        void set_connection_busy(SQLSession* session) {
            if (auto it = connections_idle.find(session); it != connections_idle.end()) {
                auto node = connections_idle.extract(it);
                connections_busy.insert(std::move(node));
            }
        }

        void set_connection_idle(SQLSession* session) {
            if (auto it = connections_busy.find(session); it != connections_busy.end()) {
                auto node = connections_busy.extract(it);
                connections_idle.insert(std::move(node));
            }
        }
    
    protected:
        mutable std::mutex connections_mutex;
        std::unordered_map<SQLSession*, std::unique_ptr<SQLSession>> connections_idle;
        std::unordered_map<SQLSession*, std::unique_ptr<SQLSession>> connections_busy;
};

#ifdef POSTGRESQL_FOUND
class PostgreSQLSession: public SQLSession {
    public:
        PostgreSQLSession(Uri* uri) {
            connection_string = uri->full_string;    
            connection = std::make_unique<pqxx::connection>(connection_string);
        }

        void query(std::string statement, Var* bind, Var* ret, unsigned char options = 0) {
            std::unique_lock<std::mutex> lock(busy_mutex);

            pqxx::work txn {*connection.get()};
            pqxx::result res;

            // Code for binding prepared statements.
            if (bind != nullptr) {
                pqxx::params p;
                for (int bind_col=1; bind_col <= bind->v.num; bind_col++) {
                    switch (bind[bind_col].type) {
                        case TYPE_STR:
                            p.append(pqxx::to_string(bind[bind_col].v.str));
                            break;
                        case TYPE_INT:
                        case TYPE_NUMERIC:
                            p.append(bind[bind_col].v.num);
                            break;
                        case TYPE_FLOAT:
                            p.append(bind[bind_col].v.fnum);
                            break;
                        case TYPE_BOOL:
                            p.append(bind[bind_col].v.truth);
                            break;
                    }
                }
                
                res = txn.exec_params(statement, p);
            } else {
                res = txn.exec(statement);
            }

            // Get results
            *ret = new_list(0);
            for (auto row: res) {
                Var rv = new_list(0);
                for (auto col: row) {
                    char *str = (char*)col.c_str();
                    Var column;

                    if (!(options & SQL_PARSE_TYPES)) {
                        if (options & SQL_SANITIZE_STRINGS)
                            sanitize_string_for_moo(str);
                        column.type = TYPE_STR;
                        column.v.str = str_dup(str);
                    } else {
                        column = string_to_moo_type(str, options & SQL_PARSE_OBJECTS, options & SQL_SANITIZE_STRINGS);
                    }

                    rv = listappend(rv, column);
                }
                *ret = listappend(*ret, rv);
            }

            res.clear();
            txn.commit();
        }

        void shutdown() {
            connection->close();
        }

    private:
        std::string connection_string;
        std::unique_ptr<pqxx::connection> connection;
};

class PostgreSQLSessionPool: public SQLSessionPool {
    public:
        PostgreSQLSessionPool(std::unique_ptr<Uri> uri) : SQLSessionPool(std::move(uri)) { }
    protected:
        std::unique_ptr<SQLSession> create_connection() {
            return std::make_unique<PostgreSQLSession>(connection_uri.get());
        }
};
#endif // POSTGRESQL_FOUND

#ifdef SQLITE3_FOUND
class SQLiteSession: public SQLSession {
    public:
        SQLiteSession(Uri* uri) {
            auto return_code = sqlite3_open(uri->host.c_str(), &db);            
            if (return_code != SQLITE_OK) {
                throw std::runtime_error("Cannot open database: " + uri->host);
            }
        }
    
        void query(std::string statement, Var* bind, Var* ret, unsigned char options = 0) {
            std::unique_lock<std::mutex> lock(busy_mutex);

            // Create the statement.
            sqlite3_stmt *res;
            auto return_code = sqlite3_prepare_v2(db, statement.c_str(), -1, &res, nullptr);

            if (return_code != SQLITE_OK) {
                char *errstr = (char*)sqlite3_errmsg(db);
                sqlite3_finalize(res);
                throw std::runtime_error(errstr);
            }

            // Code for binding prepared statements.
            if (bind != nullptr) {
                for (int bind_col=1; bind_col <= bind->v.num; bind_col++) {
                    switch (bind[bind_col].type) {
                        case TYPE_STR:
                            return_code = sqlite3_bind_text(res, bind_col, bind[bind_col].v.str, -1, 0);
                            break;
                        case TYPE_INT:
                        case TYPE_NUMERIC:
                            return_code = sqlite3_bind_int(res, bind_col, bind[bind_col].v.num);
                            break;
                        case TYPE_FLOAT:
                            return_code = sqlite3_bind_double(res, bind_col, bind[bind_col].v.fnum);
                            break;
                        case TYPE_BOOL:
                            return_code = sqlite3_bind_int(res, bind_col, bind[bind_col].v.truth);
                            break;
                    }

                    if (return_code == SQLITE_RANGE) {
                        sqlite3_finalize(res);
                        throw std::runtime_error("Parameter index out of range.");
                    } else if (return_code != SQLITE_OK) {
                        sqlite3_finalize(res);
                        throw std::runtime_error("Error when binding argument to query.");
                    }
                }
            }

            // Actually parsing the results.
            int column_count = 0;
            *ret = new_list(0);
            while((return_code = sqlite3_step(res)) == SQLITE_ROW) {
                int column_count = sqlite3_data_count(res);
                if (column_count <= 0) {
                  continue;
                }

                Var row = new_list(0);
                for (int i=0;i < column_count;i++) {
                    char *str = (char*)sqlite3_column_text(res, i);

                    Var column;
                    if (!(options & SQL_PARSE_TYPES)) {
                        if (options & SQL_SANITIZE_STRINGS)
                            sanitize_string_for_moo(str);
                        column.type = TYPE_STR;
                        column.v.str = str_dup(str);
                    } else {
                        column = string_to_moo_type(str, options & SQL_PARSE_OBJECTS, options & SQL_SANITIZE_STRINGS);
                    }

                    row = listappend(row, column);
                }

                *ret = listappend(*ret, row);
            }

            if (return_code != SQLITE_DONE) {
                char *errstr = (char*)sqlite3_errmsg(db);
                *ret = str_dup_to_var(errstr);
            }

            sqlite3_finalize(res);
        }

        void shutdown() {
            sqlite3_close(db);
        }

    private:
        sqlite3 *db;
};

class SQLiteSessionPool: public SQLSessionPool {
    public:
        SQLiteSessionPool(std::unique_ptr<Uri> uri) : SQLSessionPool(std::move(uri)) { }
    protected:
        std::unique_ptr<SQLSession> create_connection() {
            return std::make_unique<SQLiteSession>(connection_uri.get());
        }
};
#endif // SQLITE3_FOUND

static std::unordered_map<unsigned short, std::unique_ptr<SQLSessionPool>> connection_pools;

static int
next_identifier()
{
    int id = -1;
    int next_id = 1;
    while (id < 0)
    {
        if (!connection_pools.count(next_id))
        {
            id = next_id;
            break;
        }
        next_id++;
    }

    return next_id;
}

void sql_shutdown()
{
    connection_pools.clear();
}

static SQLSessionPool* create_session_pool(std::string connection_string, unsigned char options)
{
    auto uri = std::make_unique<Uri>(connection_string);
    int handle_id = next_identifier();
    std::unique_ptr<SQLSessionPool> pool;
#ifdef POSTGRESQL_FOUND
    if (!pool && (uri->scheme == "postgresql" || uri->scheme == "postgres")) {
        pool = std::make_unique<PostgreSQLSessionPool>(std::move(uri));
    }
#endif
#ifdef SQLITE3_FOUND
    if (!pool && (uri->scheme == "sqlite")) {
        pool = std::make_unique<SQLiteSessionPool>(std::move(uri));
    }
#endif

    if (pool) {
        pool->handle_id = handle_id;
        pool->options = options;
        connection_pools[handle_id] = std::move(pool);
        return connection_pools[handle_id].get();
    }

    throw std::runtime_error("invalid scheme provided, no schema exists by that name.");
    return nullptr;
}

static SQLSessionPool* get_or_create_session_pool(
    std::string connection_string, 
    unsigned char options = SQL_PARSE_TYPES | SQL_PARSE_OBJECTS
)
{
    for (auto& item: connection_pools) {
        /* We're searching for a connection pool with a matching connection string for caching. */
        if (item.second->connection_uri->full_string != connection_string) {
            continue;
        }
        return item.second.get();
    }

    /* We didn't find a matching pool, so we just create a new one. */
    return create_session_pool(connection_string, options);
}

void
query_callback(const Var arglist, Var *ret)
{
    int nargs = arglist.v.list[0].v.num;
    int handle_id = arglist.v.list[1].v.num;
    std::string query = arglist.v.list[2].v.str;

    auto pool = connection_pools[handle_id].get();
    if (!pool) {
        *ret = str_dup_to_var("No connection handle value found by that ID.");
        return;
    }

    SQLSession* session;

    try {
        session = pool->get_connection();
        if (nargs < 3 || arglist.v.list[3].v.num < 1) {
            // There's no SQL parameters.
            session->query(query, nullptr, ret);
        } else {
            // Parameterize the SQL
            session->query(query, arglist.v.list[3].v.list, ret);
        }

        // We're done with the connection, let it go back to the pool.
        pool->release_connection(session);
    } catch (const std::runtime_error& re) {
        *ret = str_dup_to_var(re.what());
        pool->release_connection(session);
#ifdef POSTGRESQL_FOUND
    } catch (pqxx::broken_connection const &e) {
        // This can happen at literally any time, and be left with a broken connection.
        // Release the connection object for good.
        pool->expire_connection(session);
        *ret = str_dup_to_var("Connection lost to server.");
#endif
    } catch(...) {
        *ret = str_dup_to_var("Unknown failure encountered.");
        pool->release_connection(session);
    }
}

static package
bf_sql_query (Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int handle_id = arglist.v.list[1].v.num;
    auto handle = connection_pools.find(handle_id);
    if (handle == connection_pools.end()) {
        free_var(arglist);
        return make_var_pack(str_dup_to_var("No connection handle value by that ID."));
    }

    // Input validation for arguments.
    if (arglist.v.list[0].v.num == 3 && arglist.v.list[3].v.list->v.num > 0) {
        Var *tmp = arglist.v.list[3].v.list;
        for (int x = 1; x <= tmp->v.num; x++) {
            switch(tmp[x].type) {
                case TYPE_FLOAT:
                case TYPE_INT:
                case TYPE_STR:
                case TYPE_NUMERIC:
                    continue;                    
            }

            free_var(arglist);
            return make_error_pack(E_INVARG);
        }
    }

    char *human_string = nullptr;
    asprintf(&human_string, "sql query: %s", arglist.v.list[2].v.str);

    // Run the query.
    return background_thread(query_callback, &arglist, human_string);  
}

static package
bf_sql_connections (Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    Var ret = new_map();
    for (auto const& item: connection_pools) {
        Var key;
        key.type = TYPE_INT;
        key.v.num = item.first;
        ret = mapinsert(ret, key, str_dup_to_var(item.second->connection_uri->full_string.c_str()));
    }

    free_var(arglist);
    return make_var_pack(ret);
}

static package
bf_sql_open_connection (Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    Var ret;
    ret.type = TYPE_INT;
    try {
        std::string connection_string = arglist.v.list[1].v.str;        
        
        unsigned char options;
        if (arglist.v.list[0].v.num >= 2)
            options = arglist.v.list[2].v.num;        
        auto pool = get_or_create_session_pool(connection_string, options);

        // Return the handle identifier integer.
        free_var(arglist); 
        ret.v.num = pool->handle_id;
        return make_var_pack(ret);
    } catch(...) {
        // Catch all non-sql errors as E_INVARG.
        free_var(ret);
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }
}

static package
bf_sql_close_connection (Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int handle_id = arglist.v.list[1].v.num;
    auto handle = connection_pools.find(handle_id);
    if (handle == connection_pools.end()) {
        free_var(arglist);
        return make_var_pack(str_dup_to_var("No connection handle value by that ID."));
    }

    Var ret;
    try
    {
        auto pool = handle->second.get();

        pool->stop();
        connection_pools.erase(handle);

        free_var(arglist);
        ret.type = TYPE_INT;
        ret.v.num = 1;
        return make_var_pack(ret);
    } catch (...) {
        free_var(arglist);
        free_var(ret);
        return make_error_pack(E_INVARG);
    }
}

static package
bf_sql_info(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    int handle_id = arglist.v.list[1].v.num;
    auto handle = connection_pools.find(handle_id);
    if (handle == connection_pools.end()) {
        free_var(arglist);
        return make_var_pack(str_dup_to_var("No connection handle value by that ID."));
    }

    auto pool = handle->second.get();

    Var ret = new_map();
    ret = mapinsert(ret, str_dup_to_var("uri"), str_dup_to_var(pool->connection_uri->full_string.c_str()));
    ret = mapinsert(ret, str_dup_to_var("parse_types"), Var::new_int(pool->options & SQL_PARSE_TYPES ? 1 : 0));
    ret = mapinsert(ret, str_dup_to_var("parse_objects"), Var::new_int(pool->options & SQL_PARSE_OBJECTS ? 1 : 0));
    ret = mapinsert(ret, str_dup_to_var("sanitize_strings"), Var::new_int(pool->options & SQL_SANITIZE_STRINGS ? 1 : 0));
    ret = mapinsert(ret, str_dup_to_var("pool_size"), Var::new_int(pool->size()));

    return make_var_pack(ret);
}

void register_sql(void)
{
    oklog("REGISTER_SQL: SQL features are online and enabled!\n");
#ifdef POSTGRESQL_FOUND
    oklog("  POSTGRESQL_OK: PostgreSQL database feature is enabled.\n");
#endif
#ifdef SQLITE3_FOUND
    oklog("  SQLITE3_OK: SQLite v3 database feature is enabled.\n");
#endif

    register_function("sql_query", 2, 3, bf_sql_query, TYPE_INT, TYPE_STR, TYPE_LIST);
    register_function("sql_connections", 0, 0, bf_sql_connections, TYPE_ANY, TYPE_LIST);
    register_function("sql_open", 1, 1, bf_sql_open_connection, TYPE_STR, TYPE_INT, TYPE_INT);
    register_function("sql_close", 1, 1, bf_sql_close_connection, TYPE_INT, TYPE_ANY);
    register_function("sql_info", 1, 1, bf_sql_info, TYPE_INT, TYPE_ANY);
}

#else /* SQL_FOUND */
void register_sql(void) {
    oklog("REGISTER_SQL: Sql features are disabled.\n");
 }
void sql_shutdown(void) { }
#endif /* SQL_FOUND */