#include "options.h"

#ifdef ZDB_FOUND

#include <unordered_map>

#include <zdbpp.h>
#include <memory>
#include "background.h"
#include "functions.h"
#include "numbers.h"
#include "utils.h"
#include "list.h"
#include "storage.h"
#include "log.h"
#include "server.h"
#include "map.h"

using namespace zdb;

static std::map<int, std::unique_ptr<ConnectionPool>> connection_pools;

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

static Var
result2var(ResultSet* res)
{
    Var ret = new_list(0);
    while (res->next()) {
        Var rv = new_list(0);
        for (int c = 1; c <= res->columnCount(); c++)
        {
            rv = listappend(rv, str_dup_to_var(res->getString(c)));
        }        
        ret = listappend(ret, rv);
    }
    return ret;
}

void
query_callback(const Var arglist, Var *ret)
{
    int nargs = arglist.v.list[0].v.num;
    int handle_id = arglist.v.list[1].v.num;
    std::string query = arglist.v.list[2].v.str;
      
    try
    {
        auto handle = connection_pools.find(handle_id);
        if (handle == connection_pools.end()) {
            free_var(arglist);
            *ret = str_dup_to_var("No connection handle value by that ID.");
            return;
        }
        auto pool = handle->second.get();
        auto connection = pool->getConnection();

        // // Connection query part.
        auto statement = connection.prepareStatement(query.c_str());

        if (nargs > 2 && arglist.v.list[3].v.num > 0) {
            // Parameterize the SQL
            auto paramlist = arglist.v.list[3].v.list;
            for (int x = 1; x <= paramlist[0].v.num; x++) 
            {
                switch (paramlist[x].type) {
                    case TYPE_STR:
                        statement.setString(x, paramlist[x].v.str);
                        break;
                    case TYPE_INT:
                    case TYPE_NUMERIC:
                        statement.setInt(x, paramlist[x].v.num);
                        break;
                    case TYPE_FLOAT:
                        statement.setDouble(x, paramlist[x].v.fnum);
                        break;
                }
            }
        }

        auto r = statement.executeQuery();
        *ret = result2var(&r);
    }
    catch (std::exception const &e)
    {
        *ret = str_dup_to_var(e.what());
    }

    free_var(arglist);
}

static package 
bf_sql_query(Var arglist, Byte next, void *vdata, Objid progr) 
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
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

    // Run the query.
    char *human_string = nullptr;
    asprintf(&human_string, "sql query");

    return background_thread(query_callback, &arglist, human_string);    
}

static package
bf_sql_connections(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    Var ret = new_map();
    for (auto const& x : connection_pools)
    {
        Var key;
        key.type = TYPE_INT;
        key.v.num = x.first;
        ret = mapinsert(ret, key, str_dup_to_var(x.second.get()->getURL().tostring()));
    }

    free_var(arglist);
    return make_var_pack(ret);
}

// case insensitive string equals
bool iequals(const std::string& a, const std::string& b)
{
    return std::equal(a.begin(), a.end(),
                      b.begin(), b.end(),
                      [](char a, char b) {
                          return tolower(a) == tolower(b);
                      });
}

static package
bf_sql_open_connection(Var arglist, Byte next, void *vdata, Objid progr)
{
    if (!is_wizard(progr))
    {
        free_var(arglist);
        return make_error_pack(E_PERM);
    }

    Var ret;
    ret.type = TYPE_INT;
    try {
        std::string query_url = arglist.v.list[1].v.str;

        // We start by actually looking if there's a matching connection pool already.
        // We don't actually want to create multiple connection pools. Just one per URL.
        for (auto const& x : connection_pools)
        {
            if (iequals(x.second.get()->getURL().tostring(), query_url))
            {
                // Found a cached handle that will work.
                free_var(arglist); 
                ret.v.num = x.first;
                return make_var_pack(ret);
            }
        }

        // Create a new connection pool and link it appropriately across maps.

        int handle_id = next_identifier();
        connection_pools[handle_id] = std::make_unique<ConnectionPool>(query_url);
        connection_pools.find(handle_id)->second.get()->start();

        // Return the handle identifier integer.
        free_var(arglist); 
        ret.v.num = handle_id;
        return make_var_pack(ret);
    } catch(zdb::sql_exception e) {
        // SQL in general will return strings for errors.
        free_var(arglist); 
        ret.type = TYPE_STR;
        ret.v.str = e.what();
        return make_var_pack(ret);
    } catch(...) {
        // Catch all non-sql errors as E_INVARG.
        free_var(ret);
        free_var(arglist);
        return make_error_pack(E_INVARG);
    }
}

static package
bf_sql_close_connection(Var arglist, Byte next, void *vdata, Objid progr)
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

    try
    {
        auto pool = handle->second.get();

        pool->stop();
        connection_pools.erase(handle);

        free_var(arglist);
        Var ret;
        ret.type = TYPE_INT;
        ret.v.num = 1;
        return make_var_pack(ret);
    } catch (const std::exception &exc) {
        Var ret;
        ret.type = TYPE_STR;
        ret.v.str = exc.what();
        return make_var_pack(ret);
    }
}

void register_sqldb(void) 
{
    oklog("REGISTER_SQLDB: Using libzdb Library\n");

    register_function("sql_query", 2, 3, bf_sql_query, TYPE_INT, TYPE_STR, TYPE_LIST);
    register_function("sql_connections", 0, 0, bf_sql_connections, TYPE_ANY, TYPE_LIST);
    register_function("sql_open", 1, 1, bf_sql_open_connection, TYPE_STR, TYPE_INT);
    register_function("sql_close", 1, 1, bf_sql_close_connection, TYPE_INT, TYPE_ANY);
}
#else /* ZDB_FOUND */
void register_sqldb(void) { }
#endif