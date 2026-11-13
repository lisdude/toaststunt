- Additional builtins:
    - frandom (random floats)
    - distance (calculate the distance between an arbitrary number of points)
    - relative_heading (a relative bearing between two coordinate sets)
    - memory_usage (total memory used, resident set size, shared pages, text, data + stack)
    - ftime (precise time, including an argument for monotonic timing)
    - locate_by_name (quickly locate objects by their .name property)
    - usage (returns {load averages}, user time, system time, page reclaims, page faults, block input ops, block output ops, voluntary context switches, involuntary context switches, signals received)
    - explode (serverified version of the LambdaCore verb on $string_utils)
    - slice (serverified version of the LambdaCore verb on $list_utils)
    - occupants (return a list of objects of parent parent, optionally with a player flag check)
    - spellcheck (uses Aspell to check spelling)
    - locations (recursive location function)
    - clear_ancestor_cache (clears the ancestor cache manually)
    - property_exists (checks object, anonymous-object, or waif properties, optionally excluding inherited definitions; see below)
    - chr (return extended ASCII characters; characters that can corrupt your database are considered invalid)
    - reseed_random (reseed the random number generator)
    - yin (yield if needed. Replicates :suspend_if_needed and ticks_left() checks)
    - sort (a significantly faster replacement for the :sort verb. Also allows for natural sort order and reverse sorting)
    - recreate (fill holes created by recycle() by recreating valid objects with those object numbers)
    - recycled_objects (return a list of all objects destroyed by calling recycle())
    - next_recycled_object (return the next object available for recreation)
    - reverse (reverse lists)
    - all_members (return the indices of all instances of a type in a list)
    - curl (HTTP client: GET/POST/PUT/PATCH/DELETE/HEAD/OPTIONS with request headers, request bodies, JSON serialization/parsing, and size/timeout caps)
    - owned_objects (returns all valid objects owned by an object)
    - connection_name_lookup (perform a DNS name lookup)
    - connection_info (show detailed information about a particular connection)
    - parse_ansi (parses color tags into their ANSI equivalents)
    - remove_ansi (strips ANSI tags from strings)

## property_exists

```moo
property_exists(target, property_name [, direct_only])
```

Returns integer `1` if the property exists, otherwise `0`. `target` can be an
object, anonymous object, or waif; `property_name` is a case-insensitive string.
The optional `direct_only` argument must be an integer (default `0`). Nonzero
values exclude inherited definitions. A value of `0`, an empty string, or a
clear/inherited value does not make an existing property missing.

For a waif, use the same name as a normal property read: `"color"` corresponds to
its class's `":color"` definition. Inherited prefixed definitions count by default;
with `direct_only`, the definition must be on the waif's class itself. Assigning an
instance value does not create a direct definition. Ordinary unprefixed class
properties are not exposed on the waif. A supplied leading colon is not stripped:
`":color"` looks for a class definition named `"::color"`.

The intrinsic waif properties `class`, `owner`, `wizard`, and `programmer` exist,
but return `0` with `direct_only`, just as intrinsic properties do on ordinary
objects. Waif class properties with those prefixed names do not override the
intrinsics. The lookup does not invoke waif dictionary/index verbs or read or copy
the property value.

Existing unreadable properties raise `E_PERM`, including inherited properties
when `direct_only` is set. Waif permissions follow normal waif reads: `+r` grants
read access, a `+c` property uses the waif owner, other properties use their property
owner, and wizards bypass the read restriction. Object readability is not a
separate requirement for this builtin.

Invalid objects, invalid anonymous objects, and waifs whose classes have been
recycled raise `E_INVARG`, even for intrinsic or missing property names. This is
the builtin's invalid-target behavior; ordinary waif property reads keep their
existing behavior. Lookup synchronizes the waif with class property and parent
changes before checking. Incorrect argument types raise `E_TYPE`; incorrect
argument counts raise `E_ARGS`. Missing properties return `0`, not `E_PROPNF`.
