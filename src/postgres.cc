#include "options.h"

#ifdef PQXX_FOUND

#include <unordered_map>

#include <pqxx/pqxx>
#include "background.h"
#include "functions.h"
#include "numbers.h"
#include "utils.h"
#include "list.h"
#include "storage.h"
#include "log.h"
#include "server.h"
#include "map.h"

const char* connection_string()
{
    char* s = "postgresql://";
    strcat(s, server_string_option("sql_user", "user"));
    
    const char* sql_pass = server_string_option("sql_pass", "");
    if (sql_pass != "") {
        strcat(s, ":");
        strcat(s, sql_pass);
    }
    strcat(s, "@");
    strcat(s, server_string_option("sql_host", "localhost"));

    const char* sql_port = server_string_option("sql_port", "");
    if (sql_port != "") {
        strcat(s, ":");
        strcat(s, sql_port);
    }

    strcat(s, "/");
    strcat(s, server_string_option("sql_database", "database"));

    return s;
}

Var 
col2var(pqxx::field col) 
{
    Var val;
    switch (col.type()) {
        case 16:
            val.type = TYPE_BOOL;
            val.v.truth = col.as<bool>();
            break;
        case 20:
        case 23:
            val.type = TYPE_INT;
            val.v.num = col.as<int>();
            break;
        case 25:
        case 1043:
            val = str_dup_to_var(col.c_str());
            break;
        case 701:
            val.type = TYPE_FLOAT;
            val.v.fnum = col.as<float>();
            break;
        default:
            // If we don't know what a column is, we return #-1 or $nothing.
            val.type = TYPE_OBJ;
            val.v.obj = NOTHING;
            break;
    }
    return val;
}

Var
result2var(pqxx::result res)
{
    Var ret = new_list(0);
    for (auto row: res) {
        Var rv = new_list(0);
        for (auto col: row) {
            rv = listappend(rv, col2var(col));
        }
        ret = listappend(ret, rv);
    }
    return ret;
}

pqxx::params gen_parameters(Var *paramlist)
{
    pqxx::params p;
    for (int x = 1; x <= paramlist->v.num; x++) {
        switch (paramlist[x].type) {
            case TYPE_STR:
                p.append(pqxx::to_string(paramlist[x].v.str));
                break;
            case TYPE_INT:
            case TYPE_NUMERIC:
                p.append(paramlist[x].v.num);
                break;
            case TYPE_FLOAT:
                p.append(paramlist[x].v.fnum);
                break;
            case TYPE_BOOL:
                p.append(paramlist[x].v.truth);
                break;
        }
    }
    return p;
}

void
query_callback(const Var arglist, Var *ret)
{
    pqxx::connection c{connection_string()};
    pqxx::work txn{c};

    pqxx::result res;

    int nargs = arglist.v.list[0].v.num;
    try
    {
        if (nargs < 2 || arglist.v.list[2].v.num < 1) {
            // There's no SQL parameters.
            res = txn.exec(arglist.v.list[1].v.str);
        } else {
            // Parameterize the SQL
            c.prepare("sql_query", arglist.v.list[1].v.str);
            
            res = txn.exec_prepared("sql_query", gen_parameters(arglist.v.list[2].v.list));
        }

        *ret = result2var(res);
        txn.commit();
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
    // Input validation for arguments.
    if (arglist.v.list[0].v.num == 2 && arglist.v.list[2].v.list->v.num > 0) {
        Var *tmp = arglist.v.list[2].v.list;
        for (int x = 1; x <= tmp->v.num; x++) {
            switch(tmp[x].type) {
                case TYPE_FLOAT:
                case TYPE_INT:
                case TYPE_STR:
                case TYPE_BOOL:
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
bf_sql_execute(Var arglist, Byte next, void *vdata, Objid progr) 
{

}

void register_postgres(void) 
{
    oklog("REGISTER_POSTGRESS: Using PQXX Library\n");

    register_function("sql_query", 1, 2, bf_sql_query, TYPE_STR, TYPE_LIST);
    register_function("sql_execute", 1, 2, bf_sql_execute, TYPE_STR, TYPE_LIST);
}
#else /* PQXX_FOUND */
void register_postgres(void) { }
#endif