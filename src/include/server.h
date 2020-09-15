/******************************************************************************
  Copyright (c) 1992, 1995, 1996 Xerox Corporation.  All rights reserved.
  Portions of this code were written by Stephen White, aka ghond.
  Use and copying of this software and preparation of derivative works based
  upon this software are permitted.  Any distribution of this software or
  derivative works must comply with all applicable United States export
  control laws.  This software is made available AS IS, and Xerox Corporation
  makes no warranty about the software, its performance or its conformity to
  any specification.  Any person obtaining a copy of this software is requested
  to send their name and post office or electronic mail address to:
    Pavel Curtis
    Xerox PARC
    3333 Coyote Hill Rd.
    Palo Alto, CA 94304
    Pavel@Xerox.Com
 *****************************************************************************/

/* server.h */

/*
 * This describes the complete set of procedures that a network implementation
 * is expected to use from the rest of the server.  See 'network.h' for the
 * complete set of procedures that a network implementation must provide.
 */

#ifndef Server_H
#define Server_H 1

#include <stdio.h>

#include "config.h"

typedef struct {		/* Server's handle on a connection */
    void *ptr;
} server_handle;

typedef struct {		/* Server's handle on a listening point */
    void *ptr;
} server_listener;

#include "network.h"		/* Include this *after* defining the types */

extern server_listener null_server_listener;

extern server_handle server_new_connection(server_listener l,
					 network_handle h, bool outbound);
				/* Called by the network whenever a new player
				 * connection is created.  If `outbound' is
				 * true, then the connection is being made from
				 * the server to some external place, and L is
				 * NULL_SERVER_LISTENER; if `outbound' is
				 * false, the L is the listening point on which
				 * the new connection was received.  By this
				 * call, the network and server exchange tokens
				 * representing the connection for use in later
				 * calls on each other.
				 */

extern void server_refuse_connection(server_listener l,
				     network_handle h);
				/* Called by the network whenever it has
				 * temporarily accepted a connection just to
				 * explain to the user that the server is too
				 * full to accept their connection for real.
				 * The server may call network_send_line() and
				 * network_connection_name() to print the
				 * explanation, but H will no longer be valid
				 * after server_refuse_connection() returns.
				 */

extern void server_receive_line(server_handle h, const char *line, bool out_of_band);
				/* The given line has been received as input
				 * on the specified connection.  'line' does
				 * not end in a newline; any such bytes have
				 * been removed by the network.  The characters
				 * in 'line' are all either spaces or non-
				 * whitespace ASCII characters.
				 */

extern void server_close(server_handle h);
				/* The specified connection has been broken
				 * for some reason not in the server's control.
				 * Effective immediately, the network will no
				 * longer use the given server_handle and the
				 * server should not use the corresponding
				 * network_handle.
				 */

/*
 * The following procedures should not be called by a network implementation;
 * they are exported from the server module to other parts of the program.
 */

extern void server_suspend_input(Objid connection);
				/* As soon as possible, the server module
				 * should temporarily stop enqueuing input
				 * tasks for the given connection.
				 */
extern void server_resume_input(Objid connection);
				/* The server module may resume enqueuing input
				 * tasks for the given connection.
				 */

extern void set_server_cmdline(const char *line);
				/* If possible, the server's command line, as
				 * shown in the output of the `ps' command, is
				 * changed to the given string.  NOTE: This is
				 * not possible on all systems, so this
				 * operation is effectively a no-op in some
				 * cases.
				 */

#include "structures.h"

extern int server_flag_option(const char *name, int defallt);
				/* If both $server_options and
				 * $server_options.NAME exist, then return true
				 * iff the latter has a true MOO value.
				 * Otherwise, return DEFALLT.
				 */

extern int server_int_option(const char *name, int defallt);
				/* If both $server_options and
				 * $server_options.NAME exist and the latter
				 * has a numeric value, then return that value.
				 * Otherwise, return DEFALLT.
				 */

extern double server_float_option(const char *name, double defallt);
				/* If both $server_options and
				 * $server_options.NAME exist and the latter
				 * has a float value, then return that value.
				 * Otherwise, return DEFALLT.
				 */

extern const char *server_string_option(const char *name,
					const char *defallt);
				/* If either $server_options or
				 * $server_options.NAME does not exist, then
				 * return DEFALLT.  Otherwise, if the latter
				 * has a string value, then return that value;
				 * else, return 0.  NOTE that the returned
				 * string has not had its reference count
				 * changed; the caller should str_ref() the
				 * result if the reference is to be persistent.
				 */

extern int get_server_option(Objid oid, const char *name, Var * r);
				/* If OID.server_options or $server_options
				 * exists, and the first of these that exists
				 * has as value a valid object OPT, and
				 * OPT.NAME exists, then set *R to the value of
				 * OPT.NAME and return 1; else return 0.
				 */

extern void queue_anonymous_object(Var v);
				/* Adds the specified value to the queue of
				 * values to be recycled in between running
				 * player tasks.
				 */

extern void write_values_pending_finalization(void);
extern int read_values_pending_finalization(void);

/*
 * These procedures represent my frustration with the separation
 * of the server module from the network implementation.
 */
extern int find_network_handle(Objid obj, network_handle **handle);
/***************************************************************/

#include "streams.h"
bool is_localhost(Objid connection);
int proxy_connected(Objid connection, char *command);

#include "db.h"

/* Some server options are cached for performance reasons.
   Changes to cached options must be followed by load_server_options()
   in order to have any effect.  Three categories of cached options
   (1)  "protect_<bi-function>" cached in bf_table (functions.c).
   (2)  "protect_<bi-property>" cached here.
   (3)  SERVER_OPTIONS_CACHED_MISC cached here.

 * Each of the entries in SERVER_OPTIONS_CACHED_MISC
 * should be of the form
 *
 *    DEFINE( SVO_OPTION_NAME,     // symbolic name
 *            property_name,	   // $server_options property
 *            kind,		   // 'int' or 'flag'
 *            default_value,	   //
 *            {value = fn(value);},// canonicalizer
 *          )
 */
#define SERVER_OPTIONS_CACHED_MISC(DEFINE, value)		\
								\
  DEFINE( SVO_MAX_LIST_VALUE_BYTES, max_list_value_bytes,		\
									\
	  int, DEFAULT_MAX_LIST_VALUE_BYTES,				\
	 _STATEMENT({							\
	     if (0 < value && value < MIN_LIST_VALUE_BYTES_LIMIT)	\
		 value = MIN_LIST_VALUE_BYTES_LIMIT;			\
	     else if (0 >= value || MAX_LIST_VALUE_BYTES_LIMIT < value)	\
		 value = MAX_LIST_VALUE_BYTES_LIMIT;			\
	   }))								\
								\
  DEFINE( SVO_MAX_MAP_VALUE_BYTES, max_list_value_bytes,		\
									\
	  int, DEFAULT_MAX_MAP_VALUE_BYTES,				\
	 _STATEMENT({							\
	     if (0 < value && value < MIN_MAP_VALUE_BYTES_LIMIT)	\
		 value = MIN_MAP_VALUE_BYTES_LIMIT;			\
	     else if (0 >= value || MAX_MAP_VALUE_BYTES_LIMIT < value)	\
		 value = MAX_MAP_VALUE_BYTES_LIMIT;			\
	   }))								\
								\
  DEFINE( SVO_MAX_STRING_CONCAT, max_string_concat,		\
								\
	  int, DEFAULT_MAX_STRING_CONCAT,			\
	 _STATEMENT({						\
	     if (0 < value && value < MIN_STRING_CONCAT_LIMIT)	\
		 value = MIN_STRING_CONCAT_LIMIT;		\
	     else if (value <= 0 || MAX_STRING < value)		\
		 value = MAX_STRING;				\
	     stream_alloc_maximum = value + 1;			\
	   }))							\
								\
  DEFINE( SVO_MAX_CONCAT_CATCHABLE, max_concat_catchable,	\
	  flag, 0, /* already canonical */			\
	  )

/* List of all category (2) and (3) cached server options */
enum Server_Option {

# define _BP_DEF(PROPERTY,property)		\
      SVO_PROTECT_##PROPERTY = BP_##PROPERTY,	\

    BUILTIN_PROPERTIES(_BP_DEF)

# undef _BP_DEF

# define _SVO_DEF(SVO_MISC_OPTION,_1,_2,_3,_4)	\
      SVO_MISC_OPTION,				\

    SERVER_OPTIONS_CACHED_MISC(_SVO_DEF,@)

# undef _SVO_DEF

    SVO__CACHE_SIZE   /* end marker, not an option */
};

/*
 * Retrieve cached integer server_option values using the SVO_ numbers
 * E.g., use   server_int_option_cached( SVO_MAX_LIST_CONCAT )
 * instead of  server_int_option("max_list_concat")
 */
#define server_flag_option_cached(srvopt)  (_server_int_option_cache[srvopt])
#define server_int_option_cached(srvopt)   (_server_int_option_cache[srvopt])


extern Num _server_int_option_cache[]; /* private */



enum Fork_Result {
    FORK_PARENT, FORK_CHILD, FORK_ERROR
};
extern enum Fork_Result fork_server(const char *subtask_name);

extern void player_switched(Objid old_id, Objid new_id, bool silent);
extern void player_connected(Objid old_id, Objid new_id,
			     bool is_newly_created);
extern int is_player_connected(Objid player);
extern void notify(Objid player, const char *message);
extern void boot_player(Objid player);

extern void write_active_connections(void);
extern int read_active_connections(void);



/* Body for *_connection_option() */
#define CONNECTION_OPTION_GET(TABLE,HANDLE,OPTION,VALUE)	\
    _STATEMENT({						\
	TABLE(_CONNECT_OPTION_GET_SINGLE, (HANDLE), @,		\
	      _RMPAREN2((OPTION),(VALUE)))			\
	return 0;						\
    })

/* Body for *_set_connection_option() */
#define CONNECTION_OPTION_SET(TABLE,HANDLE,OPTION,VALUE)	\
    _STATEMENT({						\
	TABLE(_CONNECT_OPTION_SET_SINGLE, (HANDLE), (VALUE),	\
	      _RMPAREN2((OPTION),(VALUE)))			\
	return 0;						\
    })

/* Body for *_connection_options() */
#define CONNECTION_OPTION_LIST(TABLE,HANDLE,LIST)	\
    _STATEMENT({					\
	TABLE(_CONNECT_OPTION_LIST_SINGLE, (HANDLE), @,	\
	      (LIST))					\
	return (LIST);					\
    })

/* All of the above require a TABLE of connection options #defined
 * as follows
 * 
 * #define TABLE(DEFINE, HANDLE, VALUE, _)
 *    DEFINE(<name>, _, TYPE_<foo>, <member>,
 *           <get-value-expression>,
 *           <set-value-statement>)
 *    ...
 *    
 * where
 *   <get-value-expression>
 *     should extract from HANDLE a value for option <name>
 *     of type suitable for assignment to Var.v.<member>
 *   <set-value-expression>
 *     should do whatever needs to be done to HANDLE
 *     to reflect the new VALUE
 */

/*
 * Helper macros for CONNECTION_OPTION_(GET|SET|LIST)
 * (nothing should need to invoke these directly):
 */
#define _RMPAREN2(ARG1,ARG2) ARG1,ARG2
#define _STATEMENT(STMT) do STMT while (0)

#define _CONNECT_OPTION_GET_SINGLE(NAME, OPTION, VALUE,		\
				   TYPE_FOO, VFOO_MEMBER,	\
				   GETVALUE, SETVALUE)		\
    if (!strcasecmp(OPTION, #NAME)) {				\
	VALUE->type  = (TYPE_FOO);				\
	VALUE->v.VFOO_MEMBER = (GETVALUE);			\
	return 1;						\
    }

#define _CONNECT_OPTION_SET_SINGLE(NAME, OPTION, VALUE,		\
				   TYPE_FOO, VFOO_MEMBER,	\
				   GETVALUE, SETVALUE)		\
    if (!strcasecmp(OPTION, #NAME)) {				\
	SETVALUE;						\
	return 1;						\
    }

#define _CONNECT_OPTION_LIST_SINGLE(NAME, LIST,			\
				    TYPE_FOO, VFOO_MEMBER,	\
				    GETVALUE, SETVALUE)		\
    {								\
	Var pair = new_list(2);					\
	pair.v.list[1].type = TYPE_STR;				\
	pair.v.list[1].v.str = str_dup(#NAME);			\
	pair.v.list[2].type = (TYPE_FOO);			\
	pair.v.list[2].v.VFOO_MEMBER = (GETVALUE);		\
	LIST = listappend(LIST, pair);				\
    }								\

/* Called when a fatal error occurs. */
extern void panic_moo(const char *message);

void set_system_object_integer_limits();

extern void sqlite_shutdown();

#endif				/* Server_H */
