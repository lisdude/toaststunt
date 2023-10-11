require 'test_helper'

class TestAnonymous < Test::Unit::TestCase

  def setup
    run_test_as('wizard') do
      command(%Q|; for t in (queued_tasks()); kill_task(t[1]); endfor;|)
    end
  end


  def test_that_anonymous_objects_created_from_nothing_are_valid
    run_test_as('programmer') do
      assert_equal 1, simplify(command("; return valid(create($nothing, 1));"))
    end
  end

  def test_that_anonymous_objects_created_from_an_empty_list_are_valid
    run_test_as('programmer') do
      assert_equal 1, simplify(command("; return valid(create({}, 1));"))
    end
  end

  def test_that_anonymous_objects_have_parents
    run_test_as('programmer') do
      x = create(:nothing)
      a = create(x)
      b = create(x)
      assert_equal a, simplify(command("; return parent(create({#{a}, #{b}}, 1));"))
      assert_equal [a, b], simplify(command("; return parents(create({#{a}, #{b}}, 1));"))
    end
  end

  def test_that_anonymous_objects_do_not_have_children
    run_test_as('programmer') do
      assert_equal [], simplify(command("; return children(create($anonymous, 1));"))
    end
  end

  def test_that_anonymous_objects_cannot_be_players
    run_test_as('programmer') do
      assert_equal E_TYPE, simplify(command("; is_player(create($anonymous, 1));"))
      assert_equal E_TYPE, simplify(command("; set_player_flag(create($anonymous, 1), 1);"))
    end
  end

  def test_that_the_owner_of_an_anonymous_object_is_the_creator
    run_test_as('programmer') do
      assert_equal player, simplify(command("; return create($anonymous, 1).owner;"))
    end
  end

  def test_that_a_wizard_can_change_the_owner_of_an_anonymous_object
    run_test_as('programmer') do
      assert_equal E_PERM, simplify(command("; a = create($anonymous, 1); a.owner = a.owner;"))
    end
    run_test_as('wizard') do
      assert_equal NOTHING, simplify(command("; a = create($anonymous, 1); a.owner = $nothing; return a.owner;"))
    end
  end

  def test_that_an_anonymous_object_has_a_name
    run_test_as('programmer') do
      assert_equal 'Foo Bar', simplify(command("; a = create($anonymous, 1); a.name = \"Foo Bar\"; return a.name;"))
    end
  end

  def test_that_setting_the_wizard_flag_on_an_anonymous_object_is_illegal
    run_test_as('programmer') do
      assert_equal E_PERM, simplify(command("; create($anonymous, 1).wizard = 0;"))
      assert_equal E_PERM, simplify(command("; create($anonymous, 1).wizard = 1;"))
    end
    run_test_as('wizard') do
      assert_equal E_INVARG, simplify(command("; create($anonymous, 1).wizard = 0;"))
      assert_equal E_INVARG, simplify(command("; create($anonymous, 1).wizard = 1;"))
    end
  end

  def test_that_setting_the_programmer_flag_on_an_anonymous_object_is_illegal
    run_test_as('programmer') do
      assert_equal E_PERM, simplify(command("; create($anonymous, 1).programmer = 0;"))
      assert_equal E_PERM, simplify(command("; create($anonymous, 1).programmer = 1;"))
    end
    run_test_as('wizard') do
      assert_equal E_INVARG, simplify(command("; create($anonymous, 1).programmer = 0;"))
      assert_equal E_INVARG, simplify(command("; create($anonymous, 1).programmer = 1;"))
    end
  end

  def test_that_defined_properties_work_on_anonymous_objects
    run_test_as('programmer') do
      x = create(:nothing)
      add_property(x, 'x', 123, ['player', ''])

      y = create(x)
      add_property(y, 'y', 'abc', ['player', ''])

      z = create(y)
      add_property(z, 'z', [1], ['player', ''])

      assert_equal 123, simplify(command("; a = create(#{z}, 1); return a.x;"))
      assert_equal 'abc', simplify(command("; a = create(#{z}, 1); return a.y;"))
      assert_equal 1, simplify(command("; a = create(#{z}, 1); return a.z;"))
    end
  end

  def test_that_verb_calls_work_on_anonymous_objects
    run_test_as('programmer') do
      x = create(:nothing)
      add_verb(x, ['player', 'xd', 'x'], ['this', 'none', 'this'])
      set_verb_code(x, 'x') do |vc|
        vc << %Q|return 123;|
      end

      y = create(x)
      add_verb(y, ['player', 'xd', 'y'], ['this', 'none', 'this'])
      set_verb_code(y, 'y') do |vc|
        vc << %Q|return "abc";|
      end

      z = create(y)
      add_verb(z, ['player', 'xd', 'z'], ['this', 'none', 'this'])
      set_verb_code(z, 'z') do |vc|
        vc << %Q|return [1 -> 1];|
      end

      assert_equal 123, simplify(command("; a = create(#{z}, 1); return a:x();"))
      assert_equal 'abc', simplify(command("; a = create(#{z}, 1); return a:y();"))
      assert_equal({1 => 1}, simplify(command("; a = create(#{z}, 1); return a:z();")))
    end
  end

  def test_that_valid_returns_true_if_an_anonymous_object_is_valid
    run_test_as('programmer') do
      assert_equal 1, simplify(command("; a = create($anonymous, 1); return valid(a);"))
    end
  end

  def test_that_recycle_invalidates_all_references_to_an_anonymous_object
    run_test_as('programmer') do
      assert_equal 0, simplify(command("; a = create($anonymous, 1); recycle(a); return valid(a);"))
      assert_equal 0, simplify(command("; a = create($anonymous, 1); b = a; recycle(a); return valid(b);"))
      assert_equal 0, simplify(command("; a = create($anonymous, 1); b = a; recycle(b); return valid(a);"))
    end
  end

  def test_that_losing_all_references_to_an_anonymous_object_calls_recycle
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'recycle_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'recycle'], ['this', 'none', 'this'])
      set_verb_code(a, 'recycle') do |vc|
        vc << %Q<typeof(this) == ANON || raise(E_INVARG);>
        vc << %Q<#{a}.recycle_called = #{a}.recycle_called + 1;>
      end
      assert_equal 0, get(a, 'recycle_called')
      simplify(command("; create(#{a}, 1);"))
      assert_equal 1, get(a, 'recycle_called')
    end
  end

  def test_that_losing_all_references_to_an_anonymous_object_calls_recycle_once
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'reference', 0, [player, ''])
      add_property(a, 'recycle_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'recycle'], ['this', 'none', 'this'])
      set_verb_code(a, 'recycle') do |vc|
        vc << %Q<typeof(this) == ANON || raise(E_INVARG);>
        vc << %Q<#{a}.recycle_called = #{a}.recycle_called + 1;>
        vc << %Q<#{a}.reference = this;>
      end
      assert_equal 0, get(a, 'recycle_called')
      simplify(command("; create(#{a}, 1);"))
      assert_equal 1, get(a, 'recycle_called')
      simplify(command("; #{a}.reference = 0;"))
      assert_equal 1, get(a, 'recycle_called')
    end
  end

  def test_that_an_anonymous_object_is_invalid_after_it_is_recycled
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'reference', 0, [player, ''])
      add_property(a, 'recycle_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'recycle'], ['this', 'none', 'this'])
      set_verb_code(a, 'recycle') do |vc|
        vc << %Q<typeof(this) == ANON || raise(E_INVARG);>
        vc << %Q<#{a}.recycle_called = #{a}.recycle_called + 1;>
        vc << %Q<#{a}.reference = this;>
      end
      assert_equal 0, get(a, 'recycle_called')
      simplify(command("; create(#{a}, 1);"))
      assert_equal 1, get(a, 'recycle_called')
      assert_equal 0, simplify(command("; return valid(#{a}.reference);"))
      assert_equal E_INVARG, simplify(command("; recycle(#{a}.reference);"))
      assert_equal E_INVIND, simplify(command("; #{a}.reference.name;"))
    end
  end

  def test_that_recycling_a_parent_invalidates_an_anonymous_object
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(o, 'go') do |vc|
        vc << %Q|o = create({});|
        vc << %Q|a = create(o, 1);|
        vc << %Q|recycle(o);|
        vc << %Q|return valid(a);|
      end
      assert_equal 0, call(o, 'go')
    end
  end

  def test_that_chparents_on_a_parent_invalidates_an_anonymous_object
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(o, 'go') do |vc|
        vc << %Q|a = create({});|
        vc << %Q|b = create({});|
        vc << %Q|c = create({a, b});|
        vc << %Q|d = create(c);|
        vc << %Q|m = create(a, 1);|
        vc << %Q|n = create(d, 1);|
        vc << %Q|chparents(c, {b, a});|
        vc << %Q|return {valid(m), valid(n)};|
      end
      assert_equal [1, 0], call(o, 'go')
    end
  end

  def test_that_adding_a_property_to_a_parent_invalidates_an_anonymous_object
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(o, 'go') do |vc|
        vc << %Q|o = create({});|
        vc << %Q|p = create({o});|
        vc << %Q|a = create(o, 1);|
        vc << %Q|b = create(p, 1);|
        vc << %Q|add_property(p, "xyz", 1, {player, ""});|
        vc << %Q|return {valid(a), valid(b)};|
      end
      assert_equal [1, 0], call(o, 'go')
    end
  end

  def test_that_deleting_a_property_from_a_parent_invalidates_an_anonymous_object
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(o, 'go') do |vc|
        vc << %Q|o = create({});|
        vc << %Q|p = create({o});|
        vc << %Q|add_property(p, "xyz", 1, {player, ""});|
        vc << %Q|a = create(o, 1);|
        vc << %Q|b = create(p, 1);|
        vc << %Q|delete_property(p, "xyz");|
        vc << %Q|return {valid(a), valid(b)};|
      end
      assert_equal [1, 0], call(o, 'go')
    end
  end

  def test_that_renumbering_a_parent_invalidates_an_anonymous_object
    run_test_as('wizard') do
      o = create(:nothing)
      add_verb(o, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(o, 'go') do |vc|
        vc << %Q|recycle(create($nothing));|
        vc << %Q|recycle(create($nothing));|
        vc << %Q|a = create($nothing);|
        vc << %Q|add_property(a, "xyz", 1, {player, ""});|
        vc << %Q|m = create(a, 1);|
        vc << %Q|renumber(a);|
        vc << %Q|return m.xyz;|
      end
      assert_equal E_INVIND, call(o, 'go')
    end
  end

  def test_that_replacing_a_parent_does_not_corrupt_an_anonymous_object
    run_test_as('wizard') do
      o = create(:nothing)
      add_verb(o, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(o, 'go') do |vc|
        vc << %Q|a = create($nothing);|
        vc << %Q|add_property(a, "xyz", 1, {player, ""});|
        vc << %Q|m = create(a, 1);|
        vc << %Q|recycle(a);|
        vc << %Q|reset_max_object();|
        vc << %Q|b = create($nothing);|
        vc << %Q|add_property(b, "xyz", 2, {player, ""});|
        vc << %Q|return m.xyz;|
      end
      assert_equal E_INVIND, call(o, 'go')
    end
  end

  def test_that_getting_a_property_on_an_invalid_anonymous_object_fails
    run_test_as('programmer') do
      assert_equal E_INVIND, simplify(command('; a = create($anonymous, 1); recycle(a); a.name;'))
    end
  end

  def test_that_setting_a_property_on_an_invalid_anonymous_object_fails
    run_test_as('programmer') do
      assert_equal E_INVIND, simplify(command('; a = create($anonymous, 1); recycle(a); a.name = "Hello";'))
    end
  end

  def test_that_a_verb_call_on_an_invalid_anonymous_object_fails
    run_test_as('programmer') do
      assert_equal E_INVIND, simplify(command('; a = create($anonymous, 1); recycle(a); a:foo();'))
    end
  end

  def test_that_recycling_an_invalid_anonymous_object_doesnt_crash_the_server
    run_test_as('programmer') do
      simplify(command(%Q|; o = create($nothing); p = create(o, 1); add_property(o, "foo", 0, {player, ""});|))
    end
  end

  def test_that_a_long_chain_of_anonymous_objects_doesnt_leak
    run_test_as('programmer') do
      a = create(:object)
      simplify(command(%Q|; add_property(#{a}, "next", #{a}, {player, ""}); |))
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        lines = <<-EOF
          r = o = create($nothing, 1);
          for i in [1..100];
              n = create($nothing, 1);
              add_property(n, "next", o, {player, ""});
            o = n;
          endfor;
        EOF
        lines.split("\n").each do |line|
          vc << line
        end
      end
      call(a, 'go')
    end
  end

  if @@options['ownership_quota']
      def test_that_ownership_quota_is_not_changed_when_an_anonymous_object_becomes_invalid
          run_test_as('programmer') do
              o = create(:object)

              add_property(player, 'stash', {}, [player, ''])
              add_property(player, 'ownership_quota', 2, [player, ''])
              assert_equal 2, get(player, 'ownership_quota')

              a = simplify(command(%Q|; player.stash["a"] = create(#{o}, 1); return "player.stash[\\\"a\\\"]";|))
              assert_equal 1, get(player, 'ownership_quota')

              assert valid(a)
              chparent(o, :nothing)
              assert !valid(a)

              assert_equal 1, get(player, 'ownership_quota')
          end
      end

      def test_that_ownership_quota_is_changed_when_an_anonymous_object_loses_its_last_reference
          run_test_as('programmer') do
              o = create(:object)

              add_property(player, 'stash', {}, [player, ''])
              add_property(player, 'ownership_quota', 2, [player, ''])
              assert_equal 2, get(player, 'ownership_quota')

              a = simplify(command(%Q|; player.stash["a"] = create(#{o}, 1); return "player.stash[\\\"a\\\"]";|))
              assert_equal 1, get(player, 'ownership_quota')

              command(%Q|; #{a} = 0;|)

              assert_equal 2, get(player, 'ownership_quota')
          end
      end
  end

  def test_that_callers_returns_valid_anonymous_objects_for_wizards
    run_test_as('wizard') do
      add_property(player, 'stash', {}, [player, ''])

      o = simplify(command(%Q|; player.stash["o"] = create($anonymous, 1); return "player.stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|return callers();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|c = this:b();|
        vc << %Q|return {{c[1][2], valid(c[1][1]), valid(c[1][4])}, {c[2][2], valid(c[2][1]), valid(c[2][4])}};|
      end

      assert_equal [["b", 1, 1], ["c", 1, 1]], call(o, 'c')
    end
  end

  def test_that_callers_returns_valid_anonymous_objects_for_owners
    run_test_as('programmer') do
      add_property(player, 'stash', {}, [player, ''])

      o = simplify(command(%Q|; player.stash["o"] = create($anonymous, 1); return "player.stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|return callers();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|c = this:b();|
        vc << %Q|return {{c[1][2], valid(c[1][1]), valid(c[1][4])}, {c[2][2], valid(c[2][1]), valid(c[2][4])}};|
      end

      assert_equal [["b", 1, 1], ["c", 1, 1]], call(o, 'c')
    end
  end

  def test_that_callers_returns_invalid_anonymous_objects_for_everyone_else
    o = nil
    run_test_as('wizard') do
      add_property(player, 'stash', {}, [player, 'r'])

      o = simplify(command(%Q|; o = player.stash["o"] = create($anonymous, 1); o.r = o.w = 1; return tostr(player) + ".stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|set_task_perms(player);|
        vc << %Q|return callers();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
    end
    run_test_as('programmer') do
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|c = this:b();|
        vc << %Q|return {{c[1][2], valid(c[1][1]), valid(c[1][4])}, {c[2][2], valid(c[2][1]), valid(c[2][4])}};|
      end

      assert_equal [["b", 0, 0], ["c", 0, 0]], call(o, 'c')
    end
  end

  def test_that_task_stack_returns_valid_anonymous_objects_for_wizards
    run_test_as('wizard') do
      add_property(player, 'stash', {}, [player, ''])

      o = simplify(command(%Q|; player.stash["o"] = create($anonymous, 1); return "player.stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|suspend();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|fork t (0)|
        vc << %Q|c = this:b();|
        vc << %Q|endfork|
        vc << %Q|suspend(0);|
        vc << %Q|t = task_stack(t);|
        vc << %Q|return {{t[1][2], valid(t[1][1]), valid(t[1][4])}, {t[2][2], valid(t[2][1]), valid(t[2][4])}, {t[3][2], valid(t[3][1]), valid(t[3][4])}};|
      end

      assert_equal [["a", 1, 1], ["b", 1, 1], ["c", 1, 1]], call(o, 'c')
    end
  end

  def test_that_task_stack_returns_valid_anonymous_objects_for_owners
    run_test_as('programmer') do
      add_property(player, 'stash', {}, [player, ''])

      o = simplify(command(%Q|; player.stash["o"] = create($anonymous, 1); return "player.stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|suspend();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|fork t (0)|
        vc << %Q|c = this:b();|
        vc << %Q|endfork|
        vc << %Q|suspend(0);|
        vc << %Q|t = task_stack(t);|
        vc << %Q|return {{t[1][2], valid(t[1][1]), valid(t[1][4])}, {t[2][2], valid(t[2][1]), valid(t[2][4])}, {t[3][2], valid(t[3][1]), valid(t[3][4])}};|
      end

      assert_equal [["a", 1, 1], ["b", 1, 1], ["c", 1, 1]], call(o, 'c')
    end
  end

  def test_that_task_stack_returns_valid_anonymous_objects_for_everyone_else
    o = nil
    run_test_as('wizard') do
      add_property(player, 'stash', {}, [player, 'r'])

      o = simplify(command(%Q|; o = player.stash["o"] = create($anonymous, 1); o.r = o.w = 1; return tostr(player) + ".stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|set_task_perms(player);|
        vc << %Q|suspend();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
    end
    run_test_as('programmer') do
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|fork t (0)|
        vc << %Q|c = this:b();|
        vc << %Q|endfork|
        vc << %Q|suspend(0);|
        vc << %Q|t = task_stack(t);|
        vc << %Q|return {{t[1][2], valid(t[1][1]), valid(t[1][4])}, {t[2][2], valid(t[2][1]), valid(t[2][4])}, {t[3][2], valid(t[3][1]), valid(t[3][4])}};|
      end

      assert_equal [["a", 0, 0], ["b", 0, 0], ["c", 0, 0]], call(o, 'c')
    end
  end

  def test_that_queued_tasks_returns_valid_anonymous_objects_for_wizards
    run_test_as('wizard') do
      add_property(player, 'stash', {}, [player, ''])

      o = simplify(command(%Q|; player.stash["o"] = create($anonymous, 1); return "player.stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|suspend();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|for t in ({0, 100})|
        vc << %Q|fork (t)|
        vc << %Q|c = this:b();|
        vc << %Q|endfork|
        vc << %Q|endfor|
        vc << %Q|suspend(0);|
        vc << %Q|q = queued_tasks();|
        vc << %Q|return {length(q), {q[1][7], valid(q[1][6]), valid(q[1][9])}, {q[2][7], valid(q[2][6]), valid(q[2][9])}};|
      end

      assert_equal [2, ["c", 1, 1], ["a", 1, 1]], call(o, 'c')
    end
  end

  def test_that_queued_tasks_returns_valid_anonymous_objects_for_programmers
    run_test_as('programmer') do
      add_property(player, 'stash', {}, [player, ''])

      o = simplify(command(%Q|; player.stash["o"] = create($anonymous, 1); return "player.stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|suspend();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|for t in ({0, 100})|
        vc << %Q|fork (t)|
        vc << %Q|c = this:b();|
        vc << %Q|endfork|
        vc << %Q|endfor|
        vc << %Q|suspend(0);|
        vc << %Q|q = queued_tasks();|
        vc << %Q|return {length(q), {q[1][7], valid(q[1][6]), valid(q[1][9])}, {q[2][7], valid(q[2][6]), valid(q[2][9])}};|
      end

      assert_equal [2, ["c", 1, 1], ["a", 1, 1]], call(o, 'c')
    end
  end

  def test_that_queued_tasks_returns_invalid_anonymous_objects_for_everyone_else
    o = nil
    run_test_as('wizard') do
      add_property(player, 'stash', {}, [player, 'r'])

      o = simplify(command(%Q|; o = player.stash["o"] = create($anonymous, 1); o.r = o.w = 1; return tostr(player) + ".stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|suspend();|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
    end
    run_test_as('programmer') do
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|for t in ({0, 100})|
        vc << %Q|fork (t)|
        vc << %Q|c = this:b();|
        vc << %Q|endfork|
        vc << %Q|endfor|
        vc << %Q|suspend(0);|
        vc << %Q|q = queued_tasks();|
        vc << %Q|return {length(q), {q[1][7], valid(q[1][6]), valid(q[1][9])}};|
      end

      assert_equal [1, ["c", 0, 0]], call(o, 'c')
    end
  end

  def test_that_an_error_stack_trace_contains_invalid_anonymous_objects_for_everyone
    run_test_as('wizard') do
      add_property(player, 'stash', {}, [player, 'r'])

      o = simplify(command(%Q|; o = player.stash["o"] = create($anonymous, 1); o.r = o.w = 1; return tostr(player) + ".stash[\\\"o\\\"]";|))

      add_verb(o, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(o, 'a') do |vc|
        vc << %Q|1/0;|
      end
      add_verb(o, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(o, 'b') do |vc|
        vc << %Q|return this:a();|
      end
      add_verb(o, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(o, 'c') do |vc|
        vc << %Q|try;|
        vc << %Q|this:b();|
        vc << %Q|except ex (ANY);|
        vc << %Q|endtry|
        vc << %Q|return {{ex[4][1][2], valid(ex[4][1][1]), valid(ex[4][1][4])}, {ex[4][2][2], valid(ex[4][2][1]), valid(ex[4][2][4])}};|
      end

      assert_equal [["a", 0, 0], ["b", 0, 0]], call(o, 'c')
    end
  end

end
