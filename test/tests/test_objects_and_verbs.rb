require 'test_helper'

class TestObjectsAndVerbs < Test::Unit::TestCase

  def setup
    run_test_as('programmer') do
      @obj = simplify(command(%Q|; o = create($nothing); o.r = 1; o.w = 1; return o;|))
    end
  end

  ## Must redefine this because anonymous objects can't be
  ## passed in/out/through Ruby-space.
  def create(parents, *args)
    case parents
    when Array
      parents = '{' + parents.map { |p| obj_ref(p) }.join(', ') + '}'
    else
      parents = obj_ref(parents)
    end

    p = rand(36**5).to_s(36)

    owner = nil
    anon = nil

    unless args.empty?
      if args.length > 1
        owner, anon = *args
      elsif args[0].kind_of? Numeric
        anon = args[0]
      else
        owner = args[0]
      end
    end

    if !owner.nil? and !anon.nil?
      simplify(command(%Q|; add_property(#{@obj}, "_#{p}", create(#{parents}, #{obj_ref(owner)}, #{value_ref(anon)}), {player, "rw"}); return "#{@obj}._#{p}";|))
    elsif !owner.nil?
      simplify(command(%Q|; add_property(#{@obj}, "_#{p}", create(#{parents}, #{obj_ref(owner)}), {player, "rw"}); return "#{@obj}._#{p}";|))
    elsif !anon.nil?
      simplify(command(%Q|; add_property(#{@obj}, "_#{p}", create(#{parents}, #{value_ref(anon)}), {player, "rw"}); return "#{@obj}._#{p}";|))
    else
      simplify(command(%Q|; add_property(#{@obj}, "_#{p}", create(#{parents}), {player, "rw"}); return "#{@obj}._#{p}";|))
    end
  end

  def typeof(*args)
    simplify(command(%Q|; return typeof(#{args.join(', ')});|))
  end

  SCENARIOS = [
    [:object, 0],
    [:anonymous, 1]
  ]

  ## add_verb

  def test_that_add_verb_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        r = add_verb(o, ['player', 'x', 'foobar'], ['this', 'none', 'this'])
        assert_not_equal E_TYPE, r
        assert_not_equal E_INVARG, r
        assert_not_equal E_PERM, r
        assert_equal 0, call(o, 'foobar')
      end
    end
  end

  def test_that_add_verb_fails_if_the_owner_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_INVARG, add_verb(o, [NOTHING, '', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  def test_that_add_verb_fails_if_the_perms_are_garbage
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_INVARG, add_verb(o, ['player', 'abc', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  def test_that_add_verb_fails_if_the_args_are_garbage
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_INVARG, add_verb(o, ['player', '', 'foobar'], ['foo', 'bar', 'baz'])
      end
    end
  end

  def test_that_add_verb_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  def test_that_add_verb_fails_if_the_programmer_does_not_have_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'w', 0)
      end
      run_test_as('programmer') do
        assert_equal E_PERM, add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  def test_that_add_verb_succeeds_if_the_programmer_has_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'w', 1)
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  def test_that_add_verb_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'w', 0)
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  def test_that_add_verb_fails_if_the_programmer_is_not_the_owner_specified_in_verbinfo
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_PERM, add_verb(o, [SYSTEM, '', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  def test_that_add_verb_succeeds_if_the_programmer_is_the_owner_specified_in_verbinfo
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_not_equal E_PERM, add_verb(o, [player, '', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  def test_that_add_verb_sets_the_owner_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        o = create(*args)
        assert_not_equal E_PERM, add_verb(o, [SYSTEM, '', 'foobar'], ['this', 'none', 'this'])
      end
    end
  end

  ## delete_verb

  def test_that_delete_verb_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
        r = delete_verb(o, 'foobar')
        assert_not_equal E_TYPE, r
        assert_not_equal E_INVARG, r
        assert_not_equal E_PERM, r
        assert_not_equal E_VERBNF, r
      end
    end
  end

  def test_that_delete_verb_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, delete_verb(o, 'foobar')
      end
    end
  end

  def test_that_delete_verb_fails_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_VERBNF, delete_verb(o, 'foobar')
      end
    end
  end

  def test_that_delete_verb_fails_if_the_programmer_does_not_have_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
        set(o, 'w', 0)
      end
      run_test_as('programmer') do
        assert_equal E_PERM, delete_verb(o, 'foobar')
      end
    end
  end

  def test_that_delete_verb_succeeds_if_the_programmer_has_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
        set(o, 'w', 1)
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, delete_verb(o, 'foobar')
      end
    end
  end

  def test_that_delete_verb_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
        set(o, 'w', 0)
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, delete_verb(o, 'foobar')
      end
    end
  end

  ## verb_info

  def test_that_verb_info_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'rw', 'foobar'], ['this', 'none', 'this'])
        assert_equal [player, 'rw', 'foobar'], verb_info(o, 'foobar')
      end
    end
  end

  def test_that_verb_info_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, verb_info(o, 'foobar')
      end
    end
  end

  def test_that_verb_info_fails_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_VERBNF, verb_info(o, 'foobar')
      end
    end
  end

  def test_that_verb_info_fails_if_the_programmer_does_not_have_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['this', 'none', 'this'])
      end
      run_test_as('programmer') do
        assert_equal E_PERM, verb_info(o, 'foobar')
      end
    end
  end

  def test_that_verb_info_succeeds_if_the_programmer_has_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'r', 'foobar'], ['this', 'none', 'this'])
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, verb_info(o, 'foobar')
      end
    end
  end

  def test_that_verb_info_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['this', 'none', 'this'])
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, verb_info(o, 'foobar')
      end
    end
  end

  ## verb_args

  def test_that_verb_args_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'rw', 'foobar'], ['any', 'on', 'this'])
        assert_equal ['any', 'on top of/on/onto/upon', 'this'], verb_args(o, 'foobar')
      end
    end
  end

  def test_that_verb_args_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, verb_args(o, 'foobar')
      end
    end
  end

  def test_that_verb_args_fails_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_VERBNF, verb_args(o, 'foobar')
      end
    end
  end

  def test_that_verb_args_fails_if_the_programmer_does_not_have_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('programmer') do
        assert_equal E_PERM, verb_args(o, 'foobar')
      end
    end
  end

  def test_that_verb_args_succeeds_if_the_programmer_has_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'r', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, verb_args(o, 'foobar')
      end
    end
  end

  def test_that_verb_args_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, verb_args(o, 'foobar')
      end
    end
  end

  ## verb_code

  def test_that_verb_code_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'rw', 'foobar'], ['any', 'on', 'this'])
        assert_equal [], verb_code(o, 'foobar')
      end
    end
  end

  def test_that_verb_code_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, verb_code(o, 'foobar')
      end
    end
  end

  def test_that_verb_code_fails_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_VERBNF, verb_code(o, 'foobar')
      end
    end
  end

  def test_that_verb_code_fails_if_the_programmer_does_not_have_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('programmer') do
        assert_equal E_PERM, verb_code(o, 'foobar')
      end
    end
  end

  def test_that_verb_code_succeeds_if_the_programmer_has_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'r', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, verb_code(o, 'foobar')
      end
    end
  end

  def test_that_verb_code_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, verb_code(o, 'foobar')
      end
    end
  end

  ## set_verb_info

  def test_that_set_verb_info_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'rw', 'foobar'], ['any', 'in', 'this'])
        set_verb_info(o, 'foobar', [player, 'x', 'barfoo'])
        assert_equal [player, 'x', 'barfoo'], verb_info(o, 'barfoo')
      end
    end
  end

  def test_that_set_verb_info_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, set_verb_info(o, 'foobar', [player, '', 'foobar'])
      end
    end
  end

  def test_that_set_verb_info_fails_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_VERBNF, set_verb_info(o, 'foobar', [player, '', 'foobar'])
      end
    end
  end

  def test_that_set_verb_info_fails_if_the_programmer_does_not_have_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('programmer') do
        assert_equal E_PERM, set_verb_info(o, 'foobar', [player, 'rw', 'barfoo'])
      end
    end
  end

  def test_that_set_verb_info_fails_even_if_the_programmer_has_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'w', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('programmer') do
        assert_equal E_PERM, set_verb_info(o, 'foobar', [player, 'rw', 'barfoo'])
      end
    end
  end

  def test_that_set_verb_info_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, set_verb_info(o, 'foobar', [player, 'rw', 'barfoo'])
      end
    end
  end

  ## set_verb_args

  def test_that_set_verb_args_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'rw', 'foobar'], ['any', 'in', 'this'])
        set_verb_args(o, 'foobar', ['any', 'any', 'any'])
        assert_equal ['any', 'any', 'any'], verb_args(o, 'foobar')
      end
    end
  end

  def test_that_set_verb_args_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, set_verb_args(o, 'foobar', ['any', 'any', 'any'])
      end
    end
  end

  def test_that_set_verb_args_fails_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_VERBNF, set_verb_args(o, 'foobar', ['any', 'any', 'any'])
      end
    end
  end

  def test_that_set_verb_args_fails_if_the_programmer_does_not_have_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['this', 'in', 'this'])
      end
      run_test_as('programmer') do
        assert_equal E_PERM, set_verb_args(o, 'foobar', ['any', 'any', 'any'])
      end
    end
  end

  def test_that_set_verb_args_succeeds_if_the_programmer_has_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'w', 'foobar'], ['this', 'in', 'this'])
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, set_verb_args(o, 'foobar', ['any', 'any', 'any'])
      end
    end
  end

  def test_that_set_verb_args_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['this', 'in', 'this'])
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, set_verb_args(o, 'foobar', ['any', 'any', 'any'])
      end
    end
  end

  ## set_verb_code

  def test_that_set_verb_code_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'rw', 'foobar'], ['any', 'in', 'this'])
        set_verb_code(o, 'foobar', ['1;', '2;', '3;'])
        assert_equal ['1;', '2;', '3;'], verb_code(o, 'foobar')
      end
    end
  end

  def test_that_set_verb_code_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, set_verb_code(o, 'foobar', ['1;', '2;', '3;'])
      end
    end
  end

  def test_that_set_verb_code_fails_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_VERBNF, set_verb_code(o, 'foobar', ['1;', '2;', '3;'])
      end
    end
  end

  def test_that_set_verb_code_fails_if_the_programmer_does_not_have_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['this', 'in', 'this'])
      end
      run_test_as('programmer') do
        assert_equal E_PERM, set_verb_code(o, 'foobar', ['1;', '2;', '3;'])
      end
    end
  end

  def test_that_set_verb_code_succeeds_if_the_programmer_has_write_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'w', 'foobar'], ['this', 'in', 'this'])
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, set_verb_code(o, 'foobar', ['1;', '2;', '3;'])
      end
    end
  end

  def test_that_set_verb_code_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['this', 'in', 'this'])
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, set_verb_code(o, 'foobar', ['1;', '2;', '3;'])
      end
    end
  end

  ## verbs

  def test_that_verbs_works_on_objects
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal [], verbs(o)
        add_verb(o, ['player', '', 'foobar'], ['this', 'none', 'this'])
        assert_equal 'foobar', verbs(o)
      end
    end
  end

  def test_that_verbs_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, verbs(o)
      end
    end
  end

  def test_that_verbs_fails_if_the_programmer_does_not_have_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'r', 0)
      end
      run_test_as('programmer') do
        assert_equal E_PERM, verbs(o)
      end
    end
  end

  def test_that_verbs_succeeds_if_the_programmer_has_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'r', 1)
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, verbs(o)
      end
    end
  end

  def test_that_verbs_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'r', 0)
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, verbs(o)
      end
    end
  end

  ## respond_to

  def test_that_respond_to_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, respond_to(o, 'foobar')
      end
    end
  end

  def test_that_respond_to_returns_false_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal 0, respond_to(o, 'foobar')
      end
    end
  end

  def test_that_respond_to_returns_verb_details_if_the_caller_is_the_owner
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, ['player', 'x', 'foo'], ['none', 'none', 'none'])
        add_verb(o, ['player', '', 'bar'], ['none', 'none', 'none'])
        assert_equal 1, simplify(command(%Q|; return respond_to(#{o}, "foo") == {#{o}, "foo"}; |))
        assert_equal 1, simplify(command(%Q|; return respond_to(#{o}, "bar") == 0; |))
      end
    end
  end

  def test_that_respond_to_returns_verb_details_if_the_caller_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, ['player', 'x', 'foo'], ['none', 'none', 'none'])
        add_verb(o, ['player', '', 'bar'], ['none', 'none', 'none'])
      end
      run_test_as('wizard') do
        assert_equal 1, simplify(command(%Q|; return respond_to(#{o}, "foo") == {#{o}, "foo"}; |))
        assert_equal 1, simplify(command(%Q|; return respond_to(#{o}, "bar") == 0; |))
      end
    end
  end

  def test_that_respond_to_returns_verb_details_if_the_object_is_readable
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'r', 1)
        add_verb(o, ['player', 'x', 'foo'], ['none', 'none', 'none'])
        add_verb(o, ['player', '', 'bar'], ['none', 'none', 'none'])
      end
      run_test_as('programmer') do
        assert_equal 1, simplify(command(%Q|; return respond_to(#{o}, "foo") == {#{o}, "foo"}; |))
        assert_equal 1, simplify(command(%Q|; return respond_to(#{o}, "bar") == 0; |))
      end
    end
  end

  def test_that_respond_to_returns_true_if_the_verb_is_callable
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'r', 0)
        add_verb(o, ['player', 'x', 'foo'], ['none', 'none', 'none'])
      end
      run_test_as('programmer') do
        assert_equal 1, simplify(command(%Q|; return respond_to(#{o}, "foo") == 1; |))
      end
    end
  end

  def test_that_respond_to_returns_false_if_the_verb_is_not_callable
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        set(o, 'r', 0)
        add_verb(o, ['player', '', 'bar'], ['none', 'none', 'none'])
      end
      run_test_as('programmer') do
        assert_equal 1, simplify(command(%Q|; return respond_to(#{o}, "bar") == 0; |))
      end
    end
  end

  ## disassemble

  def test_that_disassemble_works
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'rw', 'foobar'], ['this', 'none', 'this'])
        assert_includes disassemble(o, 'foobar'), 'Main code vector:'
      end
    end
  end

  def test_that_disassemble_fails_if_the_object_is_not_valid
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        recycle(o)
        assert_equal E_INVARG, disassemble(o, 'foobar')
      end
    end
  end

  def test_that_disassemble_fails_if_the_verb_does_not_exist
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_VERBNF, disassemble(o, 'foobar')
      end
    end
  end

  def test_that_disassemble_fails_if_the_programmer_does_not_have_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('programmer') do
        assert_equal E_PERM, disassemble(o, 'foobar')
      end
    end
  end

  def test_that_disassemble_succeeds_if_the_programmer_has_read_permission
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, 'r', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('programmer') do
        assert_not_equal E_PERM, disassemble(o, 'foobar')
      end
    end
  end

  def test_that_disassemble_succeeds_if_the_programmer_is_a_wizard
    SCENARIOS.each do |args|
      o = nil
      run_test_as('programmer') do
        o = create(*args)
        add_verb(o, [player, '', 'foobar'], ['any', 'none', 'any'])
      end
      run_test_as('wizard') do
        assert_not_equal E_PERM, disassemble(o, 'foobar')
      end
    end
  end

  ## miscellaneous

  def test_that_invocation_and_inheritance_works
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        a = kahuna(NOTHING, 'a')
        b = kahuna(a, 'b')
        c = kahuna(b, 'c', args[1])

        add_verb(a, ['player', 'xd', 'foo'], ['this', 'none', 'this'])
        set_verb_code(a, 'foo') do |vc|
          vc << %Q|return "foo";|
        end

        assert_equal 'foo', call(a, 'foo')
        assert_equal 'foo', call(b, 'foo')
        assert_equal 'foo', call(c, 'foo')

        assert_equal 'a', call(a, 'a')
        assert_equal 'b', call(b, 'b')
        assert_equal 'c', call(c, 'c')

        chparent(c, a)
        chparent(b, NOTHING)
        typeof(c) == TYPE_ANON ? chparent(a, b, [c]) : chparent(a, b)

        assert_equal 'foo', call(a, 'foo')
        assert_equal E_VERBNF, call(b, 'foo')
        assert_equal 'foo', call(c, 'foo')

        delete_verb(a, 'foo')

        assert_equal E_VERBNF, call(a, 'foo')
        assert_equal E_VERBNF, call(b, 'foo')
        assert_equal E_VERBNF, call(c, 'foo')

        assert_equal 'a', call(a, 'a')
        assert_equal 'b', call(b, 'b')
        assert_equal 'c', call(c, 'c')

        add_verb(a, ['player', 'xd', 'foo'], ['this', 'none', 'this'])
        set_verb_code(a, 'foo') do |vc|
          vc << %Q|return "foo";|
        end

        chparents(c, [a, b])

        assert_equal 'foo', call(a, 'foo')
        assert_equal E_VERBNF, call(b, 'foo')
        assert_equal 'foo', call(c, 'foo')

        assert_equal 'a', call(c, 'a')
        assert_equal 'b', call(c, 'b')
        assert_equal 'c', call(c, 'c')

        chparents(c, [b, a])

        assert_equal 'foo', call(a, 'foo')
        assert_equal E_VERBNF, call(b, 'foo')
        assert_equal 'foo', call(c, 'foo')

        assert_equal 'a', call(c, 'a')
        assert_equal 'b', call(c, 'b')
        assert_equal 'c', call(c, 'c')

        delete_verb(a, 'foo')

        assert_equal E_VERBNF, call(a, 'foo')
        assert_equal E_VERBNF, call(b, 'foo')
        assert_equal E_VERBNF, call(c, 'foo')

        assert_equal 'a', call(a, 'a')
        assert_equal 'b', call(b, 'b')
        assert_equal 'c', call(c, 'c')
      end
    end
  end

  def test_that_the_verb_cache_works
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        c = kahuna(NOTHING, 'c')
        b = kahuna(c, 'b')
        a = kahuna(b, 'a')

        add_verb(a, ['player', 'xd', 'foo'], ['this', 'none', 'this'])
        set_verb_code(a, 'foo') do |vc|
          vc << %Q|return verb;|
        end

        # Test a case that challenges the verb cache.  Both M and N
        # inherit from E.  M also inherits from A.  Because E defines
        # `bar' it is the `first_parent_with_verbs' in the list of
        # ancestors for M.  However, with respect to verbs `a' and
        # `foo', it is NOT the _correct_ first parent, because these
        # verbs are defined in the line of ancestors through A.
        #
        #      n
        #       \
        #        e (bar)
        #       /
        #      m
        #       \
        #        a (foo) - b - c
        #

        e = create(NOTHING)
        add_verb(e, ['player', 'xd', 'bar'], ['this', 'none', 'this'])
        set_verb_code(e, 'bar') do |vc|
          vc << %Q|return verb;|
        end

        n = create([e], args[1])
        m = create([e, a], args[1])

        assert_equal 'bar', call(m, 'bar')
        assert_equal 'bar', call(n, 'bar')

        assert_equal 'foo', call(m, 'foo')
        assert_equal E_VERBNF, call(n, 'foo')

        assert_equal E_VERBNF, call(n, 'c')
        assert_equal 'c', call(m, 'c')

        assert_equal 'a', call(m, 'a')
        assert_equal E_VERBNF, call(n, 'a')

        assert_equal E_VERBNF, call(n, 'b')
        assert_equal 'b', call(m, 'b')

        chparents(m, [e, b])

        assert_equal E_VERBNF, call(n, 'c')
        assert_equal 'c', call(m, 'c')

        assert_equal E_VERBNF, call(m, 'a')
        assert_equal E_VERBNF, call(n, 'a')

        assert_equal E_VERBNF, call(n, 'b')
        assert_equal 'b', call(m, 'b')

        chparents(m, [e, c])

        assert_equal E_VERBNF, call(n, 'c')
        assert_equal 'c', call(m, 'c')

        assert_equal E_VERBNF, call(m, 'a')
        assert_equal E_VERBNF, call(n, 'a')

        assert_equal E_VERBNF, call(n, 'b')
        assert_equal E_VERBNF, call(m, 'b')

        chparents(m, [e])

        assert_equal E_VERBNF, call(n, 'c')
        assert_equal E_VERBNF, call(m, 'c')

        assert_equal E_VERBNF, call(m, 'a')
        assert_equal E_VERBNF, call(n, 'a')

        assert_equal E_VERBNF, call(n, 'b')
        assert_equal E_VERBNF, call(m, 'b')
      end

      run_test_as('wizard') do
        vcs = verb_cache_stats
        unless (vcs[0] == 0 && vcs[1] == 0 && vcs[2] == 0 && vcs[3] == 0)
          s = create(NOTHING);
          t = create(NOTHING);
          m = create(s);
          n = create(t);
          a = create([m, n], args[1])

          add_verb(s, [player, 'xd', 's'], ['this', 'none', 'this'])
          set_verb_code(s, 's') { |vc| vc << %|return "s";| }
          add_verb(t, [player, 'xd', 't'], ['this', 'none', 'this'])
          set_verb_code(t, 't') { |vc| vc << %|return "t";| }
          add_verb(m, [player, 'xd', 'm'], ['this', 'none', 'this'])
          set_verb_code(m, 'm') { |vc| vc << %|return "m";| }
          add_verb(n, [player, 'xd', 'n'], ['this', 'none', 'this'])
          set_verb_code(n, 'n') { |vc| vc << %|return "n";| }

          x = create(NOTHING)
          add_verb(x, [player, 'xd', 'x'], ['this', 'none', 'this'])
          set_verb_code(x, 'x') do |vc|
            vc << %|recycle(create($nothing));|
              vc << %|a = verb_cache_stats();|
              vc << %|#{a}:s();|
              vc << %|b = verb_cache_stats();|
              vc << %|#{a}:s();|
              vc << %|c = verb_cache_stats();|
              vc << %|#{a}:t();|
              vc << %|d = verb_cache_stats();|
              vc << %|#{a}:t();|
              vc << %|e = verb_cache_stats();|
              vc << %|return {a, b, c, d, e};|
          end

          r = call(x, 'x')
          ra, rb, rc, rd, re = r

          # a:s()

          assert_equal ra[0], rb[0] # no hit
          assert_equal ra[1], rb[1] # no -hit
          assert_equal ra[2] + 1, rb[2] # miss! (m)

          # a:s()

          assert_equal rb[0] + 1, rc[0] # hit! (m)
          assert_equal rb[1], rc[1] # no -hit
          assert_equal rb[2], rc[2] # no miss

          # a:t()

          assert_equal rc[0], rd[0] # no hit
          assert_equal rc[1], rd[1] # no -hit!
          assert_equal rc[2] + 2, rd[2] # miss! (on m and n)

          # a:t()

          assert_equal rd[0] + 1, re[0] # hit! (n)
          assert_equal rd[1] + 1, re[1] # -hit! (m)
          assert_equal rd[2], re[2] # no miss

          assert_equal [0, 1, 1, 3, 3], r.map { |z| z[4][1] }
        end
      end
    end
  end

  def test_that_pass_works
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        e = kahuna(NOTHING, 'e')
        b = kahuna(_(e), 'b')
        c = kahuna(_(b), 'c', args[1])

        add_verb(e, ['player', 'xd', 'foo'], ['this', 'none', 'this'])
        set_verb_code(e, 'foo') do |vc|
          vc << %Q|return {"e", @`pass() ! ANY => {}'};|
        end

        assert_equal 'e', call(e, 'foo')
        assert_equal 'e', call(b, 'foo')
        assert_equal 'e', call(c, 'foo')

        add_verb(b, ['player', 'xd', 'foo'], ['this', 'none', 'this'])
        set_verb_code(b, 'foo') do |vc|
          vc << %Q|return {"b", @`pass() ! ANY => {}'};|
        end

        assert_equal 'e', call(e, 'foo')
        assert_equal ['b', 'e'], call(b, 'foo')
        assert_equal ['b', 'e'], call(c, 'foo')

        add_verb(c, ['player', 'xd', 'foo'], ['this', 'none', 'this'])
        set_verb_code(c, 'foo') do |vc|
          vc << %Q|return {"c", @`pass() ! ANY => {}'};|
        end

        assert_equal 'e', call(e, 'foo')
        assert_equal ['b', 'e'], call(b, 'foo')
        assert_equal ['c', 'b', 'e'], call(c, 'foo')

        add_verb(c, ['player', 'xd', 'boo'], ['this', 'none', 'this'])
        set_verb_code(c, 'boo') do |vc|
          vc << %Q|return {"c", @pass()};|
        end

        assert_equal E_VERBNF, call(c, 'boo')

        add_verb(e, ['player', 'xd', 'hoo'], ['this', 'none', 'this'])
        set_verb_code(e, 'hoo') do |vc|
          vc << %Q|return {"e", @pass()};|
        end

        assert_equal E_INVIND, call(e, 'hoo')

        chparents(c, [e, b])

        assert_equal 'e', call(e, 'foo')
        assert_equal ['b', 'e'], call(b, 'foo')
        assert_equal ['c', 'e'], call(c, 'foo')

        chparents(c, [b, e])

        assert_equal 'e', call(e, 'foo')
        assert_equal ['b', 'e'], call(b, 'foo')
        assert_equal ['c', 'b', 'e'], call(c, 'foo')

        typeof(c) == TYPE_ANON ? chparents(b, [], [c]) : chparents(b, [])

        assert_equal 'e', call(e, 'foo')
        assert_equal 'b', call(b, 'foo')
        assert_equal ['c', 'b'], call(c, 'foo')

        delete_verb(b, 'foo')

        assert_equal 'e', call(e, 'foo')
        assert_equal E_VERBNF, call(b, 'foo')
        assert_equal ['c', 'e'], call(c, 'foo')

        delete_verb(e, 'foo')

        assert_equal E_VERBNF, call(e, 'foo')
        assert_equal E_VERBNF, call(b, 'foo')
        assert_equal 'c', call(c, 'foo')

        assert_equal [_(b), _(e)], parents(c)

        assert_equal E_VERBNF, call(c, 'goo')
      end
    end
  end

  def test_that_two_calls_that_free_the_activation_do_not_leak_memory
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        a = kahuna(NOTHING, 'a', args[1])
        c = kahuna(NOTHING, 'c')
        m = create(c, args[1])
        call(a, 'a')
        call(m, 'c')
      end
    end
  end

  private

  def kahuna(parent, name, opt = 0)
    create(parent, opt).tap do |object|
      set(object, 'name', name)
      add_verb(object, [player, 'xd', name], ['this', 'none', 'this'])
      set_verb_code(object, name) do |vc|
        vc << %|return "#{name}";|
      end
    end
  end

end
