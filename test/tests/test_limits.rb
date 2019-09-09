require 'test_helper'

# Test various in-MOO limits on values.

class TestLimits < Test::Unit::TestCase

  def setup
    run_test_as('wizard') do
      evaluate('add_property($server_options, "max_concat_catchable", 1, {player, "r"})')
      evaluate('add_property($server_options, "max_string_concat", 1000000, {player, "r"})')
      evaluate('add_property($server_options, "max_list_value_bytes", 1000000, {player, "r"})')
      evaluate('add_property($server_options, "max_map_value_bytes", 1000000, {player, "r"})')
      evaluate('add_property($server_options, "fg_seconds", 123, {player, "r"})')
      evaluate('add_property($server_options, "fg_ticks", 2147483647, {player, "r"})')
      evaluate('load_server_options();')
    end
  end

  def teardown
    run_test_as('wizard') do
      evaluate('delete_property($server_options, "max_concat_catchable")')
      evaluate('delete_property($server_options, "max_string_concat")')
      evaluate('delete_property($server_options, "max_list_value_bytes")')
      evaluate('delete_property($server_options, "max_map_value_bytes")')
      evaluate('delete_property($server_options, "fg_seconds")')
      evaluate('delete_property($server_options, "fg_ticks")')
      evaluate('load_server_options();')
    end
  end

  def set_max_concat(max)
    evaluate("$server_options.max_string_concat = #{max};")
    evaluate('load_server_options();')
  end

  def set_max_value_bytes(max)
    evaluate("$server_options.max_list_value_bytes = #{max};")
    evaluate("$server_options.max_map_value_bytes = #{max};")
    evaluate('load_server_options();')
  end

  # pad the limits for account for argument lists to built-in functions

  def test_that_setadd_checks_list_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-10..10)
        set_max_value_bytes(1000000)
        pad = simplify(command("; return value_bytes({1, 2}) - value_bytes({});"))
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = setadd(x, i); endfor; return value_bytes(x);"))
        set_max_value_bytes(size + pad)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = setadd(x, i); endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = setadd(x, i); endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_setadd_fails_if_the_value_is_too_large
    run_test_as('wizard') do
      [5, 12].each do |n|
        n += rand(-1..1)
        set_max_value_bytes(1000000)
        pad = simplify(command("; return value_bytes({1, 2}) - value_bytes({});"))
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = setadd({}, {x, x}); endfor; return value_bytes(x);"))
        set_max_value_bytes(size + pad)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = setadd({}, {x, x}); endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = setadd({}, {x, x}); endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_listinsert_checks_list_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-10..10)
        set_max_value_bytes(1000000)
        pad = simplify(command("; return value_bytes({1, 2}) - value_bytes({});"))
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = listinsert(x, i); endfor; return value_bytes(x);"))
        set_max_value_bytes(size + pad)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = listinsert(x, i); endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = listinsert(x, i); endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_listinsert_fails_if_the_value_is_too_large
    run_test_as('wizard') do
      [5, 12].each do |n|
        n += rand(-1..1)
        set_max_value_bytes(1000000)
        pad = simplify(command("; return value_bytes({1, 2}) - value_bytes({});"))
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = listinsert({}, {x, x}); endfor; return value_bytes(x);"))
        set_max_value_bytes(size + pad)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = listinsert({}, {x, x}); endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = listinsert({}, {x, x}); endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_listappend_checks_list_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-10..10)
        set_max_value_bytes(1000000)
        pad = simplify(command("; return value_bytes({1, 2}) - value_bytes({});"))
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = listappend(x, i); endfor; return value_bytes(x);"))
        set_max_value_bytes(size + pad)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = listappend(x, i); endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = listappend(x, i); endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_listappend_fails_if_the_value_is_too_large
    run_test_as('wizard') do
      [5, 12].each do |n|
        n += rand(-1..1)
        set_max_value_bytes(1000000)
        pad = simplify(command("; return value_bytes({1, 2}) - value_bytes({});"))
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = listappend({}, {x, x}); endfor; return value_bytes(x);"))
        set_max_value_bytes(size + pad)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = listappend({}, {x, x}); endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = listappend({}, {x, x}); endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_listset_fails_if_the_value_is_too_large
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-10..10)
        set_max_value_bytes(1000000)
        pad = simplify(command("; return value_bytes({{}, {}, 3}) - value_bytes({});"))
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = listset({0}, {x}, 1); endfor; return value_bytes(x);"))
        set_max_value_bytes(size + pad)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = listset({0}, {x}, 1); endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = listset({0}, {x}, 1); endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_setremove_fails_is_the_result_is_too_large
    run_test_as('wizard') do
      a = create(NOTHING)
      add_property(a, 'foo', 0, [a, ''])
      command "; x = {}; for i in [1..1000]; x = {@x, i}; endfor; #{a}.foo = x;"
      set_max_value_bytes(1000)
      assert_equal E_QUOTA, simplify(command("; setremove(#{a}.foo, 1);"))
    end
  end

  def test_that_listdelete_fails_is_the_result_is_too_large
    run_test_as('wizard') do
      a = create(NOTHING)
      add_property(a, 'foo', 0, [a, ''])
      command "; x = {}; for i in [1..1000]; x = {@x, i}; endfor; #{a}.foo = x;"
      set_max_value_bytes(1000)
      assert_equal E_QUOTA, simplify(command("; listdelete(#{a}.foo, 1);"))
    end
  end

  def test_that_appending_to_a_list_checks_list_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-10..10)
        set_max_value_bytes(1000000)
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = {@x, i}; endfor; return value_bytes(x);"))
        set_max_value_bytes(size + 1)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = {@x, i}; endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = {@x, i}; endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_prepending_to_a_list_checks_list_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-10..10)
        set_max_value_bytes(1000000)
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = {i, @x}; endfor; return value_bytes(x);"))
        set_max_value_bytes(size + 1)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = {i, @x}; endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = {i, @x}; endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_indexset_on_a_list_checks_list_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-10..10)
        set_max_value_bytes(1000000)
        size = simplify(command("; x = {0}; for i in [1..#{n}]; x[1] = {x}; endfor; return value_bytes(x);"))
        set_max_value_bytes(size + 1)
        assert_equal size, simplify(command("; x = {0}; for i in [1..#{n}]; x[1] = {x}; endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {0}; for i in [1..#{n + 1}]; x[1] = {x}; endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_rangeset_on_a_list_checks_list_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-1..1)
        set_max_value_bytes(1000000)
        size = simplify(command("; x = {0}; for i in [1..#{n}]; x[1..1] = {x}; endfor; return value_bytes(x);"))
        set_max_value_bytes(size + 1)
        assert_equal size, simplify(command("; x = {0}; for i in [1..#{n}]; x[1..1] = {x}; endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {0}; for i in [1..#{n + 1}]; x[1..1] = {x}; endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_assigning_a_list_fails_if_the_value_is_too_large
    run_test_as('wizard') do
      [5, 12].each do |n|
        n += rand(-1..1)
        set_max_value_bytes(1000000)
        size = simplify(command("; x = {}; for i in [1..#{n}]; x = {x, x}; endfor; return value_bytes(x);"))
        set_max_value_bytes(size + 1)
        assert_equal size, simplify(command("; x = {}; for i in [1..#{n}]; x = {x, x}; endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = {}; for i in [1..#{n + 1}]; x = {x, x}; endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_decode_binary_checks_list_max_value_bytes
    run_test_as('wizard') do
      a = create(NOTHING)
      add_property(a, 'foo', 0, [a, ''])
      command "; x = \"x\"; for i in [1..12]; x = tostr(x, x); endfor; #{a}.foo = x;"
      set_max_value_bytes(1000000)
      pad = simplify(command("; return value_bytes({1, 2}) - value_bytes({});"))
      size = simplify(command("; x = #{a}.foo; return value_bytes(decode_binary(x));"))
      set_max_value_bytes(size + pad)
      assert_equal size, simplify(command("; x = #{a}.foo; return value_bytes(decode_binary(x));"))
      assert_equal E_QUOTA, simplify(command("; x = #{a}.foo; x[2..4] = \"~00~0D~0A\"; return value_bytes(decode_binary(x));"))
      assert_equal E_QUOTA, simplify(command("; x = #{a}.foo; return value_bytes(decode_binary(x, 1));"))
    end
  end

  def test_that_mapdelete_fails_is_the_result_is_too_large
    run_test_as('wizard') do
      a = create(NOTHING)
      add_property(a, 'foo', 0, [a, ''])
      command "; x = []; for i in [1..1000]; x[i] = i; endfor; #{a}.foo = x;"
      set_max_value_bytes(1000)
      assert_equal E_QUOTA, simplify(command("; mapdelete(#{a}.foo, 1);"))
    end
  end

  def test_that_indexset_on_a_map_checks_map_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-10..10)
        set_max_value_bytes(1000000)
        size = simplify(command("; x = []; for i in [1..#{n}]; x[1] = {x}; endfor; return value_bytes(x);"))
        set_max_value_bytes(size + 1)
        assert_equal size, simplify(command("; x = []; for i in [1..#{n}]; x[1] = {x}; endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = []; for i in [1..#{n + 1}]; x[1] = {x}; endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_rangeset_on_a_map_checks_map_max_value_bytes
    run_test_as('wizard') do
      [90, 250, 1000].each do |n|
        n += rand(-1..1)
        set_max_value_bytes(1000000)
        size = simplify(command("; x = [1 -> 1]; for i in [1..#{n}]; x[1..1] = [1 -> x]; endfor; return value_bytes(x);"))
        set_max_value_bytes(size + 1)
        assert_equal size, simplify(command("; x = [1 -> 1]; for i in [1..#{n}]; x[1..1] = [1 -> x]; endfor; return value_bytes(x);"))
        assert_equal E_QUOTA, simplify(command("; x = [1 -> 1]; for i in [1..#{n + 1}]; x[1..1] = [1 -> x]; endfor; return value_bytes(x);"))
      end
    end
  end

  def test_that_making_a_list_literal_checks_list_max_value_bytes
    run_test_as('wizard') do
      set_max_value_bytes(2000)
      x = (0..1000).inject([]) { |acc, i| acc << i.to_s; acc }
      assert_equal E_QUOTA, eval("{#{x.join(',')}};")
    end
  end

  def test_that_making_a_map_literal_checks_map_max_value_bytes
    run_test_as('wizard') do
      set_max_value_bytes(2000)
      x = (0..1000).inject([]) { |acc, i| acc << "#{i}->#{i}"; acc }
      assert_equal E_QUOTA, eval("[#{x.join(',')}];")
    end
  end

  def test_that_string_limits_work
    run_test_as('wizard') do
      set_max_concat(3210)
      assert_equal 3210, simplify(command('; x = ""; for i in [1..3210]; x = x + " "; endfor; return length(x);'))
      assert_equal E_QUOTA, simplify(command('; x = ""; for i in [1..3211]; x = x + " "; endfor; return length(x);'))
      assert_equal 3210, simplify(command('; x = ""; for i in [1..3210]; x = tostr(" ", x); endfor; return length(x);'))
      assert_equal E_QUOTA, simplify(command('; x = ""; for i in [1..3211]; x = tostr(" ", x); endfor; return length(x);'))
      assert_equal 2046, simplify(command('; x = ""; for i in [1..10]; x = toliteral(x); endfor; return length(x);'))
      assert_equal E_QUOTA, simplify(command('; x = ""; for i in [1..11]; x = toliteral(x); endfor; return length(x);'))
      assert_equal 2048, simplify(command('; x = " "; for i in [1..11]; x = strsub(x, " ", "  "); endfor; return length(x);'))
      assert_equal E_QUOTA, simplify(command('; x = " "; for i in [1..12]; x = strsub(x, " ", "  "); endfor; return length(x);'))
      assert_equal 3209, simplify(command('; x = "~"; for i in [1..1604]; x = encode_binary(x); endfor; return length(x);'))
      assert_equal E_QUOTA, simplify(command('; x = "~"; for i in [1..1605]; x = encode_binary(x); endfor; return length(x);'))
      assert_equal 2048, simplify(command('; x = "%1"; for i in [1..10]; x = substitute(x, {1, 4, {{1, 4}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}}, "%1%1"}); endfor; return length(x);'))
      assert_equal E_QUOTA, simplify(command('; x = "%1"; for i in [1..11]; x = substitute(x, {1, 4, {{1, 4}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}, {0, -1}}, "%1%1"}); endfor; return length(x);'))
      assert_equal 2616, simplify(command('; x = "!"; for i in [1..21]; x = encode_base64(x); endfor; return length(x);'))
      assert_equal E_QUOTA, simplify(command('; x = "!"; for i in [1..22]; x = encode_base64(x); endfor; return length(x);'))
      # can't be exact due to MOO's lame binary strings
      assert_equal 1, simplify(command('; x = 1001; return length(random_bytes(x)) > 1;'))
      assert_equal E_QUOTA, simplify(command('; x = 3211; return length(random_bytes(x)) > 1;'))
    end
  end

  # `mapforeach()' was not exception safe and leaked memory when
  #  a quota error was thrown while iterating
  def test_that_quota_errors_do_not_leak_memory
    run_test_as('wizard') do
      set_max_concat(3210)
      assert_equal E_QUOTA, simplify(command('; x = []; for i in [1..1605]; x[tostr(random())] = {tostr(random())}; endfor; return toliteral(x);'))
    end
  end

end
