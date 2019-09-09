require 'test_helper'

class TestCreate < Test::Unit::TestCase

  def setup
    run_test_as('wizard') do
      @o = create(:nothing)
      add_property(@o, 'args', 0, [me, ''])
      add_verb(@o, [me, 'x', 'initialize'], ['this', 'none', 'this'])
      set_verb_code(@o, 'initialize', ['this.args = args;'])
    end
  end

  def test_that_an_argument_is_required
    run_test_as('programmer') do
      assert_equal E_ARGS, simplify(command("; create();"))
    end
  end

  def test_that_the_first_argument_must_be_an_object_number_or_a_list_of_object_numbers
    run_test_as('wizard') do
      assert_equal E_TYPE, simplify(command("; create(1);"))
      assert_equal E_TYPE, simplify(command("; create({1});"))
      assert_not_equal E_TYPE, simplify(command("; create($nothing);"))
      assert_not_equal E_TYPE, simplify(command("; create($object);"))
      assert_not_equal E_TYPE, simplify(command("; create({#1, #2, #3});"))
    end
  end

  def test_that_the_second_argument_is_the_owner_if_it_is_an_object_number
    run_test_as('wizard') do
      assert_not_equal E_TYPE, simplify(command("; return create($nothing, #1);"))
      x = simplify(command("; return create($nothing, $nothing);"))
      assert_equal x, get(x, 'owner')
      y = simplify(command("; return create($nothing, #{x});"))
      assert_equal x, get(y, 'owner')
    end
  end

  def test_that_the_second_argument_is_the_anonymous_flag_if_it_is_an_integer
    run_test_as('wizard') do
      assert_not_equal E_TYPE, simplify(command("; create($nothing, 1);"))
      x = simplify(command("; return typeof(create($nothing, 1));"))
      assert_equal TYPE_ANON, x
      y = simplify(command("; return typeof(create($nothing, 0));"))
      assert_equal TYPE_OBJ, y
    end
  end

  def test_that_the_second_argument_is_the_initialize_args_if_it_is_a_list
    run_test_as('wizard') do
      assert_not_equal E_TYPE, simplify(command("; create(#{@o}, {});"))
      x = simplify(command("; return create(#{@o}, {1, 2, 3});"))
      assert_equal [1, 2, 3], get(x, 'args')
      y = simplify(command("; return create(#{@o});"))
      assert_equal [], get(y, 'args')
    end
  end

  def test_that_the_third_argument_is_the_anonymous_flag_if_it_is_an_integer
    run_test_as('wizard') do
      assert_not_equal E_TYPE, simplify(command("; create($nothing, #1, 1);"))
      x = simplify(command("; return typeof(create($nothing, #1, 1));"))
      assert_equal TYPE_ANON, x
      y = simplify(command("; return typeof(create($nothing, #1, 0));"))
      assert_equal TYPE_OBJ, y
    end
  end

  def test_that_the_third_argument_is_the_initialize_args_if_it_is_a_list
    run_test_as('wizard') do
      assert_not_equal E_TYPE, simplify(command("; create(#{@o}, #1, {});"))
      x = simplify(command("; return create(#{@o}, #1, {1.1, 2.0, 3});"))
      assert_equal [1.1, 2.0, 3], get(x, 'args')
      y = simplify(command("; return create(#{@o}, #1, {});"))
      assert_equal [], get(y, 'args')
    end
  end

  def test_that_the_fourth_argument_is_the_anonymous_flag_if_it_is_an_integer
    run_test_as('wizard') do
      assert_not_equal E_TYPE, simplify(command("; create($nothing, #1, {}, 1);"))
      x = simplify(command("; return typeof(create($nothing, #1, {}, 1));"))
      assert_equal TYPE_ANON, x
      y = simplify(command("; return typeof(create($nothing, #1, {}, 0));"))
      assert_equal TYPE_OBJ, y
    end
  end

  def test_that_the_fourth_argument_is_the_initialize_args_if_it_is_a_list
    run_test_as('wizard') do
      assert_not_equal E_TYPE, simplify(command("; create(#{@o}, #1, 0, {});"))
      x = simplify(command("; return create(#{@o}, #1, 0, {1.1, 2.0, 3});"))
      assert_equal [1.1, 2.0, 3], get(x, 'args')
      y = simplify(command("; return create(#{@o}, #1, 0, {});"))
      assert_equal [], get(y, 'args')
    end
  end

  def test_that_anonymous_objects_cannot_be_their_own_owner
    run_test_as('wizard') do
      assert_not_equal E_INVARG, simplify(command("; create($nothing, $nothing, 0);"))
      assert_equal E_INVARG, simplify(command("; create($nothing, $nothing, 1);"))
    end
  end

  def test_a_variety_of_argument_errors
    run_test_as('wizard') do
      assert_equal E_TYPE, simplify(command("; create($nothing, 1, $nothing);"))
      assert_equal E_TYPE, simplify(command("; create($nothing, {}, $nothing);"))
      assert_equal E_TYPE, simplify(command("; create($nothing, $nothing, $nothing);"))
      assert_equal E_TYPE, simplify(command("; create($nothing, $nothing, 1, 1);"))
      assert_equal E_TYPE, simplify(command("; create($nothing, $nothing, {}, {});"))
      assert_equal E_TYPE, simplify(command("; create($nothing, 1, 1);"))
      assert_equal E_TYPE, simplify(command("; create($nothing, {}, {});"))
      assert_equal E_TYPE, simplify(command("; create($nothing, $object, 1.0);"))
      assert_equal E_TYPE, simplify(command("; create($nothing, 1.0, 1);"))
      assert_equal E_TYPE, simplify(command("; create($nothing, []);"))
      assert_equal E_TYPE, simplify(command("; create($nothing, 1.0);"))
    end
  end

  def test_that_a_wizard_can_change_the_fertile_flag
    x = nil
    run_test_as('programmer') do
      x = create(:nothing)
    end
    run_test_as('wizard') do
      assert_not_equal E_PERM, set(x, 'f', 1)
      assert_equal 1, get(x, 'f')
    end
  end

  def test_that_an_owner_can_change_the_fertile_flag
    x = nil
    run_test_as('programmer') do
      x = create(:nothing)
    end
    run_test_as('programmer') do
      assert_equal E_PERM, set(x, 'f', 1)
      assert_equal 0, get(x, 'f')
      assert_not_equal E_PERM, set(player, 'f', 1)
      assert_equal 1, get(player, 'f')
    end
  end

  def test_that_a_wizard_can_change_the_anonymous_flag
    x = nil
    run_test_as('programmer') do
      x = create(:nothing)
    end
    run_test_as('wizard') do
      assert_not_equal E_PERM, set(x, 'a', 1)
      assert_equal 1, get(x, 'a')
    end
  end

  def test_that_an_owner_can_change_the_anonymous_flag
    x = nil
    run_test_as('programmer') do
      x = create(:nothing)
    end
    run_test_as('programmer') do
      assert_equal E_PERM, set(x, 'a', 1)
      assert_equal 0, get(x, 'a')
      assert_not_equal E_PERM, set(player, 'a', 1)
      assert_equal 1, get(player, 'a')
    end
  end

  def test_that_a_wizard_can_create_even_if_the_fertile_flag_is_not_set
    run_test_as('wizard') do
      assert_equal 0, get(:system, 'f')
      assert_not_equal E_PERM, simplify(command("; create($system, 0);"))
      assert_equal 1, get(:object, 'f')
      assert_not_equal E_PERM, simplify(command("; create($object, 0);"))
    end
  end

  def test_that_an_owner_can_create_even_if_the_fertile_flag_is_not_set
    run_test_as('programmer') do
      assert_equal 0, get(player, 'f')
      assert_not_equal E_PERM, simplify(command("; create(player, 0);"))
      assert_equal 0, get(:anonymous, 'f')
      assert_equal E_PERM, simplify(command("; create($anonymous, 0);"))
    end
  end

  def test_that_a_programmer_can_create_only_when_the_fertile_flag_is_set
    run_test_as('programmer') do
      assert_equal 1, get(:object, 'f')
      assert_not_equal E_PERM, simplify(command("; create($object, 0);"))
      assert_equal 0, get(:anonymous, 'f')
      assert_equal E_PERM, simplify(command("; create($anonymous, 0);"))
    end
  end

  def test_that_a_wizard_can_create_even_if_the_anonymous_flag_is_not_set
    run_test_as('wizard') do
      assert_equal 0, get(:system, 'a')
      assert_not_equal E_PERM, simplify(command("; create($system, 1);"))
      assert_equal 1, get(:anonymous, 'a')
      assert_not_equal E_PERM, simplify(command("; create($anonymous, 1);"))
    end
  end

  def test_that_an_owner_can_create_even_if_the_anonymous_flag_is_not_set
    run_test_as('programmer') do
      assert_equal 0, get(player, 'a')
      assert_not_equal E_PERM, simplify(command("; create(player, 1);"))
      assert_equal 0, get(:object, 'a')
      assert_equal E_PERM, simplify(command("; create($object, 1);"))
    end
  end

  def test_that_a_programmer_can_create_only_when_the_anonymous_flag_is_set
    run_test_as('programmer') do
      assert_equal 1, get(:anonymous, 'a')
      assert_not_equal E_PERM, simplify(command("; create($anonymous, 1);"))
      assert_equal 0, get(:object, 'a')
      assert_equal E_PERM, simplify(command("; create($object, 1);"))
    end
  end

  def test_that_creating_an_object_creates_an_object
    run_test_as('programmer') do
      t = simplify(command("; return OBJ;"))
      assert_equal t, simplify(command("; return typeof(create($object, 0));"))
    end
  end

  def test_that_creating_an_anonymous_object_creates_an_anonymous_object
    run_test_as('programmer') do
      t = simplify(command("; return ANON;"))
      assert_equal t, simplify(command("; return typeof(create($anonymous, 1));"))
    end
  end

  def test_that_creating_an_object_increments_max_object
    run_test_as('programmer') do
      m = max_object
      simplify(command("; create($object, 0);"))
      assert_not_equal m, max_object
    end
  end

  def test_that_creating_an_anonymous_object_does_not_increment_max_object
    run_test_as('programmer') do
      m = max_object
      simplify(command("; create($anonymous, 1);"))
      assert_equal m, max_object
    end
  end

  def test_that_creating_an_object_calls_initialize
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'initialize_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'initialize'], ['this', 'none', 'this'])
      set_verb_code(a, 'initialize') do |vc|
        vc << %Q<typeof(this) == OBJ || raise(E_INVARG);>
        vc << %Q<#{a}.initialize_called = 1;>
      end
      assert_equal 0, get(a, 'initialize_called')
      simplify(command("; create(#{a}, 0);"))
      assert_equal 1, get(a, 'initialize_called')
    end
  end

  def test_that_creating_an_anonymous_object_calls_initialize
    run_test_as('programmer') do
      a = create(:object)
      add_property(a, 'initialize_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'initialize'], ['this', 'none', 'this'])
      set_verb_code(a, 'initialize') do |vc|
        vc << %Q<typeof(this) == ANON || raise(E_INVARG);>
        vc << %Q<#{a}.initialize_called = 1;>
      end
      assert_equal 0, get(a, 'initialize_called')
      simplify(command("; create(#{a}, 1);"))
      assert_equal 1, get(a, 'initialize_called')
    end
  end

  def test_that_an_object_appears_in_its_parents_children
    run_test_as('programmer') do
      a = create(:object)
      simplify(command("; create(#{a}, 0);"))
      assert_not_equal [], simplify(command("; return children(#{a});"))
    end
  end

  def test_that_an_anonymous_object_appears_in_its_parents_children
    run_test_as('programmer') do
      a = create(:object)
      simplify(command("; create(#{a}, 1);"))
      assert_equal [], simplify(command("; return children(#{a});"))
    end
  end

  def test_that_duplicate_parents_is_not_allowed
    run_test_as('programmer') do
      assert_equal E_INVARG, create([:object, :object], 0)
      assert_equal E_INVARG, create([:anonymous, :anonymous], 1)
    end
  end

  def test_that_parents_cannot_have_properties_with_the_same_name
    run_test_as('wizard') do
      a = create(NOTHING)
      b = create(NOTHING)

      add_property(a, 'foo', 1, [a, ''])
      add_property(b, 'foo', 2, [b, ''])

      assert_equal E_INVARG, create([a, b], 0)
      assert_equal E_INVARG, create([a, b], 1)
    end
  end

end
