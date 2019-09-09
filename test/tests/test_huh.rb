require 'test_helper'

class TestHuh < Test::Unit::TestCase

  def teardown
    run_test_as('wizard') do
      evaluate('delete_property($server_options, "player_huh")')
    end
  end

  def test_that_huh_works_on_players_when_server_option_player_huh_is_true
    run_test_with_prefix_and_suffix_as('programmer') do
      add_verb(player, [player, 'xd', 'huh'], ['this', 'none', 'this'])
      set_verb_code(player, 'huh') do |vc|
        vc << %Q|notify(player, "Huh?");|
      end
      move(player, :nothing)
      assert_not_equal 'huh', command(%Q|zip|)
    end
    run_test_with_prefix_and_suffix_as('wizard') do
      evaluate('add_property($server_options, "player_huh", 1, {player, "r"})')
      add_verb(player, [player, 'xd', 'huh'], ['this', 'none', 'this'])
      set_verb_code(player, 'huh') do |vc|
        vc << %Q|notify(player, "Huh?");|
      end
      move(player, :nothing)
      assert_equal 'Huh?', command(%Q|zap|)
    end
    run_test_with_prefix_and_suffix_as('wizard') do
      evaluate('$server_options.player_huh = 0')
      add_verb(player, [player, 'xd', 'huh'], ['this', 'none', 'this'])
      set_verb_code(player, 'huh') do |vc|
        vc << %Q|notify(player, "Huh?");|
      end
      move(player, :nothing)
      assert_not_equal 'huh', command(%Q|zop|)
    end
  end

  def test_that_huh_does_not_work_on_locations_when_server_option_player_huh_is_true
    run_test_with_prefix_and_suffix_as('programmer') do
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'accept'], ['this', 'none', 'this'])
      set_verb_code(o, 'accept') do |vc|
        vc << %Q|return 1;|
      end
      add_verb(o, [player, 'xd', 'huh'], ['this', 'none', 'this'])
      set_verb_code(o, 'huh') do |vc|
        vc << %Q|notify(player, "Huh?");|
      end
      move(player, o)
      assert_equal 'Huh?', command(%Q|zip|)
    end
    run_test_with_prefix_and_suffix_as('wizard') do
      evaluate('add_property($server_options, "player_huh", 1, {player, "r"})')
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'accept'], ['this', 'none', 'this'])
      set_verb_code(o, 'accept') do |vc|
        vc << %Q|return 1;|
      end
      add_verb(o, [player, 'xd', 'huh'], ['this', 'none', 'this'])
      set_verb_code(o, 'huh') do |vc|
        vc << %Q|notify(player, "Huh?");|
      end
      move(player, o)
      assert_not_equal 'Huh?', command(%Q|zap|)
    end
    run_test_with_prefix_and_suffix_as('wizard') do
      evaluate('$server_options.player_huh = 0')
      o = create(:nothing)
      add_verb(o, [player, 'xd', 'accept'], ['this', 'none', 'this'])
      set_verb_code(o, 'accept') do |vc|
        vc << %Q|return 1;|
      end
      add_verb(o, [player, 'xd', 'huh'], ['this', 'none', 'this'])
      set_verb_code(o, 'huh') do |vc|
        vc << %Q|notify(player, "Huh?");|
      end
      move(player, o)
      assert_equal 'Huh?', command(%Q|zop|)
    end
  end

end
