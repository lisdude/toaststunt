#include <assert.h>
#include <stdlib.h>
#include <algorithm> // std::sort
#include <vector>

#include <ctype.h>
#include <string.h>
#include "my-math.h"

#include "bf_register.h"
#include "collection.h"
#include "config.h"
#include "functions.h"
#include "list.h"
#include "log.h"
#include "map.h"
#include "options.h"
#include "pattern.h"
#include "streams.h"
#include "storage.h"
#include "structures.h"
#include "unparse.h"
#include "utils.h"
#include "server.h"
#include "background.h"   // Threads
#include "random.h"

/*
Below is a list of LIST functions that work with the Toaststunt MOO SERVER found at:
https://lisdude.com/moo
or
https://github.com/lisdude/toaststunt
They have been tested, and do not appear to have any known issues. If you find an issue, you can send me a message. I am Jim33 in the Toaststunt Discord.
Thanks, and hope you find a use for them.
*/

/* $list_utils:iassoc() turned builtin. */
static package
bf_iassoc(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var match = arglist.v.list[1];
    int listindex = (arglist.v.list[0].v.num == 3?arglist.v.list[3].v.num : 1);
    if (listindex < 1)
    {
        free_var(arglist);
        return make_error_pack(E_RANGE);
    }
    if (arglist.v.list[2].v.list[0].v.num == 0)
    {
        free_var(arglist);
        return make_error_pack(E_RANGE);
    }
    int listlength = arglist.v.list[2].v.list[0].v.num;
    for (auto index = 1; index <= listlength; index++)
    {
        if (arglist.v.list[2].v.list[index].type != TYPE_LIST)
        {
            free_var(arglist);
            return make_error_pack(E_TYPE);
        }
        if (arglist.v.list[2].v.list[index].v.list[0].v.num < listindex)
        {
            free_var(arglist);
            return make_error_pack(E_RANGE);
        }
        else if (equality(arglist.v.list[2].v.list[index].v.list[listindex], match, 0))
        {
            free_var(arglist);
            return make_var_pack(var_ref(Var::new_int(index)));
        }
    }

    free_var(arglist);
    return make_var_pack(var_ref(Var::new_int(0)));
}


/* $list_utils:assoc() turned builtin. */
static package
bf_assoc(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var match = arglist.v.list[1];
    int listindex = (arglist.v.list[0].v.num == 3?arglist.v.list[3].v.num : 1);
    if (listindex < 1)
    {
        free_var(arglist);
        return make_error_pack(E_RANGE);
    }

    if (arglist.v.list[2].v.list[0].v.num == 0)
    {
        free_var(arglist);
        return make_error_pack(E_RANGE);
    }

    int listlength = arglist.v.list[2].v.list[0].v.num;
    for (auto index = 1; index <= listlength; index++)
    {
        if (arglist.v.list[2].v.list[index].type != TYPE_LIST)
        {
            free_var(arglist);
            return make_error_pack(E_TYPE);
        }
        if (arglist.v.list[2].v.list[index].v.list[0].v.num < listindex)
        {
            free_var(arglist);
            return make_error_pack(E_RANGE);
        }
        else if (equality(arglist.v.list[2].v.list[index].v.list[listindex], match, 0))
        {
            Var tmp = new_list(0);
            tmp = listappend(tmp, var_ref(arglist.v.list[2].v.list[index]));
            free_var(arglist);
            return make_var_pack(tmp);
        }
    }

    free_var(arglist);
    return make_var_pack(var_ref(new_list(0)));
}


/* Remove all of list2 from list 1. */
static package
bf_set_remove_list(Var arglist, Byte next, void *vdata, Objid progr)
{
    /* Uncomment out the lines below if you want it to not allow empty lists sent. Currently, I allow two empty lists, but you might want E_RANGE  */
    /*
        if (arglist.v.list[1].v.list[0].v.num == 0 || arglist.v.list[2].v.list[0].v.num == 0)
        {
        free_var(arglist);
        return make_error_pack(E_RANGE);
        }; */
    /* Did all our range checking. Remove is our second list, so remove X from list 1 */
    const int listlength = arglist.v.list[1].v.list[0].v.num;
    Var newlist = new_list(0);
    for (auto index = 1; index <= listlength; index++)
    {
        if (!ismember(arglist.v.list[1].v.list[index], arglist.v.list[2], 0))
            newlist = listappend(newlist, var_ref(arglist.v.list[1].v.list[index]));
    }

    free_var(arglist);
    return make_var_pack(newlist);
}


/* removes multiple indices.
remove_multiples({1, #5, 2, #5 1})
returns {1, #5, 2}
This is $list_utils:remove_duplicates turned builtin. */
static package
bf_remove_multiples(Var arglist, Byte next, void *vdata, Objid progr)
{
    const auto olength = arglist.v.list[1].v.list[0].v.num;
    Var nlist = new_list(0);
    for (unsigned int index = 1; index <= olength; index++)
        if (!ismember(arglist.v.list[1].v.list[index], nlist, 0))
            nlist = listappend(nlist, var_ref(arglist.v.list[1].v.list[index]));
    free_var(arglist);
    return make_var_pack(nlist);
}


/* CreateSub_lists: Allows you to send something like the following:
create_sublists({"Bob", "Jim"}, 0);
=> {{"Bob", 0}, {"Jim", 0}};
You can also do:
Create_sublists({"Bob", "Jim",}, {1, 2});
=> {{"Bob", 1}, {"Jim", 2});
If you use a list as the second argument, it needs to match the length of the first list. */
static package
bf_create_sublists(Var arglist, Byte next, void *vdata, Objid progr)
{
     Var nvalue = (arglist.v.list[0].v.num == 2?arglist.v.list[2] : Var::new_int(0));
    if (arglist.v.list[1].v.list[0].v.num == 0)
    {
        free_var(arglist);
        return make_error_pack(E_RANGE);
    }
    const int llength = arglist.v.list[1].v.list[0].v.num;
    Var nlist = new_list(0);
    for (unsigned int index = 1; index <= llength; index++)
    {
        Var tmp = new_list(0);
        tmp = listappend(tmp, var_ref(arglist.v.list[1].v.list[index]));
        if (nvalue.type == TYPE_LIST && nvalue.v.list[0].v.num == 0)
            tmp = listappend(tmp, var_ref(nvalue));
        else if (nvalue.type == TYPE_LIST && nvalue.v.list[0].v.num != 0)
        {
            if (nvalue.v.list[0].v.num != arglist.v.list[1].v.list[0].v.num)
            {
                free_var(tmp);
                free_var(nvalue);
                free_var(nlist);
                return make_error_pack(E_RANGE);
            }

            tmp = listappend(tmp, var_ref(nvalue.v.list[index]));
        }
        else
            tmp = listappend(tmp, var_ref(nvalue));

        nlist = listappend(nlist, var_ref(tmp));
        free_var(tmp);
    }

    free_var(arglist);
    free_var(nvalue);
    return make_var_pack(nlist);
}


/* Callback thread for $list_utils:make() turned function. */
void make_thread_callback(Var arglist, Var *ret)
{
    const int length = arglist.v.list[1].v.num;
    Var nvalue = (arglist.v.list[0].v.num == 2?arglist.v.list[2] : Var::new_int(0));
    /* We have the following check because anything over this could crash the server. 
    Likely a memory usage issue.  Or it was a thread waiting issue because it took so long? 
    It could for sure be because I have no idea what I'm doing... I'm not sure. */
    if (length >= 50000000)
    {
        ret->type = TYPE_ERR;
        ret->v.err = E_INVARG;
    }
    if (length < 0)
    {
        ret->type = TYPE_ERR;
        ret->v.err = E_INVARG;
        return;
    }
    *ret = new_list(0);
    for (int index = 1; index <= length; index++)
        *ret = listappend(*ret, var_dup(nvalue));
}


/* Main user-facing function for the MAKE function. */
static package
bf_make(Var arglist, Byte next, void *vdata, Objid progr)
{
    char *human_string = nullptr;
    asprintf(&human_string, "make");

    return background_thread(make_thread_callback, &arglist, human_string);
}


/* List_loop: Loop through contents of list two, and see if it's in list one. If it is, return a true value. If function completes and it wasn't, return false.
list_loop({"bob", "george"}, {"Jim"});
=> 0;
list_loop({"bob", "george", "frank"}, {"bob"});
=> 1;
*/
static package
bf_list_loop(Var arglist, Byte next, void *vdata, Objid progr)
{
    const int list_length = arglist.v.list[1].v.list[0].v.num;
    Var list_one = arglist.v.list[1];
    Var list_two = arglist.v.list[2];
    free_var(arglist);

    if (list_one.v.list[0].v.num == 0)
    {
        free_var(list_one);
        free_var(list_two);
        return make_error_pack(E_RANGE);
    }
    else if (list_two.v.list[0].v.num == 0)
    {
        free_var(list_one);
        free_var(list_two);
        return make_error_pack(E_RANGE);
    }

    for (unsigned int index = 1; index <= list_length; index++)
    {
        if (ismember(list_one.v.list[index], list_two, 0))
        {
        free_var(list_one);
        free_var(list_two);
        return make_var_pack(Var::new_int(1));
        }
    }

    free_var(list_one);
    free_var(list_two);
    return make_var_pack(Var::new_int(0));
}

/*
Even_odd function. Only works with integers. Anything else is skipped over.
Usage:
even_odd({1, 2, 3});
=> {2};
even_odd({1, 2, 3}, 0);
=> {1, 3};
even_odd(1);
=> {};
even_odd(1, 0);
=> {1};
even_odd(3, 0);
=> {3};
The second argument defaults to 1 if you don't use it. Otherwise, it is set to 0 for negative numbers.
*/
static package
bf_even_odd(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var set;
    int elroy = (arglist.v.list[0].v.num == 2?arglist.v.list[2].v.num : 1);
    if (arglist.v.list[1].type != TYPE_LIST)
    {
        set = new_list(1);
        set.v.list[1] = var_ref(arglist.v.list[1]);
    }
    else
        set = var_ref(arglist.v.list[1]);
    free_var(arglist);
    if (set.v.list[0].v.num == 0)
    {
        free_var(set);
        return make_error_pack(E_RANGE);
    }
    /* Get the length of it. Const is better. */
    const int length = set.v.list[0].v.num;
    /* Build your list. */
    Var result = new_list(0);
    for (unsigned int index = 1; index <= length; index++)
    {
        /* Look over each value type, if not an integer, skip it. If not, handle it. I wanted floating points, but they didn't work right. Maybe another day. */
        if (set.v.list[index].type != TYPE_INT)
            continue; index;
        if (elroy == 1)
        {
            /* They're checking even numbers. */
            /* Which are integers. */
            Var r;
            r.type = TYPE_INT;
            r.v.num = set.v.list[index].v.num;
            if (r.v.num % 2 == 0)
                result = listappend(result, var_ref(set.v.list[index]));
        }
    }
}

/* Setreplace values in list 1, with argument 2.
Usage:
setreplace({1, 2, #5, #8}, #7);
=> {1, 2, #7, #8};
setreplace({1, 2, #3, #5}, #3, {});
=> {1, 2, {}, #5};

You can also put stuff in the list as such:
setreplace({1, 2, #3, #5}, #3, {"bob"});
=> {1, 2, {"bob"}, #5};
*/
static package
bf_setreplace(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var lone = arglist.v.list[1];
    Var ltwo = arglist.v.list[2];
    Var lthree = arglist.v.list[3];
    free_var(arglist);
    if (ltwo.type != TYPE_FLOAT && ltwo.type != TYPE_OBJ && ltwo.type != TYPE_INT && ltwo.type != TYPE_STR)
        return make_error_pack(E_TYPE);
    if (lthree.type != TYPE_FLOAT && lthree.type != TYPE_OBJ && lthree.type != TYPE_INT && lthree.type != TYPE_STR)
        return make_error_pack(E_TYPE);
    if (lone.v.list[0].v.num == 0)
        return make_error_pack(E_RANGE);
    const int lonelength = lone.v.list[0].v.num;
    Var nlist = new_list(0);
    for (unsigned int index = 1; index <= lonelength; ++index)
    {
        if (lone.v.list[index].type != TYPE_INT && lone.v.list[index].type != TYPE_FLOAT && lone.v.list[index].type != TYPE_OBJ && lone.v.list[index].type != TYPE_STR)
        {
            free_var(nlist);
            return make_error_pack(E_TYPE);
        }
        if (equality(lone.v.list[index], ltwo, 0))
            nlist = listappend(nlist, var_dup(lthree));
        else
            nlist = listappend(nlist, var_dup(lone.v.list[index]));
    }
    return make_var_pack(var_dup(nlist));
}


/* First function for $list_utils:char_list() turned builtin;
I took this from Goblens functions, and fixed it so it worked. */
Var
char_list(const char *s)
{
    Var r;
    int l = strlen(s), i;
    Var ss;
    ss.type = TYPE_STR;
    ss.v.str =  s;

    r = new_list(l);
    for (i = 1; i <= l; ++i) 
    {
        r.v.list[i].type = TYPE_STR;
        r.v.list[i] = substr(ss, 1, 1);
        ss.v.str += strlen(r.v.list[i].v.str);
    }

    return r;
}

/* Main user facing function for char_list();
Usage:
char_list("bob");
=> {"b", "o", "b"}; */
static package bf_char_list(Var arglist, Byte next, void *vdata, Objid progr) 
{
    if (strlen(arglist.v.list[1].v.str) > 150) 
    {
	    free_var(arglist);
	    return make_error_pack(E_INVARG);
    }
    return make_var_pack(char_list(arglist.v.list[1].v.str));
}


/* Callback for compress function. */
void compress_thread_callback(Var arglist, Var *ret)
{
    Var elroy = arglist.v.list[1];
    *ret = new_list(0);
    const int length = elroy.v.list[0].v.num;
    if (length != 0)
    {
        for (int index = 1; index <= length; index++)
        {
            if (ret->v.list[0].v.num != 0 && equality(elroy.v.list[index], elroy.v.list[index-1], 0))
              continue;
            else
              *ret = listappend(*ret, var_dup(elroy.v.list[index]));
        }
    }
}

/* $list_utils:compress() converted to a function:
Usage:
eval compress({1, 1, 2, 3});
=> {1, 2, 3}
compress {1, 2, 3, 3, "orange", "purple", "purple"};
=> {1, 2, 3, "orange", "purple"}
*/
static package
bf_compress(Var arglist, Byte next, void *vdata, Objid progr)
{
    char *human_string = nullptr;
    asprintf(&human_string, "compress in %" PRIdN " element list", arglist.v.list[1].v.list[0].v.num);
    return background_thread(compress_thread_callback, &arglist, human_string);
}

/* make_map(); It turns sublists into a map with key being first value, with second argue being the value:
Key can only be the standard map types, integer, string, object, float.
Value can be whatever you want.
Usage:
make_map({{"bob", []}, {#7, {"Loves cheese."}}});
=> ["bob" -> [], #7 -> {"Loves cheese."}];
*/
static package
bf_make_map(Var arglist, Byte next, void *vdata, Objid progr)
{
    const int listlength = arglist.v.list[1].v.list[0].v.num;
    Var alist = arglist.v.list[1];
    free_var(arglist);
    if (listlength == 0)
    {
        free_var(alist);
        return make_error_pack(E_ARGS);
    }

    Var nmap = new_map();
    for (unsigned int index = 1; index <= listlength; index++)
    {
        if (alist.v.list[index].type != TYPE_LIST)
        {
            free_var(nmap);
            return make_error_pack(E_TYPE);
        }

        if (alist.v.list[index].v.list[0].v.num != 2)
        {
            free_var(nmap);
            return make_error_pack(E_RANGE);
        }

        const auto left = alist.v.list[index].v.list[1].type;
        if (left != TYPE_INT && left != TYPE_FLOAT && left != TYPE_STR && left != TYPE_OBJ)
        {
            free_var(nmap);
            return make_error_pack(E_TYPE);
        }

        nmap = mapinsert(nmap, var_ref(alist.v.list[index].v.list[1]), var_dup(alist.v.list[index].v.list[2]));
    }

    return make_var_pack(var_dup(nmap));
}

/* Get_location: Find a kid of second argument, starting at first argument.
Usage:
get_location(me, #3);
=> #62  (The First Room)
*/
static package
bf_get_location(Var arglist, Byte next, void *vdata, Objid progr)
{
    Var loc = arglist.v.list[1];
    Var destination = arglist.v.list[2];
    free_var(arglist);

    if (!is_valid(loc))
    {
        free_var(loc);
        free_var(destination);
        return make_error_pack(E_INVARG);
    }
    else if (!is_valid(destination))
    {
        free_var(loc);
        free_var(destination);
        return make_error_pack(E_INVARG);
    }
    while (is_valid(loc))
    {
        if (db_object_isa(loc, destination))
        {
            free_var(destination);
            return make_var_pack(loc);
        }

        loc = db_property_value(db_find_property(loc, "location", 0));
    }
    free_var(loc);
    free_var(destination);
    return make_var_pack(Var::new_int(0));
}

/* $code_utils:task_valid turned builtin. Currently isn't completely how we want it, but arg[1] equals output from queued_tasks, arg[2] equals task id you're checking. It returns 1 if true, 0 if false. It's still faster than $code_utils:task_valid, use if you wish.
Usage:
task_valid(queued_tasks(), me.test_task);
=> 1;
(if valid) otherwise:);
=> 0;
*/
static package
bf_task_valid(Var arglist, Byte next, void *vdata, Objid progr)
{
    const Var id = arglist.v.list[2];
    const int tasklength = arglist.v.list[1].v.list[0].v.num;
    if (tasklength == 0)
    {
        free_var(arglist);
        return make_error_pack(E_RANGE);
    }
    
    for (auto index = 1; index <= tasklength; index++)
    {
        if (equality(arglist.v.list[1].v.list[index].v.list[1], id, 0))
        {
            free_var (arglist);
            free_var(id);
            return make_var_pack(Var::new_int(1));
        }
    }

    free_var(arglist);
    free_var(id);
    return make_var_pack(Var::new_int(0));
}

void register_jims_extensions(void)
{
    /* list functions */
    register_function("iassoc", 2, 3, bf_iassoc, TYPE_ANY, TYPE_LIST, TYPE_INT);
    register_function("assoc", 2, 3, bf_assoc, TYPE_ANY, TYPE_LIST, TYPE_INT);
    register_function("set_remove_list", 2, 2, bf_set_remove_list, TYPE_LIST, TYPE_LIST);
    register_function("remove_multiples", 1, 1, bf_remove_multiples, TYPE_LIST);
    register_function("create_sublists", 1, 2, bf_create_sublists, TYPE_LIST, TYPE_ANY);
    register_function("make", 1, 2, bf_make, TYPE_INT, TYPE_ANY);
    register_function("list_loop", 2, 2, bf_list_loop, TYPE_LIST, TYPE_LIST);
    register_function("even_odd", 3, 3, bf_even_odd, TYPE_ANY, TYPE_INT, TYPE_INT);
    register_function("setreplace", 3, 3, bf_setreplace, TYPE_LIST, TYPE_ANY, TYPE_ANY);
    register_function("char_list", 1, 1, bf_char_list, TYPE_STR);
    register_function("compress", 1, 1, bf_compress, TYPE_LIST);

    /* map functions */
    register_function("make_map", 1, 1, bf_make_map, TYPE_LIST);

    /* object functions */
    register_function("get_location", 2, 2, bf_get_location, TYPE_OBJ, TYPE_OBJ);

    /* task functions */
    register_function("task_valid", 2, 2, bf_task_valid, TYPE_LIST, TYPE_INT);
}