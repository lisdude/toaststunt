require 'test_helper'

class TestThingsThatUsedToCrashTheServer < Test::Unit::TestCase

  def setup
    run_test_as('wizard') do
      evaluate('add_property($server_options, "max_concat_catchable", 1, {player, "r"})')
      evaluate('load_server_options();')
    end
  end

  def teardown
    run_test_as('wizard') do
      evaluate('delete_property($server_options, "max_concat_catchable")')
      evaluate('load_server_options();')
    end
  end

  def test_that_building_a_long_nested_list_does_not_crash_the_server
    run_test_as('programmer') do
      assert_equal E_QUOTA, simplify(command('; x = {}; while (1); ticks_left() < 2000 || seconds_left() < 2 && suspend(0); x = {x, x, x, x, x, x, x, x, x, x}; endwhile;'))
    end
  end

  def test_that_building_a_long_nested_map_does_not_crash_the_server
    run_test_as('programmer') do
      assert_equal E_QUOTA, simplify(command('; x = []; while (1); ticks_left() < 2000 || seconds_left() < 2 && suspend(0); x[1] = {x, x, x, x, x, x, x, x, x, x}; endwhile;'))
    end
  end

end
