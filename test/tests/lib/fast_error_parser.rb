require 'parslet'

require 'moo_err'

class FastErrorParser < Parslet::Parser

  rule(:error) {
    str('{2, {') >> (str('E_') >> match('[A-Z]').repeat(1)).as(:err) >> any.repeat
  }

  root :error

end

class FastErrorTransform < Parslet::Transform

  rule(:err => simple(:err)) { MooErr.new(err) }

end
