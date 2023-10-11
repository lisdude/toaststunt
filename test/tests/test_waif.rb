require 'test_helper'

class TestWaif < Test::Unit::TestCase

  def setup
    run_test_as('wizard') do
      command(%Q|; for t in (queued_tasks()); kill_task(t[1]); endfor;|)
    end
  end


  def test_that_waifs_are_invalid
    run_test_as('programmer') do
      assert_equal 0, simplify(command("; return valid($waif:new());"))
    end
  end

  def test_that_waifs_have_no_parents_or_children
    run_test_as('programmer') do
      assert_equal E_INVARG, simplify(command("; parents($waif:new());"))
      assert_equal E_INVARG, simplify(command("; children($waif:new());"))
    end
  end

  def test_that_waifs_cannot_be_players
    run_test_as('programmer') do
      assert_equal E_TYPE, simplify(command("; is_player($waif:new());"))
      assert_equal E_TYPE, simplify(command("; set_player_flag($waif:new(), 1);"))
    end
  end

  def test_that_the_owner_of_a_waif_is_the_creator
    run_test_as('programmer') do
      assert_equal player, simplify(command("; return $waif:new().owner;"))
    end
  end

  def test_that_nobody_can_change_a_waif_owner
    run_test_as('programmer') do
      assert_equal E_PERM, simplify(command("; a = $waif:new(); a.owner = a.owner;"))
    end
    run_test_as('wizard') do
      assert_equal E_PERM, simplify(command("; a = $waif:new(); a.owner = $nothing; return a.owner;"))
    end
  end

  def test_that_setting_the_wizard_flag_on_a_waif_is_illegal
    run_test_as('programmer') do
      assert_equal E_PERM, simplify(command("; $waif:new().wizard = 0;"))
      assert_equal E_PERM, simplify(command("; $waif:new().wizard = 1;"))
    end
    run_test_as('wizard') do
      assert_equal E_PERM, simplify(command("; $waif:new().wizard = 0;"))
      assert_equal E_PERM, simplify(command("; $waif:new().wizard = 1;"))
    end
  end

  def test_that_setting_the_programmer_flag_on_a_waif_is_illegal
    run_test_as('programmer') do
      assert_equal E_PERM, simplify(command("; $waif:new().programmer = 0;"))
      assert_equal E_PERM, simplify(command("; $waif:new().programmer = 1;"))
    end
    run_test_as('wizard') do
      assert_equal E_PERM, simplify(command("; $waif:new().programmer = 0;"))
      assert_equal E_PERM, simplify(command("; $waif:new().programmer = 1;"))
    end
  end

  def test_that_defined_properties_work_on_waifs_and_waif_classes
    run_test_as('programmer') do
      x = create(:waif)
      add_property(x, ':x', 123, ['player', ''])

      y = create(x)
      add_property(y, ':y', 'abc', ['player', ''])

      z = create(y)
      add_property(z, ':z', [1], ['player', ''])

      q = create(z)
      add_property(q, 'q', 'wxyz', ['player', ''])

      assert_equal 123, simplify(command("; a = #{z}:new(); return a.x;"))
      assert_equal 'abc', simplify(command("; a = #{z}:new(); return a.y;"))
      assert_equal 1, simplify(command("; a = #{z}:new(); return a.z;"))
      assert_equal E_PROPNF, simplify(command("; a = #{q}:new(); return a.q;"))
      assert_equal 'wxyz', simplify(command("; a = #{q}:new(); return a.class.q;"))
    end
  end

  def test_that_verb_calls_work_on_waifs_and_waif_classes
    run_test_as('programmer') do
      x = create(:waif)
      add_verb(x, ['player', 'xd', ':x'], ['this', 'none', 'this'])
      set_verb_code(x, ':x') do |vc|
        vc << %Q|return 123;|
      end

      y = create(x)
      add_verb(y, ['player', 'xd', ':y'], ['this', 'none', 'this'])
      set_verb_code(y, ':y') do |vc|
        vc << %Q|return "abc";|
      end

      z = create(y)
      add_verb(z, ['player', 'xd', ':z'], ['this', 'none', 'this'])
      set_verb_code(z, ':z') do |vc|
        vc << %Q|return [1 -> 1];|
      end

      q = create(z)
      add_verb(q, ['player', 'xd', 'q'], ['this', 'none', 'this'])
      set_verb_code(q, 'q') do |vc|
        vc << %Q|return 1;|
      end

      assert_equal 123, simplify(command("; a = #{z}:new(); return a:x();"))
      assert_equal 'abc', simplify(command("; a = #{z}:new(); return a:y();"))
      assert_equal({1 => 1}, simplify(command("; a = #{z}:new(); return a:z();")))
      assert_equal(E_VERBNF, simplify(command("; a = #{q}:new(); return a:q();")))
      assert_equal(1, simplify(command("; a = #{q}:new(); return a.class:q();")))
    end
  end

  def test_that_losing_all_references_to_a_waif_calls_recycle
    run_test_as('programmer') do
      a = create(:waif)
      add_property(a, 'recycle_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', ':recycle'], ['this', 'none', 'this'])
      set_verb_code(a, ':recycle') do |vc|
        vc << %Q<typeof(this) == WAIF || raise(E_INVARG);>
        vc << %Q<#{a}.recycle_called = #{a}.recycle_called + 1;>
      end
      assert_equal 0, get(a, 'recycle_called')
      simplify(command("; #{a}:new();"))
      assert_equal 1, get(a, 'recycle_called')
    end
  end

  def test_that_losing_all_references_to_a_waif_calls_recycle_once
    run_test_as('programmer') do
      a = create(:waif)
      add_property(a, 'reference', 0, [player, ''])
      add_property(a, 'recycle_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', ':recycle'], ['this', 'none', 'this'])
      set_verb_code(a, ':recycle') do |vc|
        vc << %Q<typeof(this) == WAIF || raise(E_INVARG);>
        vc << %Q<#{a}.recycle_called = #{a}.recycle_called + 1;>
        vc << %Q<#{a}.reference = this;>
      end
      assert_equal 0, get(a, 'recycle_called')
      simplify(command("; #{a}:new();"))
      assert_equal 1, get(a, 'recycle_called')
      simplify(command("; #{a}.reference = 0;"))
      assert_equal 1, get(a, 'recycle_called')
    end
  end

  def test_that_waifs_cant_reference_each_other
      run_test_as('programmer') do
          a = create(:waif)
          add_property(a, ':other_waif', 0, [player, ''])
          assert_equal(E_RECMOVE, simplify(command("; a = #{a}:new(); b = #{a}:new(); a.other_waif = b; b.other_waif = a;")))
      end
  end

  def test_that_recycling_a_parent_effectively_invalidates_a_waif
    run_test_as('programmer') do
      o = create(:waif)
      a = create(:waif)
      add_verb(o, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(o, 'go') do |vc|
        vc << %Q|a = #{a}:new();|
        vc << %Q|recycle(#{a});|
        vc << %Q|return a.class == #-1;|
      end
      assert_equal 1, call(o, 'go')
    end
  end

  def test_that_chparenting_a_waif_inherits_new_properties
      run_test_as('programmer') do
          a = create(:waif)
          b = create(:waif)
          c = create(:waif)
          add_property(player, 'waifs', {}, [player, ''])
          add_property(a, ':a', 0, [player, ''])
          add_property(b, ':b', 0, [player, ''])
          simplify(command(%Q|; chparent(#{c}, #{a}); player.waifs["a"] = #{c}:new(); return "player.waifs[\\\"a\\\"]";|))
          assert_equal(0, simplify(command(%Q|; waif = player.waifs["a"]; return waif.a;|)))
          assert_equal(E_PROPNF, simplify(command(%Q|; waif = player.waifs["a"]; return waif.b;|)))
          simplify(command(%Q|; chparent(#{c}, #{b}); |))
          assert_equal(E_PROPNF, simplify(command(%Q|; waif = player.waifs["a"]; return waif.a;|)))
          assert_equal(0, simplify(command(%Q|; waif = player.waifs["a"]; return waif.b;|)))
      end
  end

  def test_that_anon_cant_be_waif_parent
      run_test_as('programmer') do
          a = create(:object)
          add_verb(a, ['player', 'xd', 'new'], ['this', 'none', 'this'])
          set_verb_code(a, 'new') do |vc|
              vc << %Q|return new_waif();|
          end
          assert_equal(E_INVARG, simplify(command("; a = create(#{a}, 1); return a:new();")))
      end
  end

  def test_that_a_long_chain_of_waifs_doesnt_leak
    run_test_as('programmer') do
      a = create(:waif)
      simplify(command(%Q|; add_property(#{a}, ":next", #{a}, {player, ""}); |))
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      add_verb(a, ['player', 'xd', 'gc'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        lines = <<-EOF
          r = o = #{a}:new();
          for i in [1..100];
              n = #{a}:new();
              o.next = n;
              o = n;
          endfor;
        EOF
        lines.split("\n").each do |line|
          vc << line
        end
      end
      set_verb_code(a, 'gc') do |vc|
        lines = <<-EOF
        for x in [1..100]
        suspend(0);
        endfor
        EOF
        lines.split("\n").each do |line|
          vc << line
        end
      end
      call(a, 'go')
      call(a, 'gc')
      assert_equal({"pending_recycle" => 0, "total" => 0}, simplify(command(";; return waif_stats();")))
    end
  end

  def test_that_callers_returns_valid_waifs_for_wizards
    run_test_as('programmer') do
        o = create(:waif)
      add_verb(o, ['player', 'xd', ':a'], ['this', 'none', 'this'])
      set_verb_code(o, ':a') do |vc|
        vc << %Q|return callers();|
      end
      add_verb(o, ['player', 'xd', ':b'], ['this', 'none', 'this'])
      set_verb_code(o, ':b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', ':c'], ['this', 'none', 'this'])
      set_verb_code(o, ':c') do |vc|
        vc << %Q|c = this:b();|
        vc << %Q|return {{c[1][2], typeof(c[1][1]) == WAIF, c[1][4] == #{o}}, {c[2][2], typeof(c[2][1]) == WAIF, c[2][4] == #{o}}};|
      end
      add_property(player, 'stash', {}, [player, ''])

      w = simplify(command(%Q|; player.stash["o"] = #{o}:new(); return "player.stash[\\\"o\\\"]";|))

      assert_equal [[":b", 1, 1], [":c", 1, 1]], call(w, 'c')
    end
  end

  def test_that_task_stack_returns_valid_waifs_for_owners
    run_test_as('programmer') do
        o = create(:waif)
      add_property(player, 'stash', {}, [player, ''])

      add_verb(o, ['player', 'xd', ':a'], ['this', 'none', 'this'])
      set_verb_code(o, ':a') do |vc|
        vc << %Q|suspend();|
      end
      add_verb(o, ['player', 'xd', ':b'], ['this', 'none', 'this'])
      set_verb_code(o, ':b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', ':c'], ['this', 'none', 'this'])
      set_verb_code(o, ':c') do |vc|
        vc << %Q|fork t (0)|
        vc << %Q|c = this:b();|
        vc << %Q|endfork|
        vc << %Q|suspend(0);|
        vc << %Q|t = task_stack(t);|
        vc << %Q|return {{t[1][2], typeof(t[1][1]) == WAIF, t[1][4] == #{o}}, {t[2][2], typeof(t[2][1]) == WAIF, t[2][4] == #{o}}, {t[3][2], typeof(t[3][1]) == WAIF, t[3][4] == #{o}}};|
      end

      o = simplify(command(%Q|; player.stash["o"] = #{o}:new(); return "player.stash[\\\"o\\\"]";|))
      assert_equal [[":a", 1, 1], [":b", 1, 1], [":c", 1, 1]], call(o, 'c')
    end
  end

  def test_that_queued_tasks_returns_valid_waifs_for_programmers
    run_test_as('programmer') do
        o = create(:waif)
      add_property(player, 'stash', {}, [player, ''])

      add_verb(o, ['player', 'xd', ':a'], ['this', 'none', 'this'])
      set_verb_code(o, ':a') do |vc|
        vc << %Q|suspend();|
      end
      add_verb(o, ['player', 'xd', ':b'], ['this', 'none', 'this'])
      set_verb_code(o, ':b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', ':c'], ['this', 'none', 'this'])
      set_verb_code(o, ':c') do |vc|
        vc << %Q|for t in ({0, 100})|
        vc << %Q|fork (t)|
        vc << %Q|c = this:b();|
        vc << %Q|endfork|
        vc << %Q|endfor|
        vc << %Q|suspend(0);|
        vc << %Q|q = queued_tasks();|
        vc << %Q|return {length(q), {q[1][7], q[1][6] == #{o}, typeof(q[1][9]) == WAIF}, {q[2][7], q[2][6] == #{o}, typeof(q[2][9]) == WAIF}};|
      end

      o = simplify(command(%Q|; player.stash["o"] = #{o}:new(); return "player.stash[\\\"o\\\"]";|))
      assert_equal [2, [":c", 1, 1], [":a", 1, 1]], call(o, 'c')
    end
  end


end
