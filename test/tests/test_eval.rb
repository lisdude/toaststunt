require 'test_helper'

class TestEval < Test::Unit::TestCase

  def test_that_eval_cannot_be_called_by_non_programmers
    run_test_as('wizard') do
      set(player, 'programmer', 0)
      assert_equal E_PERM, simplify(command(%Q|; return eval("return 5;");|))
    end
  end

  def test_that_eval_requires_at_least_one_argument
    run_test_as('wizard') do
      assert_equal E_ARGS, simplify(command(%Q|; return eval();|))
    end
  end

  def test_that_eval_requires_string_arguments
    run_test_as('wizard') do
      assert_equal E_TYPE, simplify(command(%Q|; return eval(1);|))
      assert_equal E_TYPE, simplify(command(%Q|; return eval(1, 2);|))
      assert_equal E_TYPE, simplify(command(%Q|; return eval({});|))
      assert_equal E_TYPE, simplify(command(%Q|; return eval([]);|))
    end
  end

  def test_that_eval_evaluates_multiple_strings
    run_test_as('wizard') do
      assert_equal [1, 15], simplify(command(%Q|; return eval("x = 0;", "for i in [1..5]", "x = x + i;", "endfor", "return x;");|))
    end
  end

  def test_that_eval_evaluates_a_single_string
    run_test_as('wizard') do
      assert_equal [1, 5], simplify(command(%Q|; return eval("return 5;");|))
    end
  end

end
