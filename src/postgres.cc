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

static package 
bf_sql_query(Var arglist, Byte next, void *vdata, Objid progr) 
{

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