# implode

`implode(LIST values [, ANY separator]) => STR`

Convert each element using the same representation as `tostr()` and concatenate
in list order, inserting `separator` only between elements. The separator defaults
to `""`; an explicit separator can be any MOO value and is also converted with
`tostr()` semantics. It is not converted for an empty or singleton list.

```moo
implode({})                           => ""
implode({"a", "b"})                   => "ab"
implode({"a", "b"}, "")               => "ab"
implode({"a", "b"}, ", ")             => "a, b"
implode({"a", "b"}, 42)               => "a42b"
implode({"", "b", ""}, ",")           => ",b,"
implode({{1, 2}, ["x" -> 3]}, "|")    => "{list}|[map]"
implode({true, false, #0}, " ")       => "true false #0"
```

This preserves case, whitespace, blank elements and literal MOO binary escape
text. It does not decode `~00` sequences. Nested collections use `tostr()`'s
`{list}` / `[map]` placeholders, rather than `toliteral()` serialization.

Zero arguments or more than two arguments raise `E_ARGS`. A non-list first
argument raises `E_TYPE`. Output uses the same stream limit and error handling
as `tostr()`: `$server_options.max_string_concat` limits the string size; overflow
raises `E_QUOTA` when `max_concat_catchable` is true and otherwise aborts the task.
The standard server minimum and disabled-limit rules apply.

Calls use the standard `protect_implode` option and `#0:bf_implode` override.
Element conversion happens inside the engine. If a database customizes or
restricts `tostr()` through `protect_tostr`, review that policy before switching
callers since the reference verb calls `tostr()` explicitly.

The engine appends converted elements and separators into one growable stream.
It does not repeatedly copy the accumulated string as the reference MOO loop does.
`implode()` charges the normal builtin-call tick cost, not a tick per element; CPU
work still grows with the output size. Large inputs can therefore use substantial
CPU within one call, as with `tostr(@values)`.

## Compatibility rollout snippet

For a reviewed `$string_utils:from_list` whose body matches the reference below,
this wrapper delegates only list inputs. Other inputs retain the original body,
including `from_list("ab", "-") => "a-b"` and `from_list("ab") => E_TYPE`.
The [standalone verb body](implode_from_list_compat.moo) is also exercised by the
differential tests. Install manually only after reviewing the actual destination verb. No callers or
live database verbs are changed by this engine patch.

```moo
if (args && typeof(args[1]) == LIST)
  return implode(@args);
endif
{thelist, ?separator = ""} = args;
if (separator == "")
  return tostr(@thelist);
elseif (thelist)
  result = tostr(thelist[1]);
  for elt in (thelist[2..$])
    result = tostr(result, separator, elt);
  endfor
  return result;
else
  return "";
endif
```

Only callers with a known list should switch directly to `implode()`.

## Tests and benchmark

```bash
python3 test/implode_integration.py /absolute/path/to/moo test/Test.db
python3 test/implode_integration.py /absolute/path/to/moo /absolute/path/to/toastcore.db
python3 test/benchmark_implode.py /absolute/path/to/moo /absolute/path/to/toastcore.db /tmp/implode-results.json
python3 test/benchmark_implode.py /absolute/path/to/moo /absolute/path/to/toastcore.db /tmp/implode-10000.json --elements 10000
python3 test/benchmark_implode.py /absolute/path/to/moo /absolute/path/to/toastcore.db /tmp/implode-matrix.json --elements 1 10 100 1000 --widths 1 10 100
```

The Python scripts need only the standard library and a compatible database with
`$server_options` and `$waif`. They create a disposable output database and private
loopback handler, disable outbound connections, and leave the input unchanged.
The integration script compares against the exact reference verb, checks list-only
argument validation, and exercises both catchable and task-aborting string limits.

The benchmark waits 60 seconds after startup, uses the same Release binary for
both paths, pins the MOO to one allowed CPU, and alternates measurement order.
By default it compares five- and 1,000-element lists with comma-space and empty
separators; `--elements` selects other list sizes. `--widths` selects string
element lengths, filled with repeated `x` characters; without it the original
six-character `Ab~00 ` elements are used.
Each path is calibrated separately to approximately 0.15 CPU seconds per sample,
with three warmups and 16 measured samples. The reported CPU time per call includes
the common driver loop and call overhead. Tick counts are measured separately
using `ticks_left()`; they are not CPU timings. Foreground quotas and the string
limit are raised only in the disposable benchmark database. Input and executable
SHA256 hashes and all samples are retained in the JSON output.

Measured CPU/tick results and validation are recorded in [the benchmark report](implode_benchmark.md).
