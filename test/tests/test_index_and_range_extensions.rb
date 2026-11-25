require 'test_helper'

class TestIndexAndRangeExtensions < Test::Unit::TestCase

  def test_that_caret_works_as_an_index
    run_test_as('programmer') do
      assert_equal 1, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][^];|)
      assert_equal 2, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][^ + 1];|)
      assert_equal E_RANGE, eval(%|return [][^];|)

      assert_equal({'a' => 'foobar', 'b' => 2, 'c' => 3}, eval(%|t = ["a" -> 1, "b" -> 2, "c" -> 3]; t[^] = "foobar"; return t;|))
      assert_equal({'a' => 1, 'a*' => 'foobar', 'b' => 2, 'c' => 3}, eval(%|t = ["a" -> 1, "b" -> 2, "c" -> 3]; t[^ + "*"] = "foobar"; return t;|))

      assert_equal 1, eval(%|return {1, 2, 3, 4, 5, 6, 7}[^];|)
      assert_equal 2, eval(%|return {1, 2, 3, 4, 5, 6, 7}[^ + 1];|)
      assert_equal E_RANGE, eval(%|return {}[^];|)

      assert_equal ['foobar', 2, 3, 4, 5, 6, 7], eval(%|t = {1, 2, 3, 4, 5, 6, 7}; t[^] = "foobar"; return t;|)
      assert_equal [1, 'foobar', 3, 4, 5, 6, 7], eval(%|t = {1, 2, 3, 4, 5, 6, 7}; t[^ + 1] = "foobar"; return t;|)

      assert_equal 'f', eval(%|return "foobar"[^];|)
      assert_equal 'o', eval(%|return "foobar"[^ + 1];|)
      assert_equal E_RANGE, eval(%|return ""[^];|)

      assert_equal 'xoobar', eval(%|t = "foobar"; t[^] = "x"; return t;|)
      assert_equal 'fxobar', eval(%|t = "foobar"; t[^ + 1] = "x"; return t;|)
    end
  end

  def test_that_dollar_works_as_an_index
    run_test_as('programmer') do
      assert_equal 7, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][$];|)
      assert_equal 6, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][$ - 1];|)
      assert_equal E_RANGE, eval(%|return [][$];|)

      assert_equal({'a' => 1, 'b' => 2, 'c' => 'foobar'}, eval(%|t = ["a" -> 1, "b" -> 2, "c" -> 3]; t[$] = "foobar"; return t;|))
      assert_equal({'a' => 1, 'b' => 2, 'c' => 3, 'c*' => 'foobar'}, eval(%|t = ["a" -> 1, "b" -> 2, "c" -> 3]; t[$ + "*"] = "foobar"; return t;|))

      assert_equal 7, eval(%|return {1, 2, 3, 4, 5, 6, 7}[$];|)
      assert_equal 6, eval(%|return {1, 2, 3, 4, 5, 6, 7}[$ - 1];|)
      assert_equal E_RANGE, eval(%|return {}[$];|)

      assert_equal [1, 2, 3, 4, 5, 6, 'foobar'], eval(%|t = {1, 2, 3, 4, 5, 6, 7}; t[$] = "foobar"; return t;|)
      assert_equal [1, 2, 3, 4, 5, 'foobar', 7], eval(%|t = {1, 2, 3, 4, 5, 6, 7}; t[$ - 1] = "foobar"; return t;|)

      assert_equal 'r', eval(%|return "foobar"[$];|)
      assert_equal 'a', eval(%|return "foobar"[$ - 1];|)
      assert_equal E_RANGE, eval(%|return ""[$];|)

      assert_equal 'foobax', eval(%|t = "foobar"; t[$] = "x"; return t;|)
      assert_equal 'foobxr', eval(%|t = "foobar"; t[$ - 1] = "x"; return t;|)
    end
  end

  def test_that_ranges_work
    run_test_as('programmer') do
      assert_equal({1 => 1, 2 => 2, 3 => 3, 4 => 4, 5 => 5, 6 => 6, 7 => 7}, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][^..$];|))
      assert_equal({2 => 2, 3 => 3, 4 => 4, 5 => 5, 6 => 6}, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][^ + 1 .. $ - 1];|))
      assert_equal({}, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][$..^];|))

      assert_equal({1 => 1, 2 => 'two', 3 => 'three', 4 => 'four', 5 => 5, 6 => 6, 7 => 7}, eval(%|t = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; t[2..4] = [2 -> "two", 3 -> "three", 4 -> "four"]; return t;|))

      assert_equal({1 => 1, 2 => 'two', 3 => 'three', 5 => 5, 6 => 6, 7 => 7}, eval(%|t = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; t[2..4] = [2 -> "two", 3 -> "three"]; return t;|))
      assert_equal({1 => 1, 3 => 'three', 4 => 'four', 5 => 5, 6 => 6, 7 => 7}, eval(%|t = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; t[2..4] = [3 -> "three", 4 -> "four"]; return t;|))

      assert_equal({1 => 'one', 2 => 'two', 3 => 'three', 4 => 'four', 5 => 5, 6 => 6, 7 => 7}, eval(%|t = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; t[2..4] = [1 -> "one", 2 -> "two", 3 -> "three", 4 -> "four"]; return t;|))
      assert_equal({1 => 1, 2 => 'two', 3 => 'three', 4 => 'four', 5 => 5, 6 => 6, 7 => 7}, eval(%|t = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; t[2..4] = [2 -> "two", 3 -> "three", 4 -> "four", 5 -> "five"]; return t;|))

      assert_equal({1 => 'one', 2 => 'two', 3 => 'three', 4 => 'four', 5 => 5, 6 => 6, 7 => 7}, eval(%|t = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; t[2..4] = [1 -> "one", 2 -> "two", 3 -> "three", 4 -> "four", 5 -> "five"]; return t;|))

      assert_equal({'abc' => 'abc', 'one' => 'foo', 'two' => 'bar', 'xyz' => 'xyz'}, eval(%|t = ["abc" -> "abc", "one" -> "one"]; t["one".."one"] = ["one" -> "foo", "two" -> "bar", "xyz" -> "xyz"]; return t;|))

      assert_equal({1 => 1, 2 => 2, 3 => 3, 4 => 4, 5 => 5, 6 => 6, 7 => 7, 'a' => 'a', 'b' => 'b', 'c' => 'c'}, eval(%|t = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; t[7..1] = ["a" -> "a", "b" -> "b", "c" -> "c"]; return t;|))
      assert_equal({1 => 1, 2 => 2, 3 => 3, 4 => 4, 5 => 5, 6 => 6, 7 => 7, 'a' => 'a', 'b' => 'b', 'c' => 'c'}, eval(%|t = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; t[$..^] = ["a" -> "a", "b" -> "b", "c" -> "c"]; return t;|))

      assert_equal ['two', 'three'], eval(%|return {"one", "two", "three"}[$ - 1..$];|)
      assert_equal 'three', eval(%|return {"one", "two", "three"}[3..3];|)
      assert_equal [], eval(%|return {"one", "two", "three"}[17..12];|)
      assert_equal [1, 2, 3, 4, 5, 6, 7], eval(%|return {1, 2, 3, 4, 5, 6, 7}[^..$];|)
      assert_equal [2, 3, 4, 5, 6], eval(%|return {1, 2, 3, 4, 5, 6, 7}[^ + 1 .. $ - 1];|)
      assert_equal [], eval(%|return {1, 2, 3, 4, 5, 6, 7}[$..^];|)

      assert_equal [1, 6, 7, 8, 9], eval(%|l = {1, 2, 3}; l[2..3] = {6, 7, 8, 9}; return l;|)
      assert_equal [1, 10, 'foo', 6, 7, 8, 9], eval(%|l = {1, 6, 7, 8, 9}; l[2..1] = {10, "foo"}; return l;|)
      assert_equal [1, 10, 'fu', 6, 7, 8, 9], eval(%|l = {1, 10, "foo", 6, 7, 8, 9}; l[3][2..$] = "u"; return l;|)

      assert_equal [1, 2, 3, 4, 5, 6, 'a', 'b', 'c', 2, 3, 4, 5, 6, 7], eval(%|t = {1, 2, 3, 4, 5, 6, 7}; t[7..1] = {"a", "b", "c"}; return t;|)
      assert_equal [1, 2, 3, 4, 5, 6, 'a', 'b', 'c', 2, 3, 4, 5, 6, 7], eval(%|t = {1, 2, 3, 4, 5, 6, 7}; t[$..^] = {"a", "b", "c"}; return t;|)

      assert_equal 'oobar', eval(%|return "foobar"[2..$];|)
      assert_equal 'o', eval(%|return "foobar"[3..3];|)
      assert_equal '', eval(%|return "foobar"[17..12];|)
      assert_equal 'foobar', eval(%|return "foobar"[^..$];|)
      assert_equal 'ooba', eval(%|return "foobar"[^ + 1 .. $ - 1];|)
      assert_equal '', eval(%|return "foobar"[$..^];|)

      assert_equal 'foobarbaz', eval(%|s = "foobar"; s[7..12] = "baz"; return s;|)
      assert_equal 'fubarbaz', eval(%|s = "foobarbaz"; s[1..3] = "fu"; return s;|)
      assert_equal 'testfubarbaz', eval(%|s = "fubarbaz"; s[1..0] = "test"; return s;|)

      assert_equal '123456abc234567', eval(%|s = "1234567"; s[7..1] = "abc"; return s;|)
      assert_equal '123456abc234567', eval(%|s = "1234567"; s[$..^] = "abc"; return s;|)
    end
  end

  def test_that_decompile_works
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'foobar'], ['this', 'none', 'this'])
      set_verb_code(o, 'foobar') do |vc|
        vc << 'return "foobar"[^ + 2 ^ 2 .. $ - #0.off];'
      end
      vc = simplify command %|; return verb_code(#{o}, "foobar");|
      assert_equal 'return "foobar"[^ + 2 ^ 2..$ - $off];', vc
    end
  end

  def test_indexed_assignment_preserves_writes_from_either_expression
    run_test_as('programmer') do
      assert_equal [3, 1], eval('m = ["a" -> 0, "b" -> 0]; m["a"] = (m["b"] = 1) + 2; return {m["a"], m["b"]};')
      assert_equal [2, 1], eval('m = ["a" -> 0, "b" -> 0]; m["a"] = (m["b"] = 1) ? 2 | 9; return {m["a"], m["b"]};')
      assert_equal [2, 1], eval('m = ["a" -> 0, "b" -> 0]; m[(m["b"] = 1) ? "a" | "b"] = 2; return {m["a"], m["b"]};')
      assert_equal [1, 0, 0, 1], eval('m = ["a" -> 0, "b" -> 0]; n = m; m["a"] = n["b"] = 1; return {m["a"], m["b"], n["a"], n["b"]};')
    end
  end

  def test_indexed_local_assignment_skips_refresh_for_unrelated_writes
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'assign'], ['this', 'none', 'this'])
      set_verb_code(o, 'assign', [
        'l = {0, 0}; i = 0; n = 2;',
        'l[i = i + 1] = (n = n + 1);',
        'l[2] = length({n} = {8});',
        'return {l, i, n};'
      ])
      assert_equal [[3, 1], 1, 8], call(o, 'assign')
      assert_equal [], disassemble(o, 'assign').grep(/REFRESH_LVALUE/)
    end
  end

  def test_indexed_local_assignment_refreshes_a_scatter_target
    run_test_as('programmer') do
      assert_equal [1, 5], eval('l = {0, 0}; l[1] = length({l} = {{4, 5}}); return l;')
    end
  end

  def test_indexed_and_ranged_assignment_preserve_writes_for_all_container_types
    run_test_as('programmer') do
      assert_equal [1, 1], eval('l = {0, 0}; l[1] = l[2] = 1; return l;')
      assert_equal 'xx', eval('s = "ab"; s[1] = s[2] = "x"; return s;')
      assert_equal [1, 2, 1, 2], eval('l = {0, 0, 0, 0}; l[1..2] = l[3..4] = {1, 2}; return l;')
      assert_equal 'xyxy', eval('s = "abcd"; s[1..2] = s[3..4] = "xy"; return s;')
      assert_equal [1, 4], eval('m = [1 -> 0, 2 -> 0]; m[1] = length(m[2..2] = [2 -> 4]); return {m[1], m[2]};')
      assert_equal [10, 1, 30], eval('l = {10, 20}; l[$] = length(l[3..2] = {30}); return l;')
      assert_equal [1, 1], eval('m = ["part" -> {0, 0}]; m["part"][1] = m["part"][2] = 1; return m["part"];')
      assert_equal [1, 1], eval('l = {["a" -> 0, "b" -> 0]}; l[1]["a"] = l[1]["b"] = 1; return {l[1]["a"], l[1]["b"]};')
      assert_equal [7, 1], eval('l = {{0}, {0}}; l[l[2][1] = 1][1] = 7; return {l[1][1], l[2][1]};')
      assert_equal [5, 6, 0], eval('l = {{0, 0}}; old = l; l[1][1..1] = {l[1][2] = 6, 5}[2..2]; return {l[1][1], l[1][2], old[1][2]};')
    end
  end

  def test_indexed_property_assignment_preserves_writes_from_other_verbs
    run_test_as('programmer') do
      o = create(:nothing)
      add_property(o, 'p', {'a' => 0, 'b' => 0}, [player, ''])
      add_verb(o, [player, 'xd', 'change'], ['this', 'none', 'this'])
      set_verb_code(o, 'change', ['this.p["b"] = this.p["b"] + 1;', 'return 3;'])

      assert_equal [1, 1], eval(%Q{o = #{o}; o.p["a"] = o.("P")["b"] = 1; return {o.p["a"], o.p["b"]};})
      assert_equal [3, 2], eval(%Q{o = #{o}; o.p["a"] = o:change(); return {o.p["a"], o.p["b"]};})
      assert_equal [4, 3], eval(%Q{o = #{o}; o.p[o:change() ? "a" | "b"] = 4; return {o.p["a"], o.p["b"]};})
      assert_equal [5, 4], eval(<<~MOO.tr("\n", " "))
        o = #{o};
        fork (0)
          o:change();
        endfork
        o.p["a"] = {suspend(0), 5}[2];
        return {o.p["a"], o.p["b"]};
      MOO
    end
  end

  def test_indexed_assignment_handles_a_removed_intermediate_key
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'x', 'assign'], ['this', 'none', 'this'])
      set_verb_code(o, 'assign', [
        'm = ["part" -> ["a" -> 0]];',
        'result = (m["part"]["a"] = length(m = []));',
        'return {result, length(m)};'
      ])
      assert_equal [E_RANGE, 0], call(o, 'assign')
      assert_match(/m\["part"\]\["a"\] = length\(m = \[\]\)/, verb_code(o, 'assign').join("\n"))
      assert disassemble(o, 'assign').any? { |line| line.include?('REFRESH_LVALUE') }
      assert_equal [E_RANGE, 0], eval(<<~MOO.tr("\n", " "))
        m = ["part" -> ["a" -> 0]];
        try
          m["part"]["a"] = length(m = []);
        except error (E_RANGE)
          return {error[1], length(m)};
        endtry
      MOO
    end
  end

  def test_indexed_waif_property_assignment_preserves_both_writes
    run_test_as('programmer') do
      waif_class = create(:waif)
      add_property(waif_class, ':data', [0, 0], [player, ''])
      assert_equal [1, 1], eval(%Q{w = #{waif_class}:new(); w.data[1] = w.data[2] = 1; return w.data;})
    end
  end

  def test_that_disassemble_works
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'foobar'], ['this', 'none', 'this'])
      set_verb_code(o, 'foobar') do |vc|
        vc << 'return "foobar"[^..$];'
      end
      vc = simplify command %|; return disassemble(#{o}, "foobar");|
      assert vc[8] =~ /FIRST/
      assert vc[9] =~ /LAST/
    end
  end

  def test_that_exponentiation_has_higher_precedence
    run_test_as('programmer') do
      assert_equal [1, 2, 3, 4], eval(%|return {1, 2, 3, 4, 5, 6, 7}[^ .. 2 ^ 2];|)
      assert_equal 5, eval(%|return {1, 2, 3, 4, 5, 6, 7}[^ + 2 ^ 2];|)
      assert_equal 4, eval(%|return {1, 2, 3, 4, 5, 6, 7}[2 ^ 2];|)
    end
  end

  def test_that_index_and_range_fails_on_ints
    run_test_as('programmer') do
      assert_equal E_TYPE, eval(%|5[^];|)
      assert_equal E_TYPE, eval(%|5[$];|)
      assert_equal E_TYPE, eval(%|5[^..$];|)
      assert_equal E_TYPE, eval(%|x = 5; x[^] = {1, 2, 3}; return x;|)
      assert_equal E_TYPE, eval(%|x = 5; x[$] = {1, 2, 3}; return x;|)
      assert_equal E_TYPE, eval(%|x = 5; x[^..$] = {1, 2, 3}; return x;|)
    end
  end

  def test_that_index_and_range_fails_on_floats
    run_test_as('programmer') do
      assert_equal E_TYPE, eval(%|5.0[^];|)
      assert_equal E_TYPE, eval(%|5.0[$];|)
      assert_equal E_TYPE, eval(%|5.0[^..$];|)
      assert_equal E_TYPE, eval(%|x = 5.0; x[^] = {1, 2, 3}; return x;|)
      assert_equal E_TYPE, eval(%|x = 5.0; x[$] = {1, 2, 3}; return x;|)
      assert_equal E_TYPE, eval(%|x = 5.0; x[^..$] = {1, 2, 3}; return x;|)
    end
  end

  def test_that_mismatched_types_fails
    run_test_as('programmer') do
      assert_equal E_TYPE, eval(%|[1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5][[]];|)
      assert_equal E_TYPE, eval(%|[1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5][{}];|)
      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; x[^..$] = "foobar"; return x;|)
      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; x[^..$] = {1, 2, 3}; return x;|)

      assert_equal E_TYPE, eval(%|{1, 2, 3, 4, 5}[#1];|)
      assert_equal E_TYPE, eval(%|{1, 2, 3, 4, 5}["foo"];|)
      assert_equal E_TYPE, eval(%|x = {1, 2, 3, 4, 5}; x[#1] = {1, 2, 3}; return x;|)
      assert_equal E_TYPE, eval(%|x = {1, 2, 3, 4, 5}; x["foo"] = {1, 2, 3}; return x;|)
      assert_equal E_TYPE, eval(%|x = {1, 2, 3, 4, 5}; x[^..$] = "foobar"; return x;|)
      assert_equal E_TYPE, eval(%|x = {1, 2, 3, 4, 5}; x[^..$] = [#1 -> "foobar"]; return x;|)

      assert_equal E_TYPE, eval(%|"12345"[#1];|)
      assert_equal E_TYPE, eval(%|"12345"["foo"];|)
      assert_equal E_TYPE, eval(%|x = "12345"; x[#1] = "a"; return x;|)
      assert_equal E_TYPE, eval(%|x = "12345"; x["foo"] = {1, 2, 3}; return x;|)
      assert_equal E_TYPE, eval(%|x = "12345"; x[^..$] = {1, 2, 3}; return x;|)
      assert_equal E_TYPE, eval(%|x = "12345"; x[^..$] = [#1 -> "foobar"]; return x;|)
    end
  end

  def test_that_collections_are_invalid_as_indexes_and_in_ranges
    run_test_as('programmer') do
      assert_equal E_TYPE, eval(%|return [[] -> []];|)
      assert_equal E_TYPE, eval(%|return [{} -> {}];|)

      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; return x[[]];|)
      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; return x[{}];|)

      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; x[[]] = "1"; return x;|)
      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; x[{}] = #1; return x;|)

      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; return x[1..[]];|)
      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; return x[1..{}];|)

      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; x[1..[]] = ["1" -> "1"]; return x;|)
      assert_equal E_TYPE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5]; x[1..{}] = [#1 -> #1]; return x;|)
    end
  end

  def test_that_inverted_ranges_are_empty
    run_test_as('programmer') do
      assert_equal({}, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][6..2];|))
      assert_equal [], eval(%|return {1, 2, 3, 4, 5, 6, 7}[6..2];|)
      assert_equal '', eval(%|return "foobar"[5..2];|)
    end
  end

  def test_that_inverted_ranges_are_empty_even_if_the_indexes_are_invalid
    run_test_as('programmer') do
      assert_equal({}, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][16..12];|))
      assert_equal [], eval(%|return {1, 2, 3, 4, 5, 6, 7}[16..12];|)
      assert_equal '', eval(%|return "foobar"[15..12];|)
    end
  end

  def test_that_range_indexes_must_otherwise_be_valid
    run_test_as('programmer') do
      assert_equal E_RANGE, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][2..16];|)
      assert_equal E_RANGE, eval(%|return {1, 2, 3, 4, 5, 6, 7}[2..16];|)
      assert_equal E_RANGE, eval(%|return "foobar"[2..15];|)
      assert_equal E_RANGE, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][12..16];|)
      assert_equal E_RANGE, eval(%|return {1, 2, 3, 4, 5, 6, 7}[12..16];|)
      assert_equal E_RANGE, eval(%|return "foobar"[12..15];|)
      assert_equal E_RANGE, eval(%|return [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7][-2..6];|)
      assert_equal E_RANGE, eval(%|return {1, 2, 3, 4, 5, 6, 7}[-2..6];|)
      assert_equal E_RANGE, eval(%|return "foobar"[-2..5];|)
    end
  end

  def test_that_range_indexes_must_be_valid_on_assignment
    run_test_as('programmer') do
      assert_equal E_RANGE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; x[16..12] = ["a" -> "a", "b" -> "b"];|)
      assert_equal E_RANGE, eval(%|x = {1, 2, 3, 4, 5, 6, 7}; x[16..12] = {"a", "b"};|)
      assert_equal E_RANGE, eval(%|x = "foobar"; x[15..12] = "ab";|)
      assert_equal E_RANGE, eval(%|x = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 6 -> 6, 7 -> 7]; x[6..-2] = ["a" -> "a", "b" -> "b"];|)
      assert_equal E_RANGE, eval(%|x = {1, 2, 3, 4, 5, 6, 7}; x[6..-2] = {"a", "b"};|)
      assert_equal E_RANGE, eval(%|x = "foobar"; x[5..-2] = "ab";|)
    end
  end

end
