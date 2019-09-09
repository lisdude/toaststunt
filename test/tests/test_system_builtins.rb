require 'test_helper'

class TestSystemBuiltins < Test::Unit::TestCase

  def test_that_getenv_requires_wizperms
    run_test_as('wizard') do
      assert_not_equal E_PERM, getenv('PATH')
    end
    run_test_as('programmer') do
      assert_equal E_PERM, getenv('PATH')
    end
  end

  def test_that_getenv_fails_on_invalid_arguments
    run_test_as('wizard') do
      assert_equal E_ARGS, simplify(command(%Q|; getenv(); |))
      assert_equal E_TYPE, getenv(1)
      assert_equal E_TYPE, getenv(1.1)
    end
  end

  def test_that_getenv_returns_the_environment_variable_value
    run_test_as('wizard') do
      assert_equal ENV['PATH'], getenv('PATH')
      assert_equal ENV['USER'], getenv('USER')
    end
  end

  def test_that_getenv_returns_zero_if_the_environment_variable_does_not_exist
    run_test_as('wizard') do
      assert_equal 0, getenv('XYZZY')
    end
  end

end
