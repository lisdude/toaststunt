require 'test_helper'

class TestExec < Test::Unit::TestCase

  def test_that_exec_fails_for_non_wizards
    run_test_as('programmer') do
      assert_equal E_PERM, exec(['foobar'])
    end
  end

  def test_that_exec_fails_for_nonexistent_executables
    run_test_as('wizard') do
      r = exec(['test_does_not_exist'])
      assert_equal E_INVARG, r
    end
  end

  def test_that_exec_fails_for_bad_paths
    run_test_as('wizard') do
      r = exec(['/test_io'])
      assert_equal E_INVARG, r
      r = exec(['./test_io'])
      assert_equal E_INVARG, r
      r = exec(['../test_io'])
      assert_equal E_INVARG, r
      r = exec(['foo/../../test_io'])
      assert_equal E_INVARG, r
    end
  end

  def test_that_io_works
    run_test_as('wizard') do
      r = exec(['test_io'], 'Hello, world!')
      assert_equal [0, 'Hello, world!', 'Hello, world!'], r
    end
  end

  def test_that_args_works
    run_test_as('wizard') do
      r = exec(['test_args', 'one', 'two', 'three'], '')
      assert_equal [0, 'one two three', ''], r
    end
  end

  def test_that_exit_status_works
    run_test_as('wizard') do
      r = exec(['test_exit_status', '2'], '')
      assert_equal [2, '', ''], r
    end
  end

  def test_that_executables_that_take_a_long_time_work
    run_test_as('wizard') do
      r = exec(['test_with_sleep'], '')
      assert_equal [0, '1~0A2~0A3~0A', ''], r
    end
  end

  def test_that_stdin_takes_the_right_kind_of_binary_string
    run_test_as('wizard') do
      r = exec(['test_io'], '1~ZZ23~0A')
      assert_equal E_INVARG, r
    end
  end

  def test_that_showing_queued_tasks_and_killing_a_suspended_exec_works
    run_test_as('wizard') do
      assert_equal [], queued_tasks()
      task_id = eval('fork go (0); exec({"sleep", "5"}); endfork; suspend(0); return go;')
      o = queued_tasks()
      assert_equal task_id, o[0]
      assert_equal 'executables/sleep', o[1]
      assert_equal 0, kill_task(task_id)
      assert_equal E_INVARG, kill_task(task_id)
      assert_equal E_INVARG, kill_task(task_id)
      assert_equal [], queued_tasks()
    end
  end

  def test_that_resuming_a_suspended_exec_does_not_work
    run_test_as('wizard') do
      assert_equal [], queued_tasks()
      task_id = eval('fork go (0); exec({"sleep", "5"}); endfork; suspend(0); return go;')
      o = queued_tasks()
      assert_equal task_id, o[0]
      assert_equal 'executables/sleep', o[1]
      assert_equal E_INVARG, resume(task_id)
      # give the task time to finish
      sleep 10
      assert_equal [], queued_tasks()
    end
  end

  def test_that_a_suspended_exec_looks_like_a_suspended_task
    run_test_as('wizard') do
      assert_equal [], queued_tasks()
      task_id1 = eval('fork go (0); suspend(5); endfork; suspend(0); return go;')
      task_id2 = eval('fork go (0); exec({"sleep", "5"}); endfork; suspend(0); return go;')
      task_stack1 = task_stack(task_id1)
      task_stack2 = task_stack(task_id2)
      assert_equal task_stack1, task_stack2
      assert_not_equal task_id1, task_id2
    end
  end

  TIMES = 10
  DURATION = 5

  def test_that_a_lot_of_simultaneous_execs_work
    run_test_as('wizard') do
      assert_equal [], queued_tasks()
      task_ids = []
      TIMES.times { task_ids << eval('fork go (0); exec({"sleep", "' + DURATION.to_s + '"}); endfork; suspend(0); return go;') }
      qq = queued_tasks()
      assert_equal TIMES, qq.length
      assert_equal task_ids.sort, qq.map { |q| q[0] }.sort
      assert_equal ['executables/sleep'], qq.map { |q| q[1] }.uniq
      task_ids.each_with_index { |t, i| (i % 2 == 0) && kill_task(t) && task_ids -= [t] }
      qq = queued_tasks()
      assert_equal TIMES / 2, qq.length
      assert_equal task_ids.sort, qq.map { |q| q[0] }.sort
      assert_equal ['executables/sleep'], qq.map { |q| q[1] }.uniq
      sleep DURATION * 2
      assert_equal [], queued_tasks()
    end
  end

  def test_that_a_lot_of_randomly_timed_execs_work
    run_test_as('wizard') do
      assert_equal [], queued_tasks()
      task_ids = []
      TIMES.times { task_ids << eval('fork go (0); exec({"sleep", "' + (rand(DURATION / 2) + DURATION / 2).to_s + '"}); endfork; suspend(0); return go;') }
      qq = queued_tasks()
      assert_equal TIMES, qq.length
      assert_equal task_ids.sort, qq.map { |q| q[0] }.sort
      assert_equal ['executables/sleep'], qq.map { |q| q[1] }.uniq
      task_ids.each_with_index { |t, i| (i % 2 == 0) && kill_task(t) && task_ids -= [t] }
      qq = queued_tasks()
      assert_equal TIMES / 2, qq.length
      assert_equal task_ids.sort, qq.map { |q| q[0] }.sort
      assert_equal ['executables/sleep'], qq.map { |q| q[1] }.uniq
      sleep DURATION * 2
      assert_equal [], queued_tasks()
    end
  end

  def test_that_a_lot_of_fast_execs_work
    run_test_as('wizard') do
      assert_equal [], queued_tasks()
      task_ids = []
      TIMES.times { task_ids << eval('fork go (0); exec({"true"}); endfork; suspend(0); return go;') }
      sleep 1
      assert_equal [], queued_tasks()
    end
  end

  def test_that_a_variety_of_fuzzy_inputs_do_not_break_exec
    run_test_as('wizard') do
      with_mutating_binary_string("~A7~CED~D2L~16a~01UZ2~BC~B0)~EC~02~86v~CD~9B~05~E66~F3.vx<~F0~D1E@~C7~DA~F3~C7~C0C~1E~D2~D0~03]!~F7~0C~C9~19~F0~82gv~E4:~02~F0~BE") do |g|
        TIMES.times do
          s = g.next
          server_log s
          r = exec(['echo'], s)
          assert r == E_INVARG || r.class == Array
        end
      end
    end
  end

end
