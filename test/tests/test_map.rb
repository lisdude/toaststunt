require 'test_helper'

class TestMap < Test::Unit::TestCase

  def test_that_literal_hash_notation_works
    run_test_as('programmer') do
      m1 = MooObj.new('#1')
      m2 = MooObj.new('#2')
      m3 = MooObj.new('#3')
      assert_equal({}, simplify(command(%Q|; return [];|)))
      assert_equal({1 => 2}, simplify(command(%Q|; return [1 -> 2];|)))
      assert_equal({m1 => m2, 3 => 4}, simplify(command(%Q|; return [#1 -> #2, 3 -> 4];|)))
      assert_equal({m1 => {'a' => [], 'b' => []}, m2 => {'b' => {E_ARGS => {1.0 => {}}}}, m3 => {}}, simplify(command(%Q|; return [#1 -> ["a" -> {}, "b" -> {}], #2 -> ["b" -> [E_ARGS -> [1.0 -> []]]], #3 -> []];|)))
    end
  end

  def test_that_a_map_is_sorted_no_matter_the_order_the_values_are_inserted
    run_test_as('programmer') do
      x = simplify(command(%Q|; x = [3 -> 3, 1 -> 1, 4 -> 4, 5 -> 5, 9 -> 9, 2 -> 2]; x["a"] = "a"; x[6] = 6; return x;|))
      y = simplify(command(%Q|; y = [2 -> 2, 9 -> 9, 5 -> 5, 4 -> 4, 1 -> 1, 3 -> 3]; y["a"] = "a"; y[6] = 6; return y;|))
      z = simplify(command(%Q|; z = [1 -> 1, 2 -> 2, 3 -> 3, 4 -> 4, 5 -> 5, 9 -> 9]; z["a"] = "a"; z[6] = 6; return z;|))
      assert_equal({1 => 1, 2 => 2, 3 => 3, 4 => 4, 5 => 5, 6 => 6, 9 => 9, "a" => "a"}, x)
      assert_equal({1 => 1, 2 => 2, 3 => 3, 4 => 4, 5 => 5, 6 => 6, 9 => 9, "a" => "a"}, y)
      assert_equal({1 => 1, 2 => 2, 3 => 3, 4 => 4, 5 => 5, 6 => 6, 9 => 9, "a" => "a"}, z)
      assert_equal [1, 2, 3, 4, 5, 6, 9, "a"], mapkeys(x)
      assert_equal [1, 2, 3, 4, 5, 6, 9, "a"], mapkeys(y)
      assert_equal [1, 2, 3, 4, 5, 6, 9, "a"], mapkeys(z)
      assert_equal [1, 2, 3, 4, 5, 6, 9, "a"], mapvalues(x)
      assert_equal [1, 2, 3, 4, 5, 6, 9, "a"], mapvalues(y)
      assert_equal [1, 2, 3, 4, 5, 6, 9, "a"], mapvalues(z)
    end
  end

  def test_that_mapdelete_deletes_an_entry
    run_test_as('programmer') do
      x = simplify(command(%Q|; return [E_NONE -> "No error", E_TYPE -> "Type mismatch", E_DIV -> "Division by zero", E_PERM -> "Permission denied"];|))
      assert_equal({E_NONE => "No error", E_DIV => "Division by zero", E_PERM => "Permission denied"}, x = mapdelete(x, E_TYPE))
      assert_equal({E_DIV => "Division by zero", E_PERM => "Permission denied"}, x = mapdelete(x, E_NONE))
      assert_equal({E_PERM => "Permission denied"}, x = mapdelete(x, E_DIV))
      assert_equal({}, x = mapdelete(x, E_PERM))
      assert_equal(E_RANGE, mapdelete(x, E_ARGS))
    end
  end

  def test_that_length_returns_the_number_of_entries_in_a_map
    run_test_as('programmer') do
      x = simplify(command(%Q|; return ["3" -> "3", "1" -> "1", "4" -> "4", "5" -> "5", "9" -> "9", "2" -> "2"];|))
      assert_equal 6, length(x)
      x = simplify(command(%Q|; x = ["3" -> "3", "1" -> "1", "4" -> "4", "5" -> "5", "9" -> "9", "2" -> "2"]; x = mapdelete(x, "3"); return x;|))
      assert_equal 5, length(x)
    end
  end

  def test_that_is_member_and_in_return_true_if_value_is_in_a_map
    run_test_as('programmer') do
      x = simplify(command(%Q|; return ["3" -> "3", "1" -> "1", "4" -> "4", "5" -> "5", "9" -> "9", "2" -> "2"];|))
      assert_equal 5, is_member("5", x)
      assert_equal 0, is_member(5, x)
      assert_equal(2, simplify(command %|; return "2" in #{value_ref(x)};|))
      assert_equal(0, simplify(command %|; return 2 in #{value_ref(x)};|))
      # however, `is_member' is case-sensitive
      y = simplify(command(%Q|; return ["FOO" -> "BAR"];|))
      assert_equal 0, is_member("bar", y)
      assert_equal(1, simplify(command %|; return "bar" in #{value_ref(y)};|))
      # "foo" doesn't work (in any case)
      assert_equal 0, is_member("foo", y)
      assert_equal(0, simplify(command %|; return "foo" in #{value_ref(y)};|))
      assert_equal 0, is_member("FOO", y)
      assert_equal(0, simplify(command %|; return "FOO" in #{value_ref(y)};|))
    end
  end

  def test_that_tests_for_equality_work
    run_test_as('programmer') do
      assert_equal("yes", simplify(command %Q{; return equal([], []) && "yes" || "no";}))
      assert_equal("no", simplify(command %Q{; return equal([1 -> 2], []) && "yes" || "no";}))
      assert_equal("yes", simplify(command %Q{; return equal([1 -> 2], [1 -> 2]) && "yes" || "no";}))
      assert_equal("yes", simplify(command %Q{; return equal([1 -> 2, 3 -> 4], [3 -> 4, 1 -> 2]) && "yes" || "no";}))
      assert_equal("yes", simplify(command %Q{; return equal([1 -> [2 -> 3]], [1 -> [2 -> 3]]) && "yes" || "no";}))
      assert_equal("no", simplify(command %Q{; return equal([1 -> [2 -> 3]], [1 -> [2 -> 4]]) && "yes" || "no";}))
      assert_equal("yes", simplify(command %Q{; return [] == [] && "yes" || "no";}))
      assert_equal("no", simplify(command %Q{; return [1 -> 2] == [] && "yes" || "no";}))
      assert_equal("yes", simplify(command %Q{; return [1 -> 2] == [1 -> 2] && "yes" || "no";}))
      assert_equal("yes", simplify(command %Q{; return [1 -> 2, 3 -> 4] == [3 -> 4, 1 -> 2] && "yes" || "no";}))
      assert_equal("yes", simplify(command %Q{; return [1 -> [2 -> 3]] == [1 -> [2 -> 3]] && "yes" || "no";}))
      assert_equal("no", simplify(command %Q{; return [1 -> [2 -> 3]] == [1 -> [2 -> 4]] && "yes" || "no";}))
      # however, `equal' is case-sensitive
      assert_equal("no", simplify(command %Q{; return equal(["foo" -> "bar"], ["FOO" -> "BAR"]) && "yes" || "no";}))
      assert_equal("yes", simplify(command %Q{; return ["foo" -> "bar"] == ["FOO" -> "BAR"] && "yes" || "no";}))
    end
  end

  def test_that_maps_act_as_true_and_false
    run_test_as('programmer') do
      assert_equal("no", simplify(command '; return [] && "yes" || "no";'))
      assert_equal("yes", simplify(command '; return [1 -> 2] && "yes" || "no";'))
    end
  end

  def test_that_tostr_and_toliteral_work
    run_test_as('programmer') do
      x = simplify(command(%Q|; return [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14];|))
      assert_equal '[map]', tostr(x)
      assert_equal '[5 -> 5, #-1 -> #-1, 3.14 -> 3.14, "1" -> {}, "2" -> []]', toliteral(x)
    end
  end

  def test_that_assignment_copies
    run_test_as('programmer') do
      assert_equal("yes", simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; y = x; return x == y && "yes" || "no";))))
      assert_equal("no", simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; y = x; x["1"] = "foo"; return x == y && "yes" || "no";))))
      assert_equal("no", simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; y = x; y[1] = "foo"; return x == y && "yes" || "no";))))
    end
  end

  def test_that_maps_support_indexed_access
    run_test_as('programmer') do
      assert_equal([], simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; return x["1"];))))
      assert_equal("three", simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> ["3" -> "three"], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; return x["2"]["3"];))))
      assert_equal(3.14, simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; return x[3.14];))))
      assert_equal(E_RANGE, simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; return x[1.0];))))
      assert_equal({NOTHING => NOTHING, "2" => {}, "1" => "foo", 5 => 5, 3.14 => 3.14}, simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; x["1"] = "foo"; return x;))))
      assert_equal({NOTHING => NOTHING, "2" => {"3" => "foo"}, "1" => [], 5 => 5, 3.14 => 3.14}, simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> ["3" -> "three"], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; x["2"]["3"] = "foo"; return x;))))
      assert_equal({NOTHING => NOTHING, "2" => {}, "1" => [], 5 => 5, 3.14 => "bar"}, simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; x[3.14] = "bar"; return x;))))
      assert_equal({NOTHING => NOTHING, "2" => {}, "1" => [], 5 => 5, 1.0 => "baz", 3.14 => 3.14}, simplify(command(%Q(; x = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; x[1.0] = "baz"; return x;))))
    end
  end

  def test_that_indexed_access_on_objects_mutates_those_objects
    run_test_as('programmer') do
      o = create(:nothing)
      add_property(o, 'p', {}, [player, ''])
      assert_equal([], simplify(command(%Q(; #{o}.p = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; return #{o}.p["1"];))))
      assert_equal("three", simplify(command(%Q(; #{o}.p = [#{NOTHING} -> #{NOTHING}, "2" -> ["3" -> "three"], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; return #{o}.p["2"]["3"];))))
      assert_equal(3.14, simplify(command(%Q(; #{o}.p = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; return #{o}.p[3.14];))))
      assert_equal(E_RANGE, simplify(command(%Q(; #{o}.p = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; return #{o}.p[1.0];))))
      assert_equal({NOTHING => NOTHING, "2" => {}, "1" => "foo", 5 => 5, 3.14 => 3.14}, simplify(command(%Q(; #{o}.p = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; #{o}.p["1"] = "foo"; return #{o}.p;))))
      assert_equal({NOTHING => NOTHING, "2" => {"3" => "foo"}, "1" => [], 5 => 5, 3.14 => 3.14}, simplify(command(%Q(; #{o}.p = [#{NOTHING} -> #{NOTHING}, "2" -> ["3" -> "three"], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; #{o}.p["2"]["3"] = "foo"; return #{o}.p;))))
      assert_equal({NOTHING => NOTHING, "2" => {}, "1" => [], 5 => 5, 3.14 => "bar"}, simplify(command(%Q(; #{o}.p = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; #{o}.p[3.14] = "bar"; return #{o}.p;))))
      assert_equal({NOTHING => NOTHING, "2" => {}, "1" => [], 5 => 5, 1.0 => "baz", 3.14 => 3.14}, simplify(command(%Q(; #{o}.p = [#{NOTHING} -> #{NOTHING}, "2" -> [], "1" -> {}, 5 -> 5, 3.14 -> 3.14]; #{o}.p[1.0] = "baz"; return #{o}.p;))))
    end
  end

  def test_that_lists_and_maps_cannot_be_keys
    run_test_as('programmer') do
      assert_equal E_TYPE, simplify(command(%Q(; [[] -> 1];)))
      assert_equal E_TYPE, simplify(command(%Q(; [{} -> 1];)))
      assert_equal E_TYPE, simplify(command(%Q(; [[1 -> 2] -> 1];)))
      assert_equal E_TYPE, simplify(command(%Q(; [{1, 2} -> 1];)))
      assert_equal E_TYPE, simplify(command(%Q(; x = []; x[[]] = 1;)))
      assert_equal E_TYPE, simplify(command(%Q(; x = []; x[{}] = 1;)))
      assert_equal E_TYPE, simplify(command(%Q(; x = []; x[[1 -> 2]] = 1;)))
      assert_equal E_TYPE, simplify(command(%Q(; x = []; x[{1, 2}] = 1;)))
      assert_equal E_TYPE, mapdelete({1 => 2, 3 => 4}, {})
      assert_equal E_TYPE, mapdelete({1 => 2, 3 => 4}, [])
      assert_equal E_TYPE, mapdelete({1 => 2, 3 => 4}, {1 => 2})
      assert_equal E_TYPE, mapdelete({1 => 2, 3 => 4}, [1, 2])
    end
  end

  def test_that_anonymous_objects_cannot_be_keys
    run_test_as('programmer') do
      assert_equal E_TYPE, simplify(command(%Q(; mapdelete([1 -> 2, 3 -> 4], create($anonymous, 1));)))
    end
  end

  def test_that_first_and_last_indexes_work_on_maps
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'grow'], ['this', 'none', 'this'])
      set_verb_code(o, 'grow') do |vc|
        vc << 'x = [];';
        vc << 'r = {};';
        vc << 'x[1] = 1; x[(a = ^)..(b = $)]; r = {@r, {a, b}};'
        vc << 'x[2] = 2; x[(a = ^)..(b = $)]; r = {@r, {a, b}};'
        vc << 'x[3] = 3; x[(a = ^)..(b = $)]; r = {@r, {a, b}};'
        vc << 'x[5] = 5; x[(a = ^)..(b = $)]; r = {@r, {a, b}};'
        vc << 'x[8] = 8; x[(a = ^)..(b = $)]; r = {@r, {a, b}};'
        vc << 'return r;'
      end
      r = call(o, 'grow')
      assert_equal [[1, 1], [1, 2], [1, 3], [1, 5], [1, 8]], r
    end
  end

  def test_that_ranged_set_works_on_maps
    run_test_as('programmer') do
      assert_equal E_RANGE, simplify(command('; x = [1 -> 1]; x[3..2] = ["a" -> "a", "b" -> "b"]; return x;'))
      assert_equal E_RANGE, simplify(command('; x = [1 -> 1]; x[2..1] = ["a" -> "a", "b" -> "b"]; return x;'))
      assert_equal E_RANGE, simplify(command('; x = [1 -> 1]; x[1..0] = ["a" -> "a", "b" -> "b"]; return x;'))
      assert_equal({'a' => 'a', 'b' => 'b'}, simplify(command('; x = [1 -> 1]; x[1..1] = ["a" -> "a", "b" -> "b"]; return x;')))
      assert_equal({1 => 1, 2 => 2, 'a' => 'a', 'b' => 'b'}, simplify(command('; x = [1 -> 1, 2 -> 2]; x[2..1] = ["a" -> "a", "b" -> "b"]; return x;')))
      assert_equal({1 => 1, 2 => 2, 'a' => 'foo', 'b' => 'b'}, simplify(command('; x = [1 -> 1, 2 -> 2, "a" -> "foo"]; x[2..1] = ["a" -> "a", "b" -> "b"]; return x;')))
      assert_equal({'a' => 'a', 'b' => 'b'}, simplify(command('; x = [1 -> 1, 2 -> 2]; x[1..2] = ["a" -> "a", "b" -> "b"]; return x;')))
    end
  end

  def test_that_inverted_ranged_set_does_not_crash_the_server
    run_test_as('programmer') do
      assert_equal E_RANGE, simplify(command(%Q(; x = []; for i in [1..10]; x[1..0] = [i -> i]; endfor; return length(x);)))
    end
  end

end
