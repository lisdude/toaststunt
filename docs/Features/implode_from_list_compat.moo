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
