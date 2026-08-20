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

  def test_that_recycling_a_parent_does_not_invalidate_an_anonymous_object
    run_test_as('programmer') do
      o = create(:nothing)
      add_verb(o, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(o, 'go') do |vc|
        vc << %Q|o = create({});|
        vc << %Q|a = create(o, 1);|
        vc << %Q|recycle(o);|
        vc << %Q|return valid(a);|
      end
      assert_equal 1, call(o, 'go')
    end
  end

  def test_that_chparents_on_a_parent_does_not_invalidate_an_anonymous_object
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
      assert_equal [1, 1], call(o, 'go')
    end
  end

  def test_that_adding_a_property_to_a_parent_does_not_invalidate_an_anonymous_object
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
      assert_equal [1, 1], call(o, 'go')
    end
  end

  def test_that_deleting_a_property_from_a_parent_does_not_invalidate_an_anonymous_object
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
      assert_equal [1, 1], call(o, 'go')
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

end
