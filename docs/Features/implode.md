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

The engine appends converted elements and separators into one growable stream,
avoiding the repeated copying of the reference MOO loop.

## Compatibility rollout snippet

For a reviewed `$string_utils:from_list` whose body matches the reference below,
this wrapper delegates only list inputs. Other inputs retain the original body,
including `from_list("ab", "-") => "a-b"` and `from_list("ab") => E_TYPE`.

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
