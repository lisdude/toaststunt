# ToastStunt ChangeLog

## 2.6.0 (In Progress)
- Add an `owned_objects(OBJ <who>)` builtin to return a list of valid objects owned by who.
- Fix a security oversight where `recreate()` could allow the recreation of an object that already owns other objects, verbs, or properties. Now the `recycle()` function will correct ownership of anything owned by the object being recycled, though at a slight cost to speed on larger databases. If the speed hit proves to be too much and you know what you're doing, you can disable the `SAFE_RECYCLE` option in options.h.
- Add the `NO_FORKED_LOOKUP` option to options.h. When enabled, the server won't spawn a separate name lookup task, nor will it do any DNS name lookups at all.
- Add the `name_lookup(<IP address>)` function to perform a DNS lookup on an IP address in the background. Note that this function implicitly suspends, so if you use it in do_login_command you'll also need to use `switch_player()` to work around the no-suspend-at-login rule.
- Add the `TOTAL_DNS_THREADS` option to options.h to control how many threads are given to `name_lookup()` at runtime.
- Add a `thread_pool(<function>, <pool> [, <value>])` function that allows control over the thread pools from within the database.
- Remove the `process_id()` builtin function.

## 2.5.13 (Oct 14, 2019)
- Add a `sqlite_limit()` builtin to limit the size of various SQLite constructs. [More information](https://www.sqlite.org/c3ref/c_limit_attached.html)
- Add the `-m` command line option for clearing the last_move builtin property on all objects in the database (and not setting it again for the lifetime of the server process).
- Move user-visible threading options into `options.h`.
- Add a `curl(<url>, <?include headers>)` function to easily return a webpage as a string.
- Fix a security oversight where the `chr()` function would allow non-wizards to produce telnet IAC characters.
- The `explode()` function now only considers the first character of the delimiter to more closely match the LambdaCore behavior.
- Add a second argument to the `locations()` function to indicate an alternate stopping point. If no such point exists, it falls back to $nothing.

## 2.5.12 (Oct 3, 2019)
- The `chr()` function can now accept any number of arguments, similar to how `encode_binary()` works.
- Add a `recycled_objects()` builtin, which will return a list of all invalid object numbers currently in the database.
- Return `recycle()` to its final glory and call `:recycle` on objects and waifs before destroying them instead of `:pre_destroy`. (Standard LambdaMOO functionality.)
- Add a `next_recycled_object(<?starting-object>)` builtin, which will iterate through the object hierarchy to find the lowest object that is invalid and return it. If none are available, 0 is returned. Providing an object as the first argument will start the search at that object, in the unlikely event that you only want to find non-valid objects passed a certain point.

**WARNING**: This breaks compatibility with old code that uses `destroy()` or relies on `:pre_destroy` being called. You will need to open your database file in a text editor and replace `destroy` and `pre_destroy` with `recycle`.

## 2.5.11 (Sep 30, 2019)
- Catch SIGUSR1 and, if the server was started with a log file, close and reopen the file. This way scripts can move the old log file and `kill -SIGUSR1 <pid>` to rotate logs without restarting the server.
- Add an `all_members(<value>, <list>)` function to return the indices of all occurances of <value> in <list>.
- Improve performance of string comparisons by using the pre-computed length (if `MEMO_STRLEN` is enabled in options.h) of strings to rule out equality before doing a character by character comparison.
- Add the ability to specify the default thread mode in `extension-background.h`. By default (true), threaded functions are threaded unless you explicitly `set_thread_mode(0)`. When set to false, threaded functions are not threaded until you explicitly `set_thread_mode(1)`

**WARNING**: This redefines the SIGUSR1 signal to mean 'reopen logfile' rather than 'shutdown'. If you rely on the old behavior, you will need to update your scripts accordingly.

## 2.5.10 (Sep 16, 2019)
- Fix a bug in `slice()` where the server would crash if `slice()` raised an error in the middle of processing a list.
- Add missing support for strings to `slice()`.
- Add support to `slice()` for slicing maps by key. At this time it doesn't support lists of keys.
- Increase the performance of the `explode()` builtin.
- Add a third argument to the `explode()` builtin to indicate that sequential occurences of the delimiter should be included in the return result.
- Fix a memory leak when clearing the ancestor cache.
- The `reverse()` function now accepts strings.

## 2.5.9 (Sep 12, 2019)
- Add a `reverse(<list>)` function to reverse lists.
- Add a `recreate(<object>, <parent>, <?owner>)` function. This will effectively fill in any holes created by destroy()ing an object.
- Disabled the Nagle algorithm and delayed acks for better network performance.
- Allow `occupants()` to accept a list of parents to check each object against.
- Add `THREAD_ARGON2` to options.h to allow `argon2()` and `argon2_verify()` to be threaded. There are significant issues to be aware of when enabling this option, so please read the comment in options.h.
- Fixed a small bug that would limit 64-bit MOOs to 'only' being able to create 2147483647 objects instead of 9223372036854775807 objects.

**WARNING**: This potentially breaks compatibility with old code that relies on `argon2()` raising errors. Due to threading limitations, it now returns errors as a map with the keys `error` and `message`.

## 2.5.8 (Sep 9, 2019)
- `sort()` more appropriately returns a type mismatch when given an incorrect type rather than an invalid argument.
- Fix a bug where `sort()` would crash the server when reversing a sort with identical strings.
- Fix a bug in `yin()` where the ticks and seconds remaining checks were inaccurate.
- Improved the performance of `sort()`
- Fix a bug in `locate_by_name()` where it was possibly to access memory that wasn't what we thought, resulting in mangled and inconsistent output.
- Fix a bug in `isa()` that could cause a server panic when checking an object against a list of parents.
- Add a `set_thread_mode(<?mode>)` function that can be used to disable threading of functions for a particular verb. No arguments will display the current thread mode.
- Update `slice()` to run in a background thread as well as to accept a list of arguments as the index in addition to a numeric index.
- Fix a bug where macOS would try (and fail) to use virtual timers, resulting in server panics.

**WARNING**: This version increments the database version (DBV_Threaded), making databases incompatible with previous releases.

## 2.5.7 (Aug 29, 2019)
- Fix a bug where `yin()` would suspend a task indefinitely by default rather than 0 seconds.
- Add a threaded `sort(<list>, <?keys>, <?natural order>, <?reverse>)` function that will... sort. This provides a significant performance advantage over the default LambdaCore sort verbs.

## 2.5.6 (Aug 27, 2019)
- Add a `yin(<?time>, <?minimum ticks>, <?minimum seconds>)` function (yield if needed). This is analogous to the suspend_if_needed function in LambdaCore databases. It will suspend the currently running verb if it finds it's approaching the user-defined tick / second limit.
- Run `locate_by_name()` in a background thread.

## 2.5.5 (May 14, 2019)
- Add a `slice()` builtin. *Note: It only supports indexing numerically, not by list.*
- Add an argument to `random()` to allow ranged random operations.
- Various fixes for macOS.
- Add a field to `server_version()` that will return the name of the OS the server is currently running on.
- `random()` is now seeded with more than 32 bits.
- Added a `reseed_random()` function to reseed the random number generator.
- Add a `finished_tasks()` function to track the execution time of tasks as they finish. This can be disabled with the `SAVE_FINISHED_TASKS` option in options.h.
- Add support for calling `$sysobj:handle_lagging_task()` when a task's execution time exceeds that set in `$server_options.tag_lag_threshold`
- Add `$server_options.finished_tasks_limit` to override the number of finished tasks that get saved.

## 2.5.4 (Apr 19, 2019)
- Fix a bug in PCRE that could result in memory corruption when encountering an excessive substring error.
- Fix a bug in `disassemble()` that could cause a server panic.
- Fix a bug in SQLite that could cause a server panic when sanitizing empty strings.
- Fix a bug / exploit where sending large amounts of data to the MOO in single chunks would crash the server. (See MAX_LINE_BYTES in options.h)

## 2.5.3 (Mar 4, 2019)
- Fix a bug where negating a float would return a bogus int.
- Add a third argument to `move()` that will insert the object anywhere in the destination's .contents property that you wish.
- Add a `maphaskey()` function as a faster alternative to indexing `mapkeys()`
- Fix a bug where newlines wouldn't be detected properly when reading input.
- Fix a bug where `enterfunc` and `exitfunc` would get called on objects that hadn't actually moved.

## 2.5.2 (Feb 13, 2019)
- Waif `:recycle` is now `:pre_destroy`
- Fix a bug where instantiating a waif with an anonymous object would cause a server panic.
- Fix a bug where waifs with a destroyed parent class would cause a server panic upon being accessed.
- Fix a bug where waifs could be programmers.
- Fix a bug where chparenting a waif's parent wouldn't make instantiated waifs aware of their newly inherited properties.
- Fix a bug where old Stunt databases wouldn't get converted properly, rendering `last_move` values as useless garbage.

**WARNING** This breaks compatibility with old waif code that uses `:recycle`. You will need to open your database file in a text editor and replace `:recycle` with `:pre_destroy`

## 2.5.1 (Feb 10, 2019)
- Fix a bug with waif recycling that could result in a server panic. (Thanks to Sorressean for the fix)
- Fix a bug where `disassemble()` wouldn't display floats correctly.
- Fix a bug in `argon2()` where free was being used instead of myfree. (Thanks to ethindp for the fix)
- Add `ONLY_32_BITS` to options.h to run the server in 32-bit mode rather than 64-bit mode.
- Automatically set `$maxint` and `$minint` (if present) at server start.
- Anonymous objects can now act as map keys.
- Fix a bug where using waifs or anonymous objects as map keys in `generate_json()` would cause a server panic.
- Fix an off by one error when reading files with the unsafe FIO option set.
- Call `pre_destroy` instead of `recycle` on anonymous objects.
- Unit tests are now compatible with ToastStunt changes.

**WARNING** This breaks compatibility with old code that uses `recycle()`. You will need to open your database file in a text editor and replace `recycle` with `pre_destroy`

## 2.5.0 (Feb 1, 2019)
- Hostnames for proxy IPs are now resolved rather than being numeric only.
- Improve performance of `locate_by_name()`
- Finalize 64-bit support

## 2.4.6 (Jan 29, 2019)
- Fix a boundary issue when extracted telnet IAC sequences from the middle of input.
- Add a `chr()` function that returns the symbol associated with the ASCII value provided.

## 2.4.5 (Jan 18, 2019)
- Fix a bug in waif recycling when garbage collection is disabled.
- Fix a bug in `waif_stats()` where classes with no instantiated waifs would still be present in the list.
- Fix a bug in telnet IAC support when IAC sequences are mixed with in-band data.
- Allow primitive calls on OBJ types, which counterintuitively is only useful for invalid objects.

## 2.4.4 (Jan 13, 2019)
- Improve the performance of `argon2()`
- Telnet IAC commands now get captured and passed, as binary strings, to a `do_out_of_band_command` verb on the listener.

## 2.4.3 (Jan 3, 2019)
- Add a third argument to `isa()` that will return the object stopped on as the parent. If there is no match, #-1 is returned.
- Fix a bug in `notify()` where the incorrect argument was being tested to disable newlines.
- Fix a memory leak in background threads.

## 2.4.1 (Dec 28, 2018)
- Threads now exist in a thread pool to remove the overhead associated with constantly creating and destroying threads.
- SQLite connections are now locked until background worker threads have finished their work. This prevents them from being closed at inopportune times.
- The second argument to `isa()` can now be a list of objects.

## 2.4.0 (Dec 19, 2018)
- Add a wizard-only `clear_ancestor_cache()` function to manually reset the ancestor cache.
- Fix a memory leak in `locations()`
- Fix a bug in the background thread library that would result in a background task not resuming if there was no activity in the MOO.
- Add the function `threads()`, which returns a list of active thread handles.
- Add a `thread_info()` function to return basic information about a running thread. Each threaded function will now return a string in thread_info indicating what it is.
- The `sqlite_execute()` and `sqlite_query()` functions now run in a separate thread. Each now has an additional argument to disable threading.

## 2.3.21 (Dec 5, 2018)
- Add `locations()` function that will return all nested locations of an object by recursing through .location.
- `spellcheck()` now returns 1 for correct words rather than an empty list.

## 2.3.20 (Nov 29, 2018)
- Call `pre_destroy()` verb on objects prior to their destruction. This replaces the previous `recycle()` verb.
- Add an additional argument to `notify()` to suppress the addition of a newline.
- Add recognition of waif properties to the parser. This means you can now do things like `somewaif.:someproperty` directly.
- Add a `spellcheck()` function to check spelling using Aspell.

**WARNING** This breaks compatibility with old code that uses `recycle()`. You will need to open your database file in a text editor and replace `recycle` with `pre_destroy`

## 2.3.19 (Nov 12, 2018)
- Rename `recycle()` to `destroy()`

**WARNING** This breaks compatibility with old code that uses `recycle()`. You will need to open your database file in a text editor and replace `recycle` with `destroy`

## 2.3.18 (Nov 12, 2018)
- Use `$server_options.dump_interval` instead of `$sysobj.dump_interval`

## 2.3.17 (Nov 3, 2018)
- Remove `tonum()`, thus completing the 30 year deprecation in favor of `toint()`
- Properly sanitize PCRE replacement strings to avoid introducing dangerous or annoying characters.
- The ancestor cache now gets invalidated when renumbering an object or resetting the max object.

**WARNING** This breaks compatibility with old code that uses `tonum()`. You will need to open your database file in a text editor and replace `tonum` with `toint`

## 2.3.16 (Sep 16, 2018)
- Fix a memory corruption bug in SQLite.
- Fix a bug in `simplex_noise()` that would result in a server panic if the last element of the list wasn't a float.
- Add `LOG_EVALS` to options.h to allow all evals to be written to the server log.

## 2.3.15 (Jul 11, 2018)
- Adds support for 64-bit integers.
- Fix a bug in `usage()` where the types weren't correctly set.
- Fix a bug in `ctime()` where certain arguments could cause a server panic.
- Remove an unused argument from `switch_player()`
- Replace 'CONNECTED:' with 'SWITCHED:' in the server log file when using `switch_player()`
- Fix a bug in PCRE where named capture groups wouldn't get counted.
- Fix a memory leak in PCRE.
- Add support for [Argon2](https://en.wikipedia.org/wiki/Argon2) hashing with the functions `argon2()` and `argon2_verify()`

## 2.3.14 (Jun 18, 2018)
- Call the `recycle` verb on instantiated waifs before they're actually destroyed.
- Add 'pending_recycle' to `waif_stats()`
- Rewrote `pcre_match()` to return a list of maps.

**WARNING**: This breaks compatibility with old code relying on the output of `pcre_match()`.

## 2.3.13 (Jun 17, 2018)
- Improved PCRE refcounting and performance.
- Improve the robustness of the canonical empty list by never allowing its refcount to fall to 0.
- Use `myrealloc()` to grow streams.
- Add `UNSAFE_FIO` to options.h. This allows you to skip the character by character line verification for a small performance boost. Make sure to read the disclaimer above it in options.h.
- Avoid growing streams when we know (more or less) how large they're going to be. Instead, allocate all of the memory at once.

## 2.3.9 (Jun 13, 2018)
- Fix a memory leak in `file_count_lines()`
- Fix a buffer overflow in PCRE.

## 2.3.8 (Jun 11, 2018)
- Fix a bug where waifs wouldn't be freed properly with garbage collection disabled.
- Count the number of instantiated waifs per class in `waif_stats()` in addition to the total.
- Add a third argument to the `exec()` function that allows the setup of environment variables.

## 2.3.7 (Jun 9, 2018)
- Fix ancestor cache refcounting issues. This has the happy consequence of slightly improving performance.

## 2.3.6 (Jun 6, 2018)
- Fix a memory leak in the ancestor cache.
- Improved the performance of the ancestor cache.
- Invalidate the ancestor cache for descendants of the object whose parent changed.
- Fix a server panic when attempting to use `mapdelete()` on a waif.

## 2.3.5 (Jun 5, 2018)
- Add `OWNERSHIP_QUOTA` to options.h to control whether or not the server's default ownership quota management is enabled or not. It now defaults to disabled to allow the database to handle quota on its own.
- Improve validation of file handles to limit phantom handles.
- The server now caches an object's list of ancestors. This should improve performance of property lookups. Since this functionality is experimental, it can be disabled with the `USE_ANCESTOR_CACHE` option in options.h.

## 2.3.4 (Jun 3, 2018)
- Replace the `last_location` property with `last_move`, which now returns a map with keys for the time the object moved and the location it moved from.
- Add a call to `$sysobj:do_blank_command()` when a blank command is issued from localhost. The default behavior (return 0) will assume that the server should ignore the blank command. If the verb returns 1, the server will process the command normally.
- Fix a bug with SQLite handles that will prevent phantom handles from appearing with blank information.
- Return a map from `sqlite_info()` instead of a list.
- Add convenience syntax to `in`, allowing you to use the syntax `""123" in "abc123"` instead of `index("abc123", "123")`
- Fix a bug where a large file handle would be mistakenly interpreted as meaning that many files were still open.
- Fix a bug in `locate_by_name` where the second argument had an unknown type.

**WARNING**: This release is incompatible with previous releases containing the `last_location` property. You'll need to open your database file in a text editor and replace `last_location` with `last_move`.

## 2.3.3 (May 30, 2018)
- Add an `explode()` builtin function to mimic the LambdaCore `$string_utils:explode()` verb with better performance.
- Fix bugs with `last_location` persistence and memory usage.
- Add a `waif_stats()` builtin to display how many waifs are currently instantiated.
- Fix a bug with WAIF_DICT to make it possible to index the waif properly.
- Add an `occupants()` function to return a list of objects matching certain criteria, including parent and player flag. (See ToastCore documentation for full usage details.)

**WARNING**: This version increments the database version (DBV_Last_Location), making databases incompatible with previous releases.

## 2.3.2 (May 29, 2018)
- Add sub-second forking and suspending. e.g. `suspend(0.5)`
- Add a `last_location` property to all objects to indicate their previous location after a `move()`

## 2.3.0 (May 28, 2018)
- Removed the PCRE pattern cache, as it was of dubious benefit.
- Adapted Ben Jackson's waif patch for Stunt.
- Added the `WAIF` symbol.
- IP addresses from from TCP proxies using the HAProxy Proxy protocol now have their source IP rewritten to match their actual IP rather than the local IP.
- Added `PROXY_REWRITE` to $server_options to control whether IPs from proxies get rewritten.
- Added the `simplex_noise()` function to generate [Simplex Noise](https://en.wikipedia.org/wiki/Simplex_noise).
- Added a startup flag to manually specify the waif type of a v4 LambdaMOO database for conversion. So, for instance, if your v4 database has a waif type of 10 (`typeof(somewaif)`), you can run your server with the flag `-w 10` to automatically convert waifs from the v4 format to the new ToastStunt type. *This flag only has to be specified the first time you run your v4 database with ToastStunt*
- Fix a bug where the number of objects being written to disk was incorrectly reported.

**WARNING**: This version increments the database version (DBV_Waif), making databases incompatible with previous releases.

## 2.2.0 (Nov 29, 2017)
- Added a threading library (extension-background) to allow server functions to run in a separate thread. To protect the database, these functions will implicitly suspend the MOO code (similar to how read() operates)

## 2.1.1 (Sep 8, 2017)
- Add `FILE_IO_MAX_FILES` to $server_options to allow DB-changeable limits on how many files can be opened at once.
- Replace the SQLite handle array with an std::map.
- Add `SQLITE_MAX_HANDLES` to $server_options to allow DB-changeable limits on how many SQLite connections can be opened at once.

## 2.1.0 (Sep 6, 2017)
- Add `LOG_CODE_CHANGES` to options.h to write to the log file who changed what verb code.
- Added numeric IP addresses to `connection_name()`.
- Replace the file table array with an std::map, which effectively removes the restriction on the number of files that can be opened at once.

## 2.0.4 (Sep 3, 2017)
- Add `frandom()` function to generate floating point random numbers.
- Add `round()` function to round a number.
- Add `distance()` function to calculate the distance between X number of points.
- Add `relative_heading()` function to calculate the bearing between two coordinate sets.
- Add `memory_usage()` function to return information about the memory usage of the MOO process.
- Add `ftime()` function to return the time in milliseconds.
- Add `panic()` function to manually panic the server from in-MOO.
- Add `locate_by_name()` function to quickly locate an object by its .name.
- Add `parse_ansi()` and `remove_ansi()` functions to move ANSI code parsing outside of the database.
- Add `file_grep()` function to search the contents of a file.
- Add `file_handles()` function to return a list of open file handles.
- Add `file_count_lines()` function to return the total number of lines in a file.
- Improved the performance of file reads.
- Add `usage()` function to report information about the system the MOO is running on, including: load averages, user time, system time, page reclaims, page faults, block input, block output, voluntary context switches, involuntary context switches, and signals received.

## 1.8.3+toastsoft_2 (Sep 2, 2017)
- Added SQLite support.
- Add PCRE support.
- Add additional entropy by default.
