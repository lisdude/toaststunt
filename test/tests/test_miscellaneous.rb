require 'test_helper'

class TestMiscellaneous < Test::Unit::TestCase

  def test_that_isa_returns_true_or_false_given_an_object_number
    run_test_as('programmer') do
      a = create(:nothing)
      b = create(a)

      assert_equal 0, isa(a, b)
      assert_equal 1, isa(b, a)

      assert_equal 0, isa(b, :nothing)
      assert_equal 0, isa(:nothing, a)
      assert_equal 0, isa(:nothing, :nothing)

      assert_equal 0, simplify(command(%Q|; return isa(#100000, #1); |))
      assert_equal 0, simplify(command(%Q|; return isa(#1, #100000); |))
      assert_equal 0, simplify(command(%Q|; return isa(#1000000, #1000000); |))
      assert_equal 0, simplify(command(%Q|; return isa(#-2, #2); |))
      assert_equal 0, simplify(command(%Q|; return isa(#1, #-1); |))
    end
  end

  def test_that_isa_returns_true_or_false_given_an_object
    run_test_as('programmer') do
      o = create(:nothing)
      add_property(o, 'b', 0, ['player', ''])
      add_property(o, 'c', 0, ['player', ''])

      [0, 1].each do |opt|
        simplify(command(%Q|; #{o}.b = create($nothing); |))
        simplify(command(%Q|; #{o}.c = create(#{o}.b, #{opt}); |))
        assert_equal 0, simplify(command(%Q|; return isa(#{o}.b, #{o}.c); |))
        assert_equal 1, simplify(command(%Q|; return isa(#{o}.c, #{o}.b); |))
      end
    end
  end

end
