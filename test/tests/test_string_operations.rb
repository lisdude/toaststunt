require 'test_helper'

class TestStringOperations < Test::Unit::TestCase

  def test_that_index_finds_position_of_substring_in_a_string
    run_test_as('programmer') do
      assert_equal 0, index('foobar', 'x')
      assert_equal 2, index('foobar', 'o')
    end
  end

  def test_that_index_finds_position_of_substring_in_a_string_with_case_matters
    run_test_as('programmer') do
      assert_equal 0, index('foobar', 'O', 1)
      assert_equal 2, index('foobar', 'O', 0)
    end
  end

  def test_that_index_finds_position_of_substring_in_a_string_with_offset
    run_test_as('programmer') do
      assert_equal 0, index('foobar', 'o', 0, 3)
      assert_equal 1, index('foobar', 'o', 0, 1)
      assert_equal 2, index('foobar', 'o', 0, 0)
    end
  end

  def test_that_index_offset_cannot_be_negative
    run_test_as('programmer') do
      assert_equal E_INVARG, index('foobar', 'o', 0, -1)
    end
  end

  def test_that_index_offset_can_be_larger_than_the_source_string_length
    run_test_as('programmer') do
      assert_equal 0, index('foobar', 'o', 0, 10)
    end
  end

  def test_that_rindex_finds_position_of_substring_in_a_string
    run_test_as('programmer') do
      assert_equal 0, rindex('foobar', 'x')
      assert_equal 3, rindex('foobar', 'o')
    end
  end

  def test_that_rindex_finds_position_of_substring_in_a_string_with_case_matters
    run_test_as('programmer') do
      assert_equal 0, rindex('foobar', 'O', 1)
      assert_equal 3, rindex('foobar', 'O', 0)
    end
  end

  def test_that_rindex_finds_position_of_substring_in_a_string_with_offset
    run_test_as('programmer') do
      assert_equal 2, rindex('foobar', 'o', 0, -4)
      assert_equal 3, rindex('foobar', 'o', 0, -3)
      assert_equal 3, rindex('foobar', 'o', 0, 0)
    end
  end

  def test_that_rindex_offset_cannot_be_positive
    run_test_as('programmer') do
      assert_equal E_INVARG, rindex('foobar', 'o', 0, 1)
    end
  end

  def test_that_rindex_offset_can_be_larger_than_the_source_string_length
    run_test_as('programmer') do
      assert_equal 0, rindex('foobar', 'o', 0, -10)
    end
  end

  def test_that_strtr_replaces_characters
    run_test_as('programmer') do
      assert_equal 'fbboar', strtr('foobar', 'ob', 'bo')
      assert_equal 'fiibar', strtr('foobar', 'o', 'i')
      assert_equal 'foobar', strtr('foobar', '', '')
    end
  end

  def test_that_in_strtr_from_and_to_can_be_different_lengths
    run_test_as('programmer') do
      assert_equal 'fbbbar', strtr('foobar', 'o', 'bo')
      assert_equal 'fbbar', strtr('foobar', 'ob', 'b')
    end
  end

  def test_that_in_strtr_source_can_be_empty
    run_test_as('programmer') do
      assert_equal '', strtr('', 'x', 'y')
    end
  end

  def test_that_in_strtr_from_and_to_can_match_nothing
    run_test_as('programmer') do
      assert_equal 'foobar', strtr('foobar', 'x', 'y')
    end
  end

  def test_that_strtr_works_with_numbers
    run_test_as('programmer') do
      assert_equal '02a4B', strtr('12345', '135', '0aB')
      assert_equal '02a4B', strtr('12345', '135', '0aB', 1)
    end
  end

  def test_that_strtr_works_with_symbols
    run_test_as('programmer') do
      assert_equal '0@a$B', strtr('!@#$%', '!#%', '0aB')
      assert_equal '0@a$B', strtr('!@#$%', '!#%', '0aB', 1)
    end
  end

  def test_that_strtr_in_case_insensitive_mode_it_preserves_case
    run_test_as('programmer') do
      assert_equal 'FxXbar', strtr('FoObar', 'o', 'x', 0)
      assert_equal 'FxXbar', strtr('FoObar', 'O', 'X', 0)
    end
  end

  def test_that_strtr_in_case_sensitive_mode_it_ignores_case
    run_test_as('programmer') do
      assert_equal 'FxObar', strtr('FoObar', 'o', 'x', 1)
      assert_equal 'FoXbar', strtr('FoObar', 'O', 'X', 1)
    end
  end

  def test_a_few_more_interesting_strtr_cases
    run_test_as('programmer') do
      assert_equal 'BbB', strtr('5xX', '135x', '0aBB', 0)
      assert_equal 'BBX', strtr('5xX', '135x', '0aBB', 1)
      assert_equal '4444', strtr('xXxX', 'xXxX', '1234', 0)
      assert_equal '3434', strtr('xXxX', 'xXxX', '1234', 1)
      assert_equal '11', strtr('xX', 'x', '1', 0)
      assert_equal '1X', strtr('xX', 'x', '1', 1)
      assert_equal 'X', strtr('1', '1', 'X', 0)
      assert_equal 'X', strtr('1', '1', 'X', 1)
    end
  end

  def test_that_a_variety_of_fuzzy_inputs_do_not_break_strtr
    run_test_as('wizard') do
      with_mutating_binary_string("012345678901234567890123456789") do |g|
        100.times do
          s = g.next
          server_log s
          v = strtr(s[0..9], s[10..19], s[20..29])
          assert v.class == String
        end
      end
    end
  end

end
