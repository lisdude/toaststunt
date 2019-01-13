See `README.lambdamoo' for general information on LambdaMOO.

More information on Stunt is available here: http://stunt.io/.  If you
want to get up and running quickly, consider starting with Improvise,
the Stunt starter kit: https://github.com/toddsundsted/improvise.

This is Release 10-wip of the Stunt extensions to the LambdaMOO
server.  It is based on the latest "1.8.3+" version from SourceForge.
Read the very important WARNING below before running this on your
existing database!!!

Release 10 is based on a "rewrite" of Stunt in C++ (the word "rewrite"
here means that I made the minimal necessary syntactical and structural
changes to get the project to compile on a modern C++ compiler) (shout
out to Steve Wainstead who paved the road here).  This makes way for
major improvements down the road.  Release 10 also includes many
smaller features noted below.

Stunt includes the following functionality not found in the main
server:

1)  Multiple-Inheritance
    `create()' now takes either an object number or a list of object
    numbers in the first argument position.  Two new built-ins,
    `parents()' and `chparents()', manipulate an object's parents.
    The built-ins `parent()' and `chparent()' exist for backward
    compatibility -- when an object has multiple parents these
    built-ins operate on the first parent.

2)  Anonymous objects
    Objects without an assigned object number that are accessible via
    an unforgeable references, and which are automatically garbage
    collected when no longer reachable.

3)  Task Local Storage
    The built-ins `task_local()' and `set_task_local()' retrieve/store
    a task local value.  The value is automatically freed when the
    task finishes.

4)  Map Datatype
    The server includes a native map datatype based on a binary search
    tree (specifically, a red-black tree).  The implementation allows
    in-order traversal, efficient lookup/insertion/deletion, and
    supports existing MOO datatypes as values (keys may not be lists or
    maps).  Index, range, and looping operations on lists and strings
    also work on maps.

5)  JSON Parsing/Generation
    The built-ins `parse_json()' and `generate_json()' transform MOO
    datatypes from/to JSON.

6)  New Built-in Cryptographic Operations
    The new cryptographic operations include SHA-1 and SHA-256 hashes.
    The existing MD5 hash algorithm is broken from a cryptographic
    standpoint, as is SHA-1 -- both are included for interoperability
    with existing applications (both are still popular) but the default
    for `string_hash()'/`binary_hash()'/`value_hash()' is now SHA-256.
    Stunt also includes the HMAC-SHA-1 and HMAC-SHA-256 algorithms for
    generating secure, hash-based message authentication codes.
    `crypt()' has been upgraded to support Bcrypt hashed passwords.
    The random number subsystem is now based on the SOSEMANUK cipher
    and seeds itself from `/dev/random'.

7)  Built-in Base64 Encoding/Decoding
    The built-ins `encode_base64()' and `decode_base64()' encode and
    decode Base64-encoded strings.

8)  An Improved FileIO Patch
    The 1.5p1 patch that is in wide circulation has flaws, including two
    server crashing bugs.  This patch fixes those bugs/flaws without
    changing the API.

9)  Secure Suspending Process Exec
    The exec functionality adds an `exec()' built-in which securely
    forks/execs an external executable.  It doesn't use the `system()'
    call, which is hard to secure and which blocks the server.
    `exec()' takes two parameters, a list of strings comprising the
    program and its arguments, and a MOO binary string that is sent to
    stdin.  It suspends the current task so the server can continue
    serving other tasks, and eventually returns the process termination
    code, stdout, and stderr in a list.

10) Verb Calls on Primitive Types
    The server supports verbs calls on primitive types (numbers,
    strings, etc.) so calls like `"foo bar":split()' can be
    implemented and work as expected (they were always syntactically
    correct but resulted in an E_TYPE error).  Verbs are implemented
    on prototype object delegates ($int_proto, $float_proto,
    $str_proto, etc.).  The server transparently invokes the correct
    verb on the appropriate prototype -- the primitive value is the
    value of `this'.

11) In Server HTTP Parsing
    The server uses the excellent Node HTTP parsing library to
    natively parse HTTP requests and responses into key/value pairs in
    a map datatype.  The parser handles corner cases correctly and
    supports things like HTTP upgrade (for using WebSockets, for
    example).  It's also much much faster than parsers implemented in
    MOO code.

12) Colorized Logging and Line Editing
    The server writes colorized output to the log when attached to a
    console.  The `server_log()' built-in allows colorized output from
    within the server.  And line editing is supported in emergency
    wizard mode.

13) Bitwise Operators
    The server supports bitwise and (&.), or (|.), xor (^.), logical
    (not arithmetic) right-shift (<<), logical left-shift (>>), and
    complement (~) operators.

14) Testing Framework
    The server includes a unit testing framework based on Ruby's
    Test-Unit.  It includes a Parslet parser (two, actually) for
    turning moo-code values into Ruby values, which makes writing
    tests much easier.  The new code is covered very well by the
    existing tests.

WARNING: This server changes the database format in a non-backward
compatible way in order to persist multiple-parent relationships.  The
server will automatically upgrade version 4 databases, however THERE
IS NO WAY BACK!

Use Github and the Github issue system for feedback and bugs!  See
CONTRIBUTING for details on how to contribute.

Todd
