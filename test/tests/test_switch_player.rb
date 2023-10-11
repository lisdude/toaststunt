require 'test_helper'

class TestSwitchPlayer < Test::Unit::TestCase

  def test_that_switch_player_does_not_work_for_non_wizards
    run_test_as('programmer') do
      assert_equal E_PERM, simplify(switch_player(player, player))
    end
  end

  def test_that_switch_player_switches_player
    run_test_as('wizard') do
      old_player = player
      new_player = make_new_player
      switch_player(player, new_player)
      assert_not_equal old_player, simplify(command %Q|;return player;|)
      assert_equal new_player, simplify(command %Q|;return player;|)
    end
  end

  def test_that_switching_back_and_forth_works
    run_test_as('wizard') do
      old_player = player
      new_player = make_new_player
      switch_player(player, new_player)
      assert_not_equal old_player, simplify(command %Q|;return player;|)
      assert_equal new_player, simplify(command %Q|;return player;|)
      switch_player(new_player, player)
      assert_equal old_player, simplify(command %Q|;return player;|)
      assert_not_equal new_player, simplify(command %Q|;return player;|)
      switch_player(player, new_player)
      assert_not_equal old_player, simplify(command %Q|;return player;|)
      assert_equal new_player, simplify(command %Q|;return player;|)
    end
  end

  private

  def make_new_player
    simplify command %Q|;o = create($nothing); move(o, player.location); set_player_flag(o, 1); o.programmer = 1; o.wizard = 1; return o;|
  end

end
