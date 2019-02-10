require 'test_helper'

class Testdestroy < Test::Unit::TestCase

  def test_that_the_first_argument_is_required
    run_test_as('programmer') do
      assert_equal E_ARGS, simplify(command("; destroy();"))
    end
  end

  def test_that_the_first_argument_can_be_an_object_or_an_anonymous_object
    run_test_as('wizard') do
      assert_equal E_TYPE, simplify(command("; destroy(1);"))
      assert_equal E_TYPE, simplify(command("; destroy({[]});"))
      assert_not_equal E_TYPE, simplify(command("; destroy($nothing);"))
      assert_not_equal E_TYPE, simplify(command("; destroy(create($object, 0));"))
      assert_not_equal E_TYPE, simplify(command("; destroy(create($anonymous, 1));"))
    end
  end

  def test_that_the_first_argument_must_be_valid
    run_test_as('programmer') do
      assert_equal E_INVARG, simplify(command("; x = create($object, 0); destroy(x); destroy(x);"))
      assert_equal E_INVARG, simplify(command("; x = create($anonymous, 1); destroy(x); destroy(x);"))
    end
  end

  def test_a_variety_of_argument_errors
    run_test_as('wizard') do
      assert_equal E_TYPE, simplify(command("; destroy(1.0);"))
      assert_equal E_TYPE, simplify(command("; destroy([]);"))
      assert_equal E_TYPE, simplify(command("; destroy({});"))
      assert_equal E_TYPE, simplify(command('; destroy("foobar");'))
    end
  end

  def test_that_a_wizard_can_destroy_anything
    m = nil
    n = nil
    run_test_as('wizard') do
      m = create(:object, 0)
      n = create(:object, 0)
      set(m, 'w', 0)
      set(n, 'w', 1)
      add_property(n, 'x', 0, ['player', 'r'])
      add_property(n, 'y', 0, ['player', 'r'])
      command("; #{n}.x = create($object, 0); #{n}.x.w = 0;")
      command("; #{n}.y = create($anonymous, 1); #{n}.y.w = 1;")
    end
    run_test_as('wizard') do
      add_property(n, 'i', 0, ['player', 'r'])
      add_property(n, 'j', 0, ['player', 'r'])
      command("; #{n}.i = create($object, 0);")
      command("; #{n}.j = create($anonymous, 1);")
      assert_not_equal E_PERM, simplify(command("; destroy(#{n}.i);"))
      assert_not_equal E_PERM, simplify(command("; destroy(#{n}.j);"))
      assert_not_equal E_PERM, simplify(command("; destroy(#{n}.x);"))
      assert_not_equal E_PERM, simplify(command("; destroy(#{n}.y);"))
      assert_not_equal E_PERM, simplify(command("; destroy(#{m});"))
      assert_not_equal E_PERM, simplify(command("; destroy(#{n});"))
    end
  end

  def test_that_a_programmer_can_only_destroy_things_it_controls
    m = nil
    n = nil
    run_test_as('programmer') do
      m = create(:object, 0)
      n = create(:object, 0)
      set(m, 'w', 0)
      set(n, 'w', 1)
      add_property(n, 'x', 0, ['player', 'r'])
      add_property(n, 'y', 0, ['player', 'r'])
      command("; #{n}.x = create($object, 0); #{n}.x.w = 0;")
      command("; #{n}.y = create($anonymous, 1); #{n}.y.w = 1;")
    end
    run_test_as('programmer') do
      add_property(n, 'i', 0, ['player', 'r'])
      add_property(n, 'j', 0, ['player', 'r'])
      command("; #{n}.i = create($object, 0);")
      command("; #{n}.j = create($anonymous, 1);")
      assert_not_equal E_PERM, simplify(command("; destroy(#{n}.i);"))
      assert_not_equal E_PERM, simplify(command("; destroy(#{n}.j);"))
      assert_equal E_PERM, simplify(command("; destroy(#{n}.x);"))
      assert_equal E_PERM, simplify(command("; destroy(#{n}.y);"))
      assert_equal E_PERM, simplify(command("; destroy(#{m});"))
      assert_equal E_PERM, simplify(command("; destroy(#{n});"))
    end
  end


  def test_that_destroying_an_object_calls_pre_destroy
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'pre_destroy_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<typeof(this) == OBJ || raise(E_INVARG);>
        vc << %Q<#{a}.pre_destroy_called = #{a}.pre_destroy_called + 1;>
      end
      assert_equal 0, get(a, 'pre_destroy_called')
      simplify(command("; destroy(create(#{a}, 0));"))
      assert_equal 1, get(a, 'pre_destroy_called')
    end
  end

  def test_that_destroying_an_anonymous_object_calls_pre_destroy
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'pre_destroy_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<typeof(this) == ANON || raise(E_INVARG);>
        vc << %Q<#{a}.pre_destroy_called = #{a}.pre_destroy_called + 1;>
      end
      assert_equal 0, get(a, 'pre_destroy_called')
      simplify(command("; destroy(create(#{a}, 1));"))
      assert_equal 1, get(a, 'pre_destroy_called')
    end
  end

  def test_that_calling_pre_destroy_when_recycling_an_object_fails
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'pre_destroy_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<typeof(this) == OBJ || raise(E_INVARG);>
        vc << %Q<#{a}.pre_destroy_called = #{a}.pre_destroy_called + 1;>
        vc << %Q<destroy(this);>
      end
      assert_equal 0, get(a, 'pre_destroy_called')
      simplify(command("; destroy(create(#{a}, 0));"))
      assert_equal 1, get(a, 'pre_destroy_called')
    end
  end

  def test_that_calling_pre_destroy_when_recycling_an_anonymous_object_fails
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'pre_destroy_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<typeof(this) == ANON || raise(E_INVARG);>
        vc << %Q<#{a}.pre_destroy_called = #{a}.pre_destroy_called + 1;>
        vc << %Q<destroy(this);>
      end
      assert_equal 0, get(a, 'pre_destroy_called')
      simplify(command("; destroy(create(#{a}, 1));"))
      assert_equal 1, get(a, 'pre_destroy_called')
    end
  end

  def test_that_calling_pre_destroy_on_a_destroyd_object_fails
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'keep', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<typeof(this) == OBJ || raise(E_INVARG);>
        vc << %Q<#{a}.keep = this;>
      end
      assert_equal 1, simplify(command(%Q<; return typeof(#{a}.keep) == INT;>))
      assert_equal 0, simplify(command(%<; destroy(create(#{a}, 0));>))
      assert_equal 1, simplify(command(%Q<; return typeof(#{a}.keep) == OBJ;>))
      assert_equal E_INVARG, simplify(command(%<; destroy(#{a}.keep);>))
      assert_equal false, valid("#{a}.keep")
    end
  end

  def test_that_calling_pre_destroy_on_a_destroyd_anonymous_object_fails
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'keep', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<typeof(this) == ANON || raise(E_INVARG);>
        vc << %Q<#{a}.keep = this;>
      end
      assert_equal 1, simplify(command(%Q<; return typeof(#{a}.keep) == INT;>))
      assert_equal 0, simplify(command(%<; destroy(create(#{a}, 1));>))
      assert_equal 1, simplify(command(%Q<; return typeof(#{a}.keep) == ANON;>))
      assert_equal E_INVARG, simplify(command(%<; destroy(#{a}.keep);>))
      assert_equal false, valid("#{a}.keep")
    end
  end

  def test_that_destroying_an_object_destroys_values_in_properties_defined_on_the_object
    run_test_as('wizard') do
      a = create(:object)
      add_property(a, 'pre_destroy_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<#{a}.pre_destroy_called = #{a}.pre_destroy_called + 1;>
      end
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        vc << %Q<x = create(#{a}, 0);>
        vc << %Q<add_property(x, "next", 0, {player, ""});>
        vc << %Q<x.next = create(#{a}, 1);>
        vc << %Q<destroy(x);>
      end
      set(a, 'pre_destroy_called', 0)
      call(a, 'go')
      assert_equal 2, get(a, 'pre_destroy_called')
    end
  end

  def test_that_destroying_an_object_destroys_values_in_properties_defined_on_the_parent
    run_test_as('wizard') do
      a = create(:object)
      add_property(a, 'next', 0, [player, ''])
      add_property(a, 'pre_destroy_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<#{a}.pre_destroy_called = #{a}.pre_destroy_called + 1;>
      end
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        vc << %Q<x = create(#{a}, 0);>
        vc << %Q<x.next = create(#{a}, 1);>
        vc << %Q<destroy(x);>
      end
      set(a, 'pre_destroy_called', 0)
      call(a, 'go')
      assert_equal 2, get(a, 'pre_destroy_called')
    end
  end

  def test_that_destroying_an_anonymous_object_destroys_values_in_properties_defined_on_the_object
    run_test_as('wizard') do
      a = create(:object)
      add_property(a, 'pre_destroy_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<#{a}.pre_destroy_called = #{a}.pre_destroy_called + 1;>
      end
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        vc << %Q<x = create(#{a}, 1);>
        vc << %Q<add_property(x, "next", 0, {player, ""});>
        vc << %Q<x.next = create(#{a}, 1);>
        vc << %Q<args || destroy(x);>
      end
      set(a, 'pre_destroy_called', 0)
      call(a, 'go')
      assert_equal 2, get(a, 'pre_destroy_called')
      set(a, 'pre_destroy_called', 0)
      call(a, 'go', 1)
      assert_equal 2, get(a, 'pre_destroy_called')
    end
  end

  def test_that_destroying_an_anonymous_object_destroys_values_in_properties_defined_on_the_parent
    run_test_as('wizard') do
      a = create(:object)
      add_property(a, 'next', 0, [player, ''])
      add_property(a, 'pre_destroy_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'pre_destroy'], ['this', 'none', 'this'])
      set_verb_code(a, 'pre_destroy') do |vc|
        vc << %Q<#{a}.pre_destroy_called = #{a}.pre_destroy_called + 1;>
      end
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        vc << %Q<x = create(#{a}, 1);>
        vc << %Q<x.next = create(#{a}, 1);>
        vc << %Q<args || destroy(x);>
      end
      set(a, 'pre_destroy_called', 0)
      call(a, 'go')
      assert_equal 2, get(a, 'pre_destroy_called')
      set(a, 'pre_destroy_called', 0)
      call(a, 'go', 1)
      assert_equal 2, get(a, 'pre_destroy_called')
    end
  end

end
