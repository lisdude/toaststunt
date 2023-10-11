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
