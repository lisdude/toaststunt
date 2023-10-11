require 'test_helper'

class TestCollectionImprovements < Test::Unit::TestCase

  def test_that_the_control_case_works
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'foobar'], ['this', 'none', 'this'])
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};'
        vc << 'x[1][1] = "abc";'
        vc << 'return x;'
      end
      assert_equal [['abc', [3, 4]], [[5, 6], [7, 8]]], call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> 1, 2 -> 2], 2 -> [1 -> 3, 2 -> 4]], 2 -> [1 -> [1 -> 5, 2 -> 6], 2 -> [1 -> 7, 2 -> 8]]];'
        vc << 'x[1][1] = "abc";'
        vc << 'return x;'
      end
      assert_equal({1 => {1 => 'abc', 2 => {1 => 3, 2 => 4}}, 2 => {1 => {1 => 5, 2 => 6}, 2 => {1 => 7, 2 => 8}}}, call(o, 'foobar'))
    end
  end

  def test_that_top_level_references_are_not_shared
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'foobar'], ['this', 'none', 'this'])
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};'
        vc << 'y = x;'
        vc << 'z = x;'
        vc << 'x[1] = "abc";'
        vc << 'return y;'
      end
      assert_equal [[[1, 2], [3, 4]], [[5, 6], [7, 8]]], call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> 1, 2 -> 2], 2 -> [1 -> 3, 2 -> 4]], 2 -> [1 -> [1 -> 5, 2 -> 6], 2 -> [1 -> 7, 2 -> 8]]];'
        vc << 'y = x;'
        vc << 'z = x;'
        vc << 'x[1] = "abc";'
        vc << 'return y;'
      end
      assert_equal({1 => {1 => {1 => 1, 2 => 2}, 2 => {1 => 3, 2 => 4}}, 2 => {1 => {1 => 5, 2 => 6}, 2 => {1 => 7, 2 => 8}}}, call(o, 'foobar'))
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};'
        vc << 'y = x;'
        vc << 'z = x;'
        vc << 'x[1][1] = "abc";'
        vc << 'return y;'
      end
      assert_equal [[[1, 2], [3, 4]], [[5, 6], [7, 8]]], call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> 1, 2 -> 2], 2 -> [1 -> 3, 2 -> 4]], 2 -> [1 -> [1 -> 5, 2 -> 6], 2 -> [1 -> 7, 2 -> 8]]];'
        vc << 'y = x;'
        vc << 'z = x;'
        vc << 'x[1][1] = "abc";'
        vc << 'return y;'
      end
      assert_equal({1 => {1 => {1 => 1, 2 => 2}, 2 => {1 => 3, 2 => 4}}, 2 => {1 => {1 => 5, 2 => 6}, 2 => {1 => 7, 2 => 8}}}, call(o, 'foobar'))
    end
  end

  def test_that_references_to_nested_collections_are_not_shared
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'foobar'], ['this', 'none', 'this'])
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};'
        vc << 'y = x[1];'
        vc << 'z = x[1];'
        vc << 'x[1][1][1] = "abc";'
        vc << 'return y;'
      end
      assert_equal [[1, 2], [3, 4]], call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> 1, 2 -> 2], 2 -> [1 -> 3, 2 -> 4]], 2 -> [1 -> [1 -> 5, 2 -> 6], 2 -> [1 -> 7, 2 -> 8]]];'
        vc << 'y = x[1];'
        vc << 'z = x[1];'
        vc << 'x[1][1][1] = "abc";'
        vc << 'return y;'
      end
      assert_equal({1 => {1 => 1, 2 => 2}, 2 => {1 => 3, 2 => 4}}, call(o, 'foobar'))
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}}};'
        vc << 'y = x[1];'
        vc << 'z = x[1];'
        vc << 'x[1][1][1] = "abc";'
        vc << 'return y;'
      end
      assert_equal [[[1, 2], [3, 4]], [[5, 6], [7, 8]]], call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> [1 -> 1, 2 -> 2], 2 -> [1 -> 3, 2 -> 4]], 2 -> [1 -> [1 -> 5, 2 -> 6], 2 -> [1 -> 7, 2 -> 8]]]];'
        vc << 'y = x[1];'
        vc << 'z = x[1];'
        vc << 'x[1][1][1] = "abc";'
        vc << 'return y;'
      end
      assert_equal({1 => {1 => {1 => 1, 2 => 2}, 2 => {1 => 3, 2 => 4}}, 2 => {1 => {1 => 5, 2 => 6}, 2 => {1 => 7, 2 => 8}}}, call(o, 'foobar'))
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}}};'
        vc << 'y = x[1][1];'
        vc << 'z = x[1][1];'
        vc << 'x[1][1][1][1] = "abc";'
        vc << 'return y;'
      end
      assert_equal [[1, 2], [3, 4]], call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> [1 -> 1, 2 -> 2], 2 -> [1 -> 3, 2 -> 4]], 2 -> [1 -> [1 -> 5, 2 -> 6], 2 -> [1 -> 7, 2 -> 8]]]];'
        vc << 'y = x[1][1];'
        vc << 'z = x[1][1];'
        vc << 'x[1][1][1][1] = "abc";'
        vc << 'return y;'
      end
      assert_equal({1 => {1 => 1, 2 => 2}, 2 => {1 => 3, 2 => 4}}, call(o, 'foobar'))
    end
  end

  def test_that_updates_to_references_are_not_shared
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'foobar'], ['this', 'none', 'this'])
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}}};'
        vc << 'y = x[1][2];'
        vc << 'z = x[1][2];'
        vc << 'x[1][1][1] = "abc";'
        vc << 'y[1][2] = "foobar";'
        vc << 'return x;'
      end
      # assert_equal [[['abc', [3, 4]], [[5, 6], [7, 8]]]], call(o, 'foobar') - parslet issues
      assert_equal [['abc', [3, 4]], [[5, 6], [7, 8]]], call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> [1 -> 1, 2 -> 2], 2 -> [1 -> 3, 2 -> 4]], 2 -> [1 -> [1 -> 5, 2 -> 6], 2 -> [1 -> 7, 2 -> 8]]]];'
        vc << 'y = x[1][2];'
        vc << 'z = x[1][2];'
        vc << 'x[1][1][1] = "abc";'
        vc << 'y[1][2] = "foobar";'
        vc << 'return x;'
      end
      assert_equal({1 => {1 => {1 => 'abc', 2 => {1 => 3, 2 => 4}}, 2 => {1 => {1 => 5, 2 => 6}, 2 => {1 => 7, 2 => 8}}}}, call(o, 'foobar'))
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}}};'
        vc << 'y = x[1][1][2];'
        vc << 'z = x[1][1][2];'
        vc << 'x[1][1][1][1] = "abc";'
        vc << 'y[2] = "foobar";'
        vc << 'return x;'
      end
      # assert_equal [[[['abc', 2], [3, 4]], [[5, 6], [7, 8]]]], call(o, 'foobar') - parslet issues
      assert_equal [[['abc', 2], [3, 4]], [[5, 6], [7, 8]]], call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> [1 -> 1, 2 -> 2], 2 -> [1 -> 3, 2 -> 4]], 2 -> [1 -> [1 -> 5, 2 -> 6], 2 -> [1 -> 7, 2 -> 8]]]];'
        vc << 'y = x[1][1][2];'
        vc << 'z = x[1][1][2];'
        vc << 'x[1][1][1][1] = "abc";'
        vc << 'y[2] = "foobar";'
        vc << 'return x;'
      end
      assert_equal({1 => {1 => {1 => {1 => 'abc', 2 => 2}, 2 => {1 => 3, 2 => 4}}, 2 => {1 => {1 => 5, 2 => 6}, 2 => {1 => 7, 2 => 8}}}}, call(o, 'foobar'))
    end
  end

  def test_that_multiple_writes_work
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'foobar'], ['this', 'none', 'this'])
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = {{{{{1}}}}};'
        vc << 'y = x;'
        vc << 'z = x;'
        vc << 'for i in [1..10000]'
        vc << 'ticks_left() < 2000 || seconds_left() < 2 && suspend(0);'
        vc << 'x[1][1][1][1] = i;'
        vc << 'endfor'
        vc << 'return y;'
      end
      # assert_equal [[[[[1]]]]], call(o, 'foobar') - parslet issues
      assert_equal 1, call(o, 'foobar')
      set_verb_code(o, 'foobar') do |vc|
        vc << 'x = [1 -> [1 -> [1 -> [1 -> [1 -> 1]]]]];'
        vc << 'y = x;'
        vc << 'z = x;'
        vc << 'for i in [1..10000]'
        vc << 'ticks_left() < 2000 || seconds_left() < 2 && suspend(0);'
        vc << 'x[1][1][1][1] = i;'
        vc << 'endfor'
        vc << 'return y;'
      end
      assert_equal({1 => {1 => {1 => {1 => {1 => 1}}}}}, call(o, 'foobar'))
    end
  end

end
