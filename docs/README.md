See `README.stunt' for general information on Stunt.

ToastStunt is the server that runs [Miriani](https://www.toastsoft.net) and [ChatMud](https://www.chatmud.com/). It has a number of features of dubious usefulness that have been tacked on over the past decade, a semi-complete list of which can be found below:

- SQLite
- PCRE
- Simplex noise (though I've never actually used it...)
- Argon2id hashing (argon2(), argon2_verify())

- Waifs
    - Call :recycle on waifs when they're destroyed
    - A WAIF type (so typeof(some_waif) == WAIF)
    - Waif dict patch
    - '-w' command line option to convert old databases with a different waif type to the new waif type
    - waif_stats (show how many instances of each class of waif exist, how many waifs are pending recycling, and how many waifs in total exist)
    - Add parser recognition for waif properties (e.g. thing.:property)

- Basic threading support:
    - background.cc (a library, of sorts, to make it easier to thread builtins)
    - Threaded sqlite_query and sqlite_execute functions

- FileIO improvements:
    - Faster reading
    - Open as many files as you want, configurable with FILE_IO_MAX_FILES or $server_options.file_io_max_files
    - file_handles (returns a list of open files)
    - file_grep (search for a string in a file (kind of FUP in FIO, don't tell))
    - file_count_lines (counts the number of lines in a file)

- ANSI:
    - Parse_ansi (parses color tags into their ANSI equivalents)
    - remove_ansi (strips ANSI tags from strings)

- Options.h configuration:
    - LOG_CODE_CHANGES (causes .program and set_verb_code to add a line to the server log indicating the object, verb, and programmer)
    - OWNERSHIP_QUOTA (disable the server's builtin quota management)
    - USE_ANCESTOR_CACHE (enable a cache of an object's ancestors to speed up property lookups)
    - UNSAFE_FIO (skip character by character line verification, trading off potential safety for speed)
    - LOG_EVALS (add an entry to the server log any time eval is called)

- Additional builtins:
    - frandom (random floats)
    - distance (calculate the distance between an arbitrary number of points)
    - relative_heading (a relative bearing between two points)
    - memory_info (total memory used, resident set size, shared pages, text, data + stack)
    - ftime (precise time, including an argument for monotonic timing)
    - locate_by_name (quickly locate objects by their name)
    - usage (returns {load averages}, user time, system time, page reclaims, page faults, block input ops, block output ops, voluntary context switches, involuntary context switches, signals received)
    - explode (serverified version of the LambdaCore verb)
    - occupants (return a list of objects of parent parent, optionally with a player flag check)
    - spellcheck (uses Aspell to check spelling)
    - locations (recursive location function)
    - clear_ancestor_cache (clears the ancestor cache manually)

- Miscellaneous changes:
    - Numeric IP addresses in connection_name
    - Detect stunnel connections and rewrite the source IP as appropriate (controllable with $server_options.proxy_rewrite)
    - .last_move (a map of an object's last location and the time() it moved)
    - Sub-second fork and suspend
    - Call 'do_blank_command' on listening objects when a blank command is issued
    - Allow '"string" in "some other string"' as a shortcut for index()
    - Allow exec to set environment variables with a new argument
    - Bandaid over an issue where emptylist loses all references and gets freed, causing a server panic
    - Change the server log message when calling switch_player()
    - Complete deprecation of tonum()
    - Move #0.dump_interval to $server_options.dump_interval
    - Rename recycle() to destroy() (also call pre_destroy rather than recycle verbs)
    - New argument to notify() to suppress the newline
    - Support object lists in isa() as well as an optional third argument to return the matching parent rather than simply true or false.

