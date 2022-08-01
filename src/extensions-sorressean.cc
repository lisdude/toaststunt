/* Various functions to extend the server taken for Sorressean's server fork */

#include "background.h"
#include "collection.h" //ismember
#include "db.h"
#include "db_private.h"
#include "functions.h"      // register builtins
#include "list.h" //listappend, etc
#include "log.h"            // oklog()
#include "map.h" //mapforeach, etc
#include "utils.h" //free_var plus many others

static int do_maphasvalue(Var key, Var value, void *data, int first)
{
    Var* search = (Var*)data;
    return equality(value, *search, 1);
}

static package bf_maphasvalue(Var arglist, Byte next, void *vdata, Objid progr)
{
    const auto result = mapforeach(arglist.v.list[1], do_maphasvalue, &arglist.v.list[2]);
    free_var(arglist);

    return make_var_pack(Var::new_int(result));
}

static package
bf_diff(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var result = var_dup(arglist.v.list[1]);

    const auto nargs = arglist.v.list[0].v.num;
    for (int x = 2; x <= nargs; x++)
        {
            if (arglist.v.list[x].type != TYPE_LIST)
                {
                    free_var(result);
                    free_var(arglist);
                    return make_error_pack(E_TYPE);
                }
            const auto nestargs = arglist.v.list[x].v.list[0].v.num;
            for (int y = 1; y <= nestargs; y++)
                {
                    result = setremove(result, arglist.v.list[x].v.list[y]);
                }
        }

    free_var(arglist);
    return make_var_pack(result);
}

void register_sorressean_extensions()
{
    // register_function("assoc", 2, 3, bf_assoc, TYPE_ANY, TYPE_LIST, TYPE_INT);
    // register_function("iassoc", 2, 3, bf_iassoc, TYPE_ANY, TYPE_LIST, TYPE_INT);
    register_function("maphasvalue", 2, 2, bf_maphasvalue, TYPE_MAP, TYPE_ANY);
    // register_function("intersection", 1, -1, bf_intersection, TYPE_LIST);
    register_function("difference", 1, -1, bf_diff, TYPE_LIST);
    // register_function("union", 1, -1, bf_union, TYPE_LIST);
    // register_function("set_merge", 2, 2, bf_set_merge, TYPE_LIST, TYPE_LIST);
    // register_function("listflatten", 1, 1, bf_list_flatten, TYPE_LIST);
    // register_function("join", 1, 2, bf_join, TYPE_LIST, TYPE_STR);
    // register_function("listremove_duplicates", 1, 1, bf_list_remove_duplicates, TYPE_LIST);
    // register_function("all_contents", 1, 1, bf_all_contents, TYPE_OBJ);
    // register_function("bit_or", 2, 2, bf_bit_or, TYPE_INT, TYPE_INT);
    // register_function("bit_and", 2, 2, bf_bit_and, TYPE_INT, TYPE_INT);
    // register_function("bit_xor", 2, 2, bf_bit_xor, TYPE_INT, TYPE_INT);
    // register_function("bit_not", 1, 1, bf_bit_not, TYPE_INT);
    // register_function("clamp", 3, 3, bf_clamp, TYPE_NUMERIC, TYPE_NUMERIC, TYPE_NUMERIC);
    // register_function("collect_stats", 1, 1, bf_collect_stats, TYPE_LIST);
    // register_function("mdistance", 3, 3, bf_mdistance, TYPE_LIST, TYPE_LIST, TYPE_FLOAT);
    // register_function("sort_alist", 1, 5, bf_sort_alist, TYPE_LIST, TYPE_LIST, TYPE_INT, TYPE_INT, TYPE_INT);
    // register_function("isalnum", 1, 1, bf_isalnum, TYPE_STR);
    // register_function("isalpha", 1, 1, bf_isalpha, TYPE_STR);
    // register_function("isdigit", 1, 1, bf_isdigit, TYPE_STR);
    // register_function("isprint", 1, 1, bf_isprint, TYPE_STR);
    // register_function("ispunct", 1, 1, bf_ispunct, TYPE_STR);
    // register_function("map_get_recursive", 2, 2, bf_map_get_recursive, TYPE_LIST, TYPE_MAP);
    // register_function("strmsub", 2, 4, bf_strmsub, TYPE_STR, TYPE_LIST, TYPE_INT, TYPE_STR);
    // register_function("hostname", 0, 0, bf_hostname);
    // register_function("filter_valid_objects", 1, 1, bf_filter_valid_objects, TYPE_LIST);
    // register_function("generate_uuid", 0, 0, bf_generate_uuid);
    // register_function("str_upper", 1, 1, bf_str_upper, TYPE_STR);
    // register_function("str_lower", 1, 1, bf_str_lower, TYPE_STR);
    // register_function("str_trim_left", 1, 1, bf_str_trim_left, TYPE_STR);
    // register_function("str_trim_right", 1, 1, bf_str_trim_right, TYPE_STR);
    // register_function("str_trim", 1, 1, bf_str_trim, TYPE_STR);
    // register_function("str_starts_with", 2, 3, bf_str_starts_with, TYPE_STR, TYPE_STR, TYPE_INT);
    // register_function("str_ends_with", 2, 3, bf_str_ends_with, TYPE_STR, TYPE_STR, TYPE_INT);
    // register_function("sum", 1, 1, bf_sum, TYPE_LIST);
    // register_function("average", 1, 1, bf_average, TYPE_LIST);
    // register_function("str_capitalize", 1, 1, bf_str_capitalize, TYPE_STR);
}