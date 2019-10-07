require 'test_helper'

class TestVerbCache < Test::Unit::TestCase

  def test_that_renumbering_an_object_does_not_affect_the_verb_cache
    run_test_as('wizard') do
      a = simplify(command(%Q|; a = create($nothing); return renumber(a); |))
      b = simplify(command(%Q|; b = create($nothing); return renumber(b); |))
      add_verb(b, [player, 'xd', 'test'], ['this', 'none', 'this'])
      set_verb_code(b, 'test', ['return "test";'])

      recycle(a) # should clear the cache
      x = verb_cache_stats()
      assert_equal E_INVIND, call(a, 'test')
      assert_equal 'test', call(b, 'test')

      renumber(b) # should not clear the cache
      y = verb_cache_stats()
      assert_equal 'test', call(a, 'test')
      assert_equal E_INVIND, call(b, 'test')

      z = verb_cache_stats()

      subtract = lambda { |el| el[0] - el[1] }
      assert_equal [1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], x[4].zip(y[4]).map(&subtract)
      assert_equal [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], y[4].zip(z[4]).map(&subtract)
    end
  end

end
