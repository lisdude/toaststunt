require 'test/unit'

require 'moo_support'
require 'fuzz'

if defined?(Test::Unit::TestCase)

  class Test::Unit::TestCase
    include MooSupport
    include Fuzz
  end

end
