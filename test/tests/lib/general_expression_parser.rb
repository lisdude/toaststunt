require 'parslet'

require 'moo_obj'
require 'moo_err'

class GeneralExpressionParser < Parslet::Parser

  rule(:space)      { match('\s').repeat(1) }
  rule(:space?)     { space.maybe }

  rule(:digit)      { match('[0-9]') }
  rule(:digits)     { digit.repeat(1) }
  rule(:digits?)    { digit.repeat(0) }

  rule(:sign?)      { match('[-+]').maybe }

  rule(:exp?)       { (match('[eE]') >> match('[-+]') >> digits).maybe }

  rule(:float)      { ((sign? >> digits? >> str('.') >> digits >> exp?) | (sign? >> digits >> str('.') >> digits? >> exp?)).as(:float) >> space? }

  rule(:integer)    { (sign? >> digits).as(:integer) >> space? }

  rule(:number)     { float | integer }

  rule(:obj)        { (str('#') >> sign? >> digits).as(:obj) >> space? }

  rule(:err)        { (str('E_') >> match('[A-Z]').repeat(1)).as(:err) >> space? }

  rule(:string)     { (str('"') >> (str('\\') >> any | str('"').absent? >> any).repeat >> str('"')).as(:string) >> space? }

  rule(:pair)       { expression.as(:key) >> str('->') >> space? >> expression.as(:value) }

  rule(:map)        {
    (str('[') >> space? >> str(']')).as(:empty_map) >> space? |
    (str('[') >> space? >> ((pair >> str(',') >> space?).repeat >> pair).maybe >> str(']')).as(:map) >> space?
  }

  rule(:list)       {
    (str('{') >> space? >> str('}')).as(:empty_list) >> space? |
    (str('{') >> space? >> ((expression >> str(',') >> space?).repeat >> expression).maybe >> str('}')).as(:list) >> space?
  }

  rule(:expression) { map | list | string | obj | number | err }

  root :expression

end

class GeneralExpressionTransform < Parslet::Transform

  rule(:string => simple(:string))     { eval(string) }

  rule(:integer => simple(:integer))   { integer.to_i }

  rule(:float => simple(:float))       { float.to_f }

  rule(:obj => simple(:obj))           { MooObj.new(obj.to_s) }

  rule(:err => simple(:err))           { MooErr.new(err.to_s) }

  rule(:empty_list => simple(:dummy))  { [] }
  rule(:list => simple(:value))        { [value] }
  rule(:list => subtree(:list))        { list }

  rule(:empty_map => simple(:dummy))   { {} }
  rule(:map => {:key => simple(:key), :value => simple(:value)}) { {key => value} }
  rule(:map => subtree(:map))          { map.instance_of?(Array) ? map.inject({}) { |a, i| a[i[:key]] = i[:value] ; a } : {map[:key] => map[:value]}}

end
