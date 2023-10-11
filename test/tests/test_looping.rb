require 'test_helper'

class TestLooping < Test::Unit::TestCase

  def test_that_for_loops_work_with_strings
    run_test_as('programmer') do
      assert_equal ['1', '2', '3', '4', '5'], eval(%|x = {}; for i in ("12345"); x = {@x, i}; endfor; return x;|)
      assert_equal [['1', 1], ['2', 2], ['3', 3], ['4', 4], ['5', 5]], eval(%|x = {}; for i, j in ("12345"); x = {@x, {i, j}}; endfor; return x;|)
    end
  end

  def test_that_for_loops_work_with_maps
    run_test_as('programmer') do
      assert_equal [1, 3.0, 'two'], eval(%|x = {}; for v in (["one" -> 1, "two" -> "two", "three" -> 3.0]); x = {@x, v}; endfor; return x;|)
      assert_equal [[1, 'one'], [3.0, 'three'], ['two', 'two']], eval(%|x = {}; for v, k in (["one" -> 1, "two" -> "two", "three" -> 3.0]); x = {@x, {v, k}}; endfor; return x;|)
    end
  end

  def test_that_for_loops_support_two_variables
    run_test_as('programmer') do
      assert_equal [['1', 1], ['2', 2], ['3', 3], ['4', 4], ['5', 5]], eval(%|x = {}; for i, j in ({"1", "2", "3", "4", "5"}); x = {@x, {i, j}}; endfor; return x;|)
    end
  end

  def test_that_valid_for_loops_compile
    run_test_as('programmer') do
      assert_equal [1, 2], eval(%|x = {}; for i in ({1, 2, 3, 4, 5}); if (i > 2); break i; endif; x = {@x, i}; endfor; return x;|)
      assert_equal [3, 4, 5], eval(%|x = {}; for i in ({1, 2, 3, 4, 5}); if (i < 3); continue i; endif; x = {@x, i}; endfor; return x;|)
      assert_equal ['1', '2'], eval(%|x = {}; for i, j in ({"1", "2", "3", "4", "5"}); if (j > 2); break i; endif; x = {@x, i}; endfor; return x;|)
      assert_equal ['3', '4', '5'], eval(%|x = {}; for i, j in ({"1", "2", "3", "4", "5"}); if (j < 3); continue i; endif; x = {@x, i}; endfor; return x;|)
      assert_equal ['1', '2'], eval(%|x = {}; for i, j in ({"1", "2", "3", "4", "5"}); if (j > 2); break j; endif; x = {@x, i}; endfor; return x;|)
      assert_equal ['3', '4', '5'], eval(%|x = {}; for i, j in ({"1", "2", "3", "4", "5"}); if (j < 3); continue j; endif; x = {@x, i}; endfor; return x;|)
      # the following is legal, but probably not what you want...
      assert_equal [1, 2], eval(%|x = {}; for i, i in ({"1", "2", "3", "4", "5"}); if (i > 2); break i; endif; x = {@x, i}; endfor; return x;|)
      assert_equal [3, 4, 5], eval(%|x = {}; for i, i in ({"1", "2", "3", "4", "5"}); if (i < 3); continue i; endif; x = {@x, i}; endfor; return x;|)
    end
  end

  def test_that_invalid_for_loops_do_not_compile
    run_test_as('programmer') do
      r = eval(%|x = {}; for in ({"1", "2", "3", "4", "5"}); endfor; return x;|)
      assert r =~ /syntax error/
      r = eval(%|x = {}; for i, j, k in ({"1", "2", "3", "4", "5"}); endfor; return x;|)
      assert r =~ /syntax error/
      r = eval(%|x = {}; for i in ({"1", "2", "3", "4", "5"}); continue x; endfor; return x;|)
      assert r =~ /Invalid loop name/
      r = eval(%|x = {}; for i, j in ({"1", "2", "3", "4", "5"}); continue x; endfor; return x;|)
      assert r =~ /Invalid loop name/
    end
  end

  def test_that_for_loops_cannot_loop_over_non_sequences
    run_test_as('programmer') do
      assert_equal E_TYPE, eval(%|for x in (5); endfor|)
      assert_equal E_TYPE, eval(%|for x in (5.0); endfor|)
      assert_equal E_TYPE, eval(%|for x in (#5); endfor|)
      assert_equal E_TYPE, eval(%|for x in (E_PERM); endfor|)
    end
  end

  def test_that_for_loops_decompile
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'test'], ['this', 'none', 'this'])
      set_verb_code(o, 'test') do |vc|
        vc << 'for i, i in ({}); break i; endfor;'
      end
      vc = simplify command %|; return verb_code(#{o}, "test");|
      assert_equal ['for i, i in ({})', '  break i;', 'endfor'], vc

      set_verb_code(o, 'test') do |vc|
        vc << 'for i, j in ({}); break i; endfor;'
      end
      vc = simplify command %|; return verb_code(#{o}, "test");|
      assert_equal ['for i, j in ({})', '  break i;', 'endfor'], vc

      set_verb_code(o, 'test') do |vc|
        vc << 'for i, j in ({}); break j; endfor;'
      end
      vc = simplify command %|; return verb_code(#{o}, "test");|
      assert_equal ['for i, j in ({})', '  break j;', 'endfor'], vc

      set_verb_code(o, 'test') do |vc|
        vc << 'for i, j in ({}); break j; continue i; endfor;'
      end
      vc = simplify command %|; return verb_code(#{o}, "test");|
      assert_equal ['for i, j in ({})', '  break j;', '  continue i;', 'endfor'], vc

      # strings
      set_verb_code(o, 'test') do |vc|
        vc << 'for i, j in ("foobar"); break j; continue i; endfor;'
      end
      vc = simplify command %|; return verb_code(#{o}, "test");|
      assert_equal ['for i, j in ("foobar")', '  break j;', '  continue i;', 'endfor'], vc

      # maps
      set_verb_code(o, 'test') do |vc|
        vc << 'for v, k in (["one" -> 1, "two" -> "two", "three" -> 3.0]); break k; continue v; endfor;'
      end
      vc = simplify command %|; return verb_code(#{o}, "test");|
      assert_equal ['for v, k in (["one" -> 1, "two" -> "two", "three" -> 3.0])', '  break k;', '  continue v;', 'endfor'], vc
    end
  end

  def test_that_for_loop_edge_cases_do_not_leak_memory
    run_test_as('programmer') do
      assert_equal 0, eval(%|for i in ([]); endfor; return 0;|)
      assert_equal 0, eval(%|for v, k in ([]); endfor; return 0;|)
    end
  end

end
