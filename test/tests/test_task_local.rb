require 'test_helper'

class TestTaskLocal < Test::Unit::TestCase

  def test_that_setting_task_local_fails_for_non_wizards
    run_test_as('programmer') do
      assert_equal E_PERM, set_task_local(1)
    end
  end

  def test_that_getting_task_local_fails_for_non_wizards
    run_test_as('programmer') do
      assert_equal E_PERM, task_local()
    end
  end

  def test_that_set_task_local_takes_one_argument
    run_test_as('wizard') do
      assert_equal E_ARGS, simplify(command(%Q|; return set_task_local();|))
      assert_equal E_ARGS, simplify(command(%Q|; return set_task_local(1, 2, 3);|))
      assert_equal 0, simplify(command(%Q|; return set_task_local("1");|))
    end
  end

  def test_that_task_local_takes_no_arguments
    run_test_as('wizard') do
      assert_equal E_ARGS, simplify(command(%Q|; return task_local({});|))
      assert_equal({}, simplify(command(%Q|; return task_local();|)))
    end
  end

  def test_that_the_simple_eval_case_works
    run_test_as('wizard') do
      assert_equal ["foo", "bar", 1, 2, 3], simplify(command(%Q|; set_task_local({"foo", "bar", 1, 2, 3}); return task_local();|))
    end
  end

  def test_that_the_eval_case_works_with_suspend
    run_test_as('wizard') do
      send_string %Q|; set_task_local({"foo", "bar", 2, 3, 4}); suspend(1); return task_local();|

      line = nil
      while (true)
        line = @sock.gets.chomp
        break if line[0] == ?{
      end

      assert_equal ["foo", "bar", 2, 3, 4], simplify(line)
    end
  end

  def test_that_the_eval_case_works_with_fork_and_suspend
    run_test_as('wizard') do
      send_string %Q|; set_task_local({1, 2}); id = task_id(); fork (1); set_task_local({"foo", "bar", 3, 4, 5}); suspend(1); resume(id, task_local()); endfork; return {task_local(), suspend()};|

      line = nil
      while (true)
        line = @sock.gets.chomp
        break if line[0] == ?{
      end

      assert_equal [[1, 2], ["foo", "bar", 3, 4, 5]], simplify(line)
    end
  end

  def test_that_the_eval_case_works_when_errors_are_raised
    run_test_as('wizard') do
      assert_equal E_DIV, simplify(command(%Q|; set_task_local({"foo", "bar", 1, 2, 3}); 1/0; return task_local();|))
    end
  end

  def test_that_the_suspend_case_works_when_errors_are_raised
    run_test_as('wizard') do
      send_string %Q|; set_task_local({"foo", "bar", 2, 3, 4}); suspend(1); 1/0; return task_local();|

      line = nil
      while (true)
        line = @sock.gets.chomp
        break if line[0] == ?{
      end

      assert_equal E_DIV, simplify(line)
    end
  end

  def test_that_the_fork_and_suspend_case_works_when_errors_are_raised
    run_test_as('wizard') do
      send_string %Q|; set_task_local({1, 2}); id = task_id(); fork (1); set_task_local({"foo", "bar", 3, 4, 5}); suspend(1); resume(id, task_local()); endfork; suspend(); 1/0; return {task_local()};|

      line = nil
      while (true)
        line = @sock.gets.chomp
        break if line[0] == ?{
      end

      assert_equal E_DIV, simplify(line)
    end
  end

  def test_that_it_works_with_a_command_verb
    run_test_as('wizard') do
      x = create(:nothing)

      add_verb(x, [player, 'd', 'foo'], ['any', 'any', 'any'])
      set_verb_code(x, 'foo') do |code|
        code << %Q|set_task_local({argstr});|
        code << %Q|this:bar();|
      end

      add_verb(x, [player, 'xd', 'bar'], ['this', 'none', 'this'])
      set_verb_code(x, 'bar') do |code|
        code << %Q|notify(player, "> " + toliteral(task_local()));|
      end

      send_string %Q|foo #{x} to the #{x}|

      line = nil
      while (true)
        line = @sock.gets.chomp
        break if line[0] == ?>
      end

      assert_equal "> {\"#{x} to the #{x}\"}", line
    end
  end

  def test_that_it_works_with_a_server_verb
    run_test_as('wizard') do
      x = create(:nothing)

      add_verb(x, [player, 'xd', 'recycle'], ['this', 'none', 'this'])
      set_verb_code(x, 'recycle') do |code|
        code << %Q|this:bar();|
      end

      add_verb(x, [player, 'xd', 'bar'], ['this', 'none', 'this'])
      set_verb_code(x, 'bar') do |code|
        code << %Q|notify(player, "> " + toliteral(task_local()));|
      end

      send_string %Q|; set_task_local({1, {2, {3}}}); recycle(#{x});|

      line = nil
      while (true)
        line = @sock.gets.chomp
        break if line[0] == ?>
      end

      assert_equal "> {1, {2, {3}}}", line
    end
  end

  def test_that_it_works_across_verb_calls
    run_test_as('wizard') do
      x = create(:nothing)

      add_verb(x, [player, 'xd', 'foo'], ['this', 'none', 'this'])
      set_verb_code(x, 'foo') do |code|
        code << %Q|{value} = args;|
        code << %Q|set_task_local(value);|
      end

      add_verb(x, [player, 'xd', 'bar'], ['this', 'none', 'this'])
      set_verb_code(x, 'bar') do |code|
        code << %Q|return task_local();|
      end

      assert_equal 1.234, simplify(command(%Q|; #{x}:foo(1.234); return #{x}:bar();|))

      add_verb(x, [player, 'xd', 'baz'], ['this', 'none', 'this'])
      set_verb_code(x, 'baz') do |code|
        code << %Q|{value} = args;|
        code << %Q|#{x}:foo(value);|
        code << %Q|return #{x}:bar();|
      end

      assert_equal 23.456, simplify(command(%Q|; return #{x}:baz(23.456);|))
    end
  end

  def test_that_it_works_even_when_the_task_suspends_between_verb_calls
    run_test_as('wizard') do
      x = create(:nothing)

      add_verb(x, [player, 'xd', 'foo'], ['this', 'none', 'this'])
      set_verb_code(x, 'foo') do |code|
        code << %Q|{value} = args;|
        code << %Q|set_task_local(value);|
      end

      add_verb(x, [player, 'xd', 'bar'], ['this', 'none', 'this'])
      set_verb_code(x, 'bar') do |code|
        code << %Q|return task_local();|
      end

      add_verb(x, [player, 'xd', 'baz'], ['this', 'none', 'this'])
      set_verb_code(x, 'baz') do |code|
        code << %Q|{value} = args;|
        code << %Q|#{x}:foo(value);|
        code << %Q|fork (0)|
        code << %Q|for i in [1..10]|
        code << %Q|suspend(1);|
        code << %Q|endfor|
        code << %Q|endfork|
        code << %Q|total = 1.0;|
        code << %Q|for i in [1..5]|
        code << %Q|suspend(1);|
        code << %Q|total = total * #{x}:bar();|
        code << %Q|endfor|
        code << %Q|return total;|
      end

      send_string %Q|; return #{x}:baz(2.5);|

      line = nil
      while (true)
        line = @sock.gets.chomp
        break if line[0] == ?{
      end

      assert_equal 97.65625, simplify(line)
    end
  end

  def test_that_it_works_even_when_the_task_reads_between_verb_calls
    run_test_as('wizard') do
      x = create(:nothing)

      add_verb(x, [player, 'xd', 'foo'], ['this', 'none', 'this'])
      set_verb_code(x, 'foo') do |code|
        code << %Q|{value} = args;|
        code << %Q|set_task_local(value);|
      end

      add_verb(x, [player, 'xd', 'bar'], ['this', 'none', 'this'])
      set_verb_code(x, 'bar') do |code|
        code << %Q|return task_local();|
      end

      add_verb(x, [player, 'xd', 'baz'], ['this', 'none', 'this'])
      set_verb_code(x, 'baz') do |code|
        code << %Q|{value} = args;|
        code << %Q|#{x}:foo(value);|
        code << %Q|fork (0)|
        code << %Q|for i in [1..10]|
        code << %Q|suspend(1);|
        code << %Q|endfor|
        code << %Q|endfork|
        code << %Q|total = 1.0;|
        code << %Q|for i in [1..5]|
        code << %Q|read();|
        code << %Q|total = total * #{x}:bar();|
        code << %Q|endfor|
        code << %Q|return total;|
      end

      send_string %Q|; return #{x}:baz(2.5);|
      sleep(1) and send_string %Q|a|
      sleep(1) and send_string %Q|b|
      sleep(1) and send_string %Q|c|
      sleep(1) and send_string %Q|d|
      sleep(1) and send_string %Q|e|

      line = nil
      while (true)
        line = @sock.gets.chomp
        break if line[0] == ?{
      end

      assert_equal 97.65625, simplify(line)
    end
  end

  # The following tests catch things that introduced memory leaks in
  # the past.  They don't test functionality -- they are meant to be
  # used with a tool like valgrind.

  def test_a_few_nonfunctional_cases
    run_test_as('wizard') do
      assert_equal 0, simplify(command(%Q|; fork (0); suspend(0); endfork|))
      send_string %Q|; kill_task(task_id());|
    end
  end

end
