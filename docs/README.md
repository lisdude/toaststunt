# ToastStunt

ToastStunt is a network accessible, multi-user, programmable, interactive system used in the creation of both text-based and web-based experiences. The most common usage is the creation of MUDs (examples include [Miriani](https://www.toastsoft.net) and [ChatMud](https://www.chatmud.com/)). ToastStunt is a fork of [Stunt](https://github.com/toddsundsted/stunt), which is a fork of [LambdaMOO](https://github.com/wrog/lambdamoo). It builds upon those projects to add improved performance, modern conveniences, and an improved user experience.

* [Features](#features)
* [ChangeLog](ChangeLog.md)
* [Build Instructions](#build-instructions)
  * [Debian/Ubuntu/WSL](#debianubuntuwsl)
  * [Gentoo](#gentoo)
  * [FreeBSD](#freebsd)
  * [macOS](#macos)
* [Function Documentation](https://github.com/lisdude/toaststunt-documentation)
* [Getting Started Guide](https://lisdude.com/moo/toaststunt_newbie.txt)
* [ToastCore](https://github.com/lisdude/toastcore)
* [Stunt Information](Legacy/README/README.Stunt)

<a href = "https://discord.gg/JUz3zwZamW"><img alt="Join Discord" src="https://img.shields.io/discord/738251170140651560?label=Discord&style=plastic"></a>

## Features

- SQLite
- Perl Compatible Regular Expressions (PCRE)
- [Argon2id Hashing](https://github.com/P-H-C/phc-winner-argon2)
- 64-bit Integers (with the choice to fall back to 32-bit integers; $maxint and $minint set automatically)
- HAProxy Source IP Rewriting (see [notes](#login-screen-not-showing) below if you need to disable this)
- User friendly traceback error messages

- Networking improvements:
    - IPv6 connection support
    - Threaded DNS lookups
    - Secure TLS connections in `listen()` and `open_network_connection()`

- Waifs:
    - Call :recycle on waifs when they're destroyed
    - A WAIF type (so typeof(some_waif) == WAIF)
    - Waif dict patch (so waif[x] and waif[x] = y will call the :_index and :_set_index verbs on the waif)
    - '-w' command line option to convert existing databases with a different waif type to the new waif type
    - `waif_stats()` (show how many instances of each class of waif exist, how many waifs are pending recycling, and how many waifs in total exist)
    - Parser recognition for waif properties (e.g. thing.:property)

- Basic threading support:
    - background.cc (a library, of sorts, to make it easier to thread builtins)
    - Threaded builtins: sqlite_query, sqlite_execute, locate_by_name, sort, argon2, argon2_verify, connection_name_lookup
    - set_thread_mode (an argument of 0 will disable threading for all builtins in the current verb, 1 will re-enable, and no arguments will print the current mode)
    - `thread_pool()` (database control over the the thread pools)

- FileIO improvements:
    - Faster reading
    - Open as many files as you want, configurable with FILE_IO_MAX_FILES or $server_options.file_io_max_files
    - `file_handles()` (returns a list of open files)
    - `file_grep()` (search for a string in a file (kind of FUP in FIO, don't tell))
    - `file_count_lines()` (counts the number of lines in a file)

- Profiling and debugging:
    - `finished_tasks()` (returns a list of the last X tasks to finish executing, including their total execution time) [see options.h below]
    - Set a maximum lag threshold (can be overridden with $server_options.task_lag_threshold) that, when exceeded, will make a note in the server log and call #0:handle_lagging_task with arguments: {callers, execution time}
    - Include a map of defined variables for stack frames when being passed to `handle_uncaught_error`, `handle_task_timeout`, and `handle_lagging_task` [see options.h below]
    - Optionally capture defined variables for running tasks by providing a true argument to `queued_tasks()` or a third true argument to `task_stack()`.

- Telnet:
    - Capture IAC commands and pass them to listener:do_out_of_band_command() for the database to handle.

- Stunt Improvements:
    - Primitive types:
        - Support calling verbs on an object prototype ($obj_proto) for un-logged-in connection objects.
    - Maps:
        - `maphaskey()` (check if a key exists in a map)

- [Numerous new configuration options](Features/new_options.md)

- [Several new built-in functions](Features/new_builtins.md)

- [Many miscellaneous changes](Features/new_miscellaneous.md)

## Build Instructions
### **Debian/Ubuntu/WSL**
```bash
apt install build-essential bison gperf cmake libsqlite3-dev libaspell-dev libpcre3-dev nettle-dev g++ libcurl4-openssl-dev libargon2-dev libssl-dev
mkdir build && cd build
cmake ../
make -j2
```

### **Gentoo**
```bash
emerge dev-db/sqlite app-text/aspell app-dicts/aspell-en app-crypt/argon2 dev-utils/cmake dev-libs/libpcre
mkdir build && cd build
cmake ../
make -j2
```

### **FreeBSD**
```bash
pkg install bison gperf gcc cmake sqlite3 aspell pcre nettle libargon2
mkdir build && cd build
cmake ../
make -j2
```

### **macOS**
Installing dependencies requires [Homebrew](https://brew.sh/).

If using OpenSSL, you may have to export an environment variable before running CMake:
- Apple Silicon: `export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig"`
- Intel: `export PKG_CONFIG_PATH="/usr/local/opt/openssl@3/lib/pkgconfig"`

```bash
brew install pcre aspell nettle cmake openssl argon2
mkdir build && cd build
cmake ../
make -j2
```

## **Notes**
### CMake Build Options
There are a few build options available to developers:

| Build Option | Effect                                                                       |
|--------------|------------------------------------------------------------------------------|
| Release      | Optimizations enabled, warnings disabled.                                    |
| Debug        | Optimizations disabled, debug enabled.                                       |
| Warn         | Optimizations enabled, warnings enabled. (Previous default behavior)         |
| LeakCheck    | Minimal optimizations enabled, debug enabled, and address sanitizer enabled. |

To change the build, use: `cmake -DCMAKE_BUILD_TYPE=BuildNameHere ../`

### Login screen not showing
Due to the way proxy detection works in versions of the server prior to 2.7.1_22, if you're connecting to your MOO from localhost, you won't see the login screen. This is a minor inconvenience and shouldn't affect your ability to actually use your MOO. However, if it bothers you, you can disable HAProxy rewriting:

1. `@prop $server_options.proxy_rewrite 0`
2. `;load_server_options()`

**NOTE**: Newer versions of the server control proxy rewriting via the `$server_options.trusted_proxies` property instead.