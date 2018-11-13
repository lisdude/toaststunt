Instructions on using Minimal.db
--------------------------------

The Minimal database is really quite minimal; there are no commands defined
other than the built-in ones (`PREFIX', `SUFFIX', and `.program') and only
enough verbs defined and/or programmed to make it possible to bootstrap.  It's
almost a certainty that you'd be better off starting from the most recent
release of some other database and possibly trimming things from that.  Some
people can't resist a challenge, though, so this document exists to orient them
to the meager contents of the Minimal database.

There are four objects in the Minimal database, arranged in the parent/child
hierarchy as follows:

	Root Class (#1)
	  System Object (#0)
	  The First Room (#2)
	  Wizard (#3)

Wizard is initially located in The First Room and the other three objects are
initially located in #-1 (i.e., nowhere).

The System Object has two defined verbs: `#0:do_start_script()', which invokes
the built-in function `eval()' on script input passed to the server via the
command line options `-c' and `-f'; and `#0:do_login_command()', which simply
returns #3.  Note: the implementation of `do_login_command()' has the effect
that all connections to the Minimal database are automatically logged-in to
Wizard, the only player.

The First Room has a verb named `eval' defined but not yet programmed.  It has
arguments "any any any" and permissions "rd".

There are no other properties or verbs defined in the database.  There are no
forked or suspended tasks either.

One way to get started building with the Minimal database is to use an editor
to create a file containing a bootstrap script.  Run the server and specify the
path to the file with the `-f' command line option.  The server will read the
file and pass the contents to `do_start_script()' which will evaluate it.

A reasonable starting point for the bootstrap script might include an improved
`do_start_script()' verb and an implementation of the `eval' command.

	/* provide a stack trace on errors */
	set_verb_code(#0, "do_start_script", {
	  "callers() && raise(E_PERM);",
	  "try",
	  "  return eval(@args);",
	  "except ex (ANY)",
	  "  {line, @lines} = ex[4];",
	  "  server_log(tostr(line[4], \":\", line[2], (line[4] != line[1]) ? tostr(\" (this == \", line[1], \")\") | \"\", \", line \", line[6], \":  \", ex[2]));",
	  "  for line in (lines)",
	  "    server_log(tostr(\"... called from \", line[4], \":\", line[2], (line[4] != line[1]) ? tostr(\" (this == \", line[1], \")\") | \"\", \", line \", line[6]));",
	  "  endfor",
	  "  server_log(\"(End of traceback)\");",
	  "endtry"
	});

	/* evaluate */
	set_verb_code(#2, "eval", {
	  "set_task_perms(player);",
	  "try",
	  "  {code, result} = eval(\"return \" + argstr + \";\");",
	  "  if (code)",
	  "    notify(player, tostr(\"=> \", toliteral(result)));",
	  "  else",
	  "    for line in (result)",
	  "      notify(player, line);",
	  "    endfor",
	  "  endif",
	  "except ex (ANY)",
	  "  {line, @lines} = ex[4];",
	  "  notify(player, tostr(line[4], \":\", line[2], (line[4] != line[1]) ? tostr(\" (this == \", line[1], \")\") | \"\", \", line \", line[6], \":  \", ex[2]));",
	  "  for line in (lines)",
	  "    notify(player, tostr(\"... called from \", line[4], \":\", line[2], (line[4] != line[1]) ? tostr(\" (this == \", line[1], \")\") | \"\", \", line \", line[6]));",
	  "  endfor",
	  "  notify(player, \"(End of traceback)\");",
	  "endtry"
	});

From such a basis, you can do anything by using the various built-in functions
directly, like `add_verb()', `add_property()', etc.  With sufficient amounts of
work, you can build up a set of easier-to-use command-line interfaces to these
facilities to make programming and building much easier.
