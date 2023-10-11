require 'test_helper'

class TestStressObjects < Test::Unit::TestCase

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

  def test_that_the_first_argument_to_ancestors_must_be_an_object
    run_test_as('programmer') do
      assert_equal E_TYPE, simplify(command(%Q|; ancestors(1);|))
      assert_equal E_TYPE, simplify(command(%Q|; ancestors(1.1);|))
      assert_equal E_TYPE, simplify(command(%Q|; ancestors("1");|))
    end
  end

  def test_that_the_first_argument_to_descendants_must_be_an_object
    run_test_as('programmer') do
      assert_equal E_TYPE, simplify(command(%Q|; descendants(1);|))
      assert_equal E_TYPE, simplify(command(%Q|; descendants(1.1);|))
      assert_equal E_TYPE, simplify(command(%Q|; descendants("1");|))
    end
  end

  def test_that_ancestors_includes_self
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        d = create(NOTHING, args[1])
        assert simplify(command(%Q|; {#{d}} == ancestors(#{d}, 1);|))
        assert simplify(command(%Q|; {} == ancestors(#{d}, 0);|))
      end
    end
  end

  def test_that_descendants_includes_self
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        d = create(NOTHING, args[1])
        assert simplify(command(%Q|; {#{d}} == descendants(#{d}, 1);|))
        assert simplify(command(%Q|; {} == descendants(#{d}, 0);|))
      end
    end
  end

  def test_that_chparent_chparents_works
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        o = create(NOTHING)
        a = create(NOTHING, args[1])
        assert_equal NOTHING, parent(a)
        assert_not_equal E_TYPE, chparent(a, o)
        assert_equal _(o), parent(a)
        assert_equal _(o), ancestors(a)
      end

      run_test_as('wizard') do
        o = create(NOTHING)
        p = create(NOTHING)
        a = create(NOTHING, args[1])
        assert_equal [], parents(a)
        assert_not_equal E_TYPE, chparents(a, [o, p])
        assert_equal [_(o), _(p)], parents(a)
        assert_equal [_(o), _(p)], ancestors(a)
      end
    end
  end

  def test_that_chparent_chparents_stress_tests
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        a = create(NOTHING, args[1])
        b = create(NOTHING)
        c = create(NOTHING)

        assert_equal NOTHING, parent(a)
        assert_equal NOTHING, parent(b)
        assert_equal NOTHING, parent(c)

        chparent(b, c)
        chparent(a, b)

        assert_equal _(b), parent(a)
        assert_equal _(c), parent(b)
        assert_equal NOTHING, parent(c)

        if typeof(a) == TYPE_OBJ
          assert_equal [], children(a)
          assert_equal _(a), children(b)
          assert_equal _(b), children(c)
        else
          assert_equal [], children(a)
          assert_equal [], children(b)
          assert_equal _(b), children(c)
        end

        chparent(a, c)

        assert_equal _(c), parent(a)
        assert_equal _(c), parent(b)
        assert_equal NOTHING, parent(c)

        if typeof(a) == TYPE_OBJ
          assert_equal [], children(a)
          assert_equal [], children(b)
          assert_equal [_(b), _(a)], children(c)
        else
          assert_equal [], children(a)
          assert_equal [], children(b)
          assert_equal _(b), children(c)
        end

        chparent(a, NOTHING)
        chparent(b, NOTHING)
        chparent(c, NOTHING)

        chparents(c, [])
        chparents(b, [c])
        chparents(a, [b, c])

        assert_equal [_(b), _(c)], parents(a)
        assert_equal _(c), parents(b)
        assert_equal [], parents(c)

        if typeof(a) == TYPE_OBJ
          assert_equal [], children(a)
          assert_equal _(a), children(b)
          assert_equal [_(b), _(a)], children(c)
        else
          assert_equal [], children(a)
          assert_equal [], children(b)
          assert_equal _(b), children(c)
        end

        assert_equal [_(b), _(c)], ancestors(a)
        assert_equal _(c), ancestors(b)
        assert_equal [], ancestors(c)

        if typeof(a) == TYPE_OBJ
          assert_equal [], descendants(a)
          assert_equal _(a), descendants(b)
          assert_equal [_(b), _(a)], descendants(c)
        else
          assert_equal [], descendants(a)
          assert_equal [], descendants(b)
          assert_equal _(b), descendants(c)
        end

        chparents(a, [c])

        assert_equal _(c), ancestors(a)
        assert_equal _(c), ancestors(b)
        assert_equal [], ancestors(c)

        if typeof(a) == TYPE_OBJ
          assert_equal [], descendants(a)
          assert_equal [], descendants(b)
          assert_equal [_(b), _(a)], descendants(c)
        else
          assert_equal [], descendants(a)
          assert_equal [], descendants(b)
          assert_equal _(b), descendants(c)
        end

        chparents(a, [])
        chparents(b, [])

        assert_equal NOTHING, parent(a)
        assert_equal NOTHING, parent(b)
        assert_equal NOTHING, parent(c)

        assert_equal [], parents(a)
        assert_equal [], parents(b)
        assert_equal [], parents(c)

        assert_equal [], children(a)
        assert_equal [], children(b)
        assert_equal [], children(c)
      end
    end
  end

  def test_that_chparent_chparents_stress_tests_x
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        a = create(NOTHING)
        b = create(NOTHING)
        c = create(NOTHING, args[1])

        # Test that if two objects define the same property by name,
        # the assigned property value (and info) is not preserved
        # across chparent.  Test both the single and multiple
        # inheritance cases.

        add_property(a, 'foo', 'foo', [a, 'c'])
        add_property(b, 'foo', 'foo', [b, ''])

        chparent(c, a)
        assert_equal [player, 'c'], property_info(c, 'foo')
        set(c, 'foo', 'bar')
        assert_equal 'bar', get(c, 'foo')

        chparent(c, b)
        assert_equal [_(b), ''], property_info(c, 'foo')
        assert_equal 'foo', get(c, 'foo')

        m = create(NOTHING)
        n = create(NOTHING)

        chparents(c, [m, n, a])
        assert_equal [player, 'c'], property_info(c, 'foo')
        set(c, 'foo', 'bar')
        assert_equal 'bar', get(c, 'foo')

        chparents(c, [b, m, n])
        assert_equal [_(b), ''], property_info(c, 'foo')
        assert_equal 'foo', get(c, 'foo')
      end
    end
  end

  def test_that_chparent_chparents_stress_tests_y
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        a = create(NOTHING)
        b = create(NOTHING)
        m = create(NOTHING)
        n = create(NOTHING)
        c = create(NOTHING, args[1])

        # Test that if a descendent object defines a property with the
        # same name as one defined either on the new parent or on one
        # of its ancestors, chparent/chparents fails.

        add_property(a, 'foo', 'foo', [a, ''])
        add_property(b, 'foo', 'foo', [b, ''])
        add_property(n, 'foo', 'foo', [n, 'rw'])

        # `a' and `b' define `foo' -- so does `n'

        chparent a, m
        assert_equal E_INVARG, chparent(m, n)

        chparent b, m
        assert_equal E_INVARG, chparent(m, n)

        if typeof(c) == TYPE_OBJ
          assert_equal E_INVARG, chparents(m, [n])
          assert_equal E_INVARG, chparents(m, [n, c])
          assert_equal E_INVARG, chparents(m, [c, n])
          assert_not_equal E_INVARG, chparents(m, [c])
        else
          assert_equal E_INVARG, chparents(m, [n])
          assert_equal E_TYPE, chparents(m, [n, c])
          assert_equal E_TYPE, chparents(m, [c, n])
          assert_equal E_TYPE, chparents(m, [c])
        end

        delete_property(a, 'foo')
        delete_property(b, 'foo')
      end
    end
  end

  def test_parent_chparent_parameter_errors
    run_test_as('wizard') do
      assert_equal E_ARGS, simplify(command(%Q|; parent();|))
      assert_equal E_ARGS, simplify(command(%Q|; parent(1, 2);|))
      assert_equal E_TYPE, simplify(command(%Q|; parent(1);|))
      assert_equal E_TYPE, simplify(command(%Q|; parent("1");|))
      assert_equal E_INVARG, simplify(command(%Q|; parent($nothing);|))
      assert_equal E_INVARG, simplify(command(%Q|; parent(#{invalid_object});|))

      assert_equal E_ARGS, simplify(command(%Q|; chparent();|))
      assert_equal E_ARGS, simplify(command(%Q|; chparent(1);|))
      assert_equal E_ARGS, simplify(command(%Q|; chparent(1, 2, 3, 4);|))
      assert_equal E_TYPE, simplify(command(%Q|; chparent(1, 2, 3);|))
      assert_equal E_TYPE, simplify(command(%Q|; chparent(1, 2);|))
      assert_equal E_TYPE, simplify(command(%Q|; chparent($object, 2);|))
      assert_equal E_TYPE, simplify(command(%Q|; chparent($object, "2");|))
      assert_equal E_INVARG, simplify(command(%Q|; chparent($nothing, $object);|))
      assert_equal E_INVARG, simplify(command(%Q|; chparent($object, #{invalid_object});|))
      assert_equal E_RECMOVE, simplify(command(%Q|; chparent($object, $object);|))

      assert_equal E_INVARG, simplify(command(%Q|; chparents($object, {$nothing});|))
      assert_equal E_INVARG, simplify(command(%Q|; chparents($object, {#{invalid_object}});|))
      assert_equal E_RECMOVE, simplify(command(%Q|; chparents($object, {$object});|))
    end
  end

  def test_parent_chparent_permission_errors
    SCENARIOS.each do |args|

      wizard = nil
      a = b = nil

      run_test_as('wizard') do
        wizard = player

        a = create(NOTHING)
        b = create(a, a, args[1])
        set(b, 'f', 1)

        assert_equal wizard, get(a, 'owner')
        assert_equal _(a), get(b, 'owner')

        b = create(a, a)
        set(b, 'f', 1)
      end

      programmer = nil
      c = d = nil

      run_test_as('programmer') do
        programmer = player

        c = create(NOTHING, args[1])
        d = create(b, programmer)
        set(d, 'f', 1)

        assert_equal E_PERM, chparent(c, a)
        assert_not_equal E_PERM, chparent(c, b)

        assert_equal E_PERM, chparents(c, [a, b])
        assert_equal E_PERM, chparents(c, [b, a])

        assert_equal programmer, get(c, 'owner')
        assert_equal programmer, get(d, 'owner')
      end

      run_test_as('wizard') do
        e = create(NOTHING, args[1])

        assert_not_equal E_PERM, chparent(e, a)
        assert_not_equal E_PERM, chparent(e, b)
        assert_not_equal E_PERM, chparent(e, c)
        assert_not_equal E_PERM, chparent(e, d)
      end
    end
  end

  def test_parent_chparent_errors
    SCENARIOS.each do |args|
      run_test_as('wizard') do
        a = create(NOTHING, args[1])
        b = create(NOTHING)
        c = create(NOTHING)

        chparent b, c
        chparent a, b

        if typeof(a) == TYPE_OBJ
          assert_equal E_RECMOVE, chparent(c, a)
        else
          assert_equal E_TYPE, chparent(c, a)
        end

        if typeof(a) == TYPE_OBJ
          assert_equal E_RECMOVE, chparents(c, [a, b])
        else
          assert_equal E_TYPE, chparent(c, a)
        end

        # Test that if two objects define the same property by name, a
        # new object cannot be created using both of them as parents.

        d = create(NOTHING)
        e = create(NOTHING)

        add_property(d, 'foo', 123, [d, ''])
        add_property(e, 'foo', 'abc', [e, ''])

        f = create(NOTHING, args[1])
        assert_equal E_INVARG, chparents(f, [d, e])

        # Test that duplicate objects are not allowed.

        assert_equal E_INVARG, chparents(f, [d, d])
        assert_equal E_INVARG, chparents(f, [e, e])
      end
    end
  end

  def test_that_wizards_may_pass_a_third_argument_to_chparent_chparents
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        o = create(*args)
        assert_equal E_PERM, simplify(command("; chparent(#{o}, $object, {});"))
        assert_equal E_PERM, simplify(command("; chparents(#{o}, {$object}, {});"))
      end
      run_test_as('wizard') do
        o = create(*args)
        assert_not_equal E_PERM, simplify(command("; chparent(#{o}, $object, {});"))
        assert_not_equal E_PERM, simplify(command("; chparents(#{o}, {$object}, {});"))
      end
    end
  end

  def test_that_the_third_argument_cannot_contain_duplicates
    run_test_as('wizard') do
      o = create(:object)
      p = create(o)

      a = create(p, 1)
      b = create(p, 1)

      assert_equal E_INVARG, chparent(p, :object, [a, a])
      assert_equal E_INVARG, chparents(p, [:object], [b, b])
    end
  end

  def test_that_the_third_argument_must_contain_anonymous_objects
    run_test_as('wizard') do
      o = create(:object)
      p = create(o)

      s = MooObj.new('#1')
      t = MooObj.new('#2')
      m = create(p, 0)
      n = create(p, 0)

      assert_equal E_INVARG, chparent(p, :object, [s, t])
      assert_equal E_INVARG, chparents(p, [:object], [m, n])
    end
  end

  def test_that_the_third_argument_must_contain_children_of_the_first_argument
    run_test_as('wizard') do
      o = create(:object)
      p = create(o)

      x = create(o, 1)
      y = create(o, 1)
      a = create(:anonymous, 1)
      b = create(:anonymous, 1)

      assert_equal E_INVARG, chparent(p, :object, [x, y])
      assert_equal E_INVARG, chparents(p, [:object], [a, b])
    end
  end

  def test_that_the_list_of_children_must_not_define_properties_on_the_proposed_parents
    run_test_as('wizard') do
      o = create(:object)
      p = create(o)

      a = create(p, 1)
      b = create(p, 1)

      add_property(a, 'foo', 'abc', [player, ''])
      add_property(b, 'bar', 123, [player, ''])

      x = create(:nothing)

      add_property(x, 'foo', 0, [player, ''])
      add_property(x, 'bar', 0, [player, ''])

      assert_equal E_INVARG, chparent(p, x, [a])
      assert_equal E_INVARG, chparents(p, [x], [b])
    end
  end

  def test_that_children_passed_to_chparent_chparents_remain_valid
    run_test_as('wizard') do
      o = create(:nothing)
      p = create(o)
      q = create(p)
      r = create(q)

      a = create(r, 1)
      b = create(r, 1)

      chparent(r, p, [a, b])
      chparents(r, [o], [a, b])

      assert_equal [_(r), _(o)], ancestors(a)
      assert_equal [_(r), _(o)], ancestors(b)

      assert_equal true, valid(a)
      assert_equal true, valid(b)
    end
  end

  def test_that_parent_maps_parents_to_parent
    SCENARIOS.each do |args|
      run_test_as('programmer') do

        # Test that `parent()' performs the following mapping:
        #   [x, y, z] -> x
        #   [x] -> x
        #   [] -> #-1

        x = create(NOTHING)
        y = create(NOTHING)
        z = create(NOTHING)
        a = create([x, y, z], args[1])
        b = create([x], args[1])
        c = create([], args[1])

        assert_equal [_(x), _(y), _(z)], parents(a)
        assert_equal _(x), parents(b)
        assert_equal [], parents(c)

        assert_equal _(x), parent(a)
        assert_equal _(x), parent(b)
        assert_equal NOTHING, parent(c)
      end
    end
  end

  def test_that_recycling_a_parent_does_not_blow_away_all_parents
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        a = create([])
        b = create([])
        c = create([])

        m = create([a, b, c], args[1])

        assert_equal [_(a), _(b), _(c)], parents(m)

        recycle(b)

        if typeof(m) == TYPE_OBJ
          assert_equal [_(a), _(c)], parents(m)
        else
          assert_equal false, valid(m)
        end
      end
    end
  end

  def test_that_recycling_a_parent_merges_remaining_parents_correctly
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        a = create(OBJECT)
        b = create([a])
        c = create([])

        d = create([])
        e = create(NOTHING)

        m = create([d, e])

        x = create([a, b, c, m], args[1])

        assert_equal [_(a), _(b), _(c), _(m)], parents(x)

        recycle(b)
        if typeof(x) == TYPE_OBJ
          assert_equal [_(a), _(c), _(m)], parents(x)
        else
          assert_equal false, valid(x)
        end

        recycle(m)
        if typeof(x) == TYPE_OBJ
          assert_equal [_(a), _(c), _(d), _(e)], parents(x)
        else
          assert_equal false, valid(x)
        end

        recycle(e)
        if typeof(x) == TYPE_OBJ
          assert_equal [_(a), _(c), _(d)], parents(x)
        else
          assert_equal false, valid(x)
        end
      end
    end
  end

  def test_that_the_caller_of_object_bytes_must_be_wizardly
    run_test_as('programmer') do
      assert_equal E_PERM, simplify(command(%Q|; object_bytes($object);|))
    end
    run_test_as('wizard') do
      assert_not_equal E_PERM, simplify(command(%Q|; object_bytes($object);|))
    end
  end

  def test_that_the_argument_to_object_bytes_must_be_an_object
    run_test_as('wizard') do
      assert_equal E_TYPE, simplify(command(%Q|; object_bytes(1);|))
      assert_equal E_TYPE, simplify(command(%Q|; object_bytes(1.1);|))
      assert_equal E_TYPE, simplify(command(%Q|; object_bytes("1");|))
    end
  end

  def test_that_the_argument_to_object_bytes_must_be_valid
    run_test_as('wizard') do
      assert_equal E_INVIND, simplify(command(%Q|; o = create($object, 0); recycle(o); object_bytes(o);|))
      assert_equal E_INVIND, simplify(command(%Q|; o = create($anonymous, 1); recycle(o); object_bytes(o);|))
    end
  end

  def test_that_object_bytes_works
    run_test_as('wizard') do
      assert simplify(command(%Q|; return object_bytes($object);|)) > 0
      assert simplify(command(%Q|; o = create($object, 0); return object_bytes(o);|)) > 0
      assert simplify(command(%Q|; o = create($anonymous, 1); return object_bytes(o);|)) > 0
    end
  end

  if @@options['ownership_quota']
  def test_that_ownership_quota_is_changed_as_objects_are_created_and_recycled
    SCENARIOS.each do |args|
      run_test_as('programmer') do
        add_property(player, 'ownership_quota', 3, [player, ''])
        assert_equal 3, get(player, 'ownership_quota')

        # create
        a = create(*args)
        assert_equal 2, get(player, 'ownership_quota')
        b = create(*args)
        assert_equal 1, get(player, 'ownership_quota')
        c = create(*args)
        assert_equal 0, get(player, 'ownership_quota')
        d = create(*args)
        assert_equal E_QUOTA, d

        # recycle
        recycle(a)
        assert_equal 1, get(player, 'ownership_quota')
        recycle(c)
        assert_equal 2, get(player, 'ownership_quota')
        recycle(b)
        assert_equal 3, get(player, 'ownership_quota')
      end
    end
  end
  end

end
