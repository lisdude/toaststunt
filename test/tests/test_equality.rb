require 'test_helper'

class TestEquality < Test::Unit::TestCase

  def test_that_tests_for_equality_work
    run_test_as('programmer') do
      assert_equal 1, eval(%|return 10 == 5 + 5;|)
      assert_equal 1, eval(%|return 10.0 == 5.0 + 5.0;|)
      assert_equal 1, eval(%|return "10" == "1" + "0";|)
      assert_equal 1, eval(%|return #10 == #10;|)
      assert_equal 1, eval(%|return E_NONE == E_NONE;|)
      assert_equal 1, eval(%|x = {1, 2.0}; y = {#3, "four"}; return {1, 2.0, #3, "four"} == {@x, @y};|)
      assert_equal 1, eval(%|x = []; x[1] = 2.0; x[#3] = "four"; return [1 -> 2.0, #3 -> "four"] == x;|)

      assert_equal 0, eval(%|return 1 == 1.0;|)
      assert_equal 0, eval(%|return 1.0 == #1;|)
      assert_equal 0, eval(%|return #1 == "1";|)
      assert_equal 0, eval(%|return [] == {};|)
    end
  end

end
