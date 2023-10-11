require 'test_helper'

class TestObject < Test::Unit::TestCase

  def test_create
    run_test_as('wizard') do
      a = create(NOTHING)
      b = create(a)
      c = create(b, NOTHING)
      d = create(b, a)

      # test that `parent()' works for single inheritance hierarchies
      assert_equal NOTHING, parent(a)
      assert_equal a, parent(b)
      assert_equal b, parent(c)
      assert_equal b, parent(d)

      # test that `children()' works for single inheritance hierarchies
      assert_equal b, children(a)
      assert_equal [c, d], children(b)
      assert_equal [], children(c)
      assert_equal [], children(d)

      # test that create sets the owner correctly
      assert_equal player, get(a, 'owner')
      assert_equal player, get(b, 'owner')
      assert_equal c, get(c, 'owner')
      assert_equal a, get(d, 'owner')

      i = create([b, c])
      j = create([b, c], i)
      k = create([], NOTHING)
      l = create([])

      # test that `parents()' works for multiple inheritance hierarchies
      assert_equal [b, c], parents(i)
      assert_equal [b, c], parents(j)
      assert_equal [], parents(k)
      assert_equal [], parents(l)

      # test that `children()' works for multiple inheritance hierarchies
      assert_equal [], children(i)
      assert_equal [], children(j)
      assert_equal [], children(k)
      assert_equal [], children(l)

      # test that create sets the owner correctly
      assert_equal player, get(i, 'owner')
      assert_equal i, get(j, 'owner')
      assert_equal k, get(k, 'owner')
      assert_equal player, get(l, 'owner')
    end

    # I changed how the server interprets the the second argument to
    # `create()' when it is not a valid object reference.  Test that
    # `create()' raises E_INVARG if the second argument is not valid
    # and is not #-1 (AKA $nothing).

    run_test_as('wizard') do
      e = create(NOTHING, NOTHING)
      f = create(NOTHING, AMBIGUOUS_MATCH)
      g = create(NOTHING, FAILED_MATCH)
      h = create(NOTHING, invalid_object)

      # the old way...
      #assert_equal e, get(e, 'owner')
      #assert_equal AMBIGUOUS_MATCH, get(f, 'owner')
      #assert_equal FAILED_MATCH, get(g, 'owner')
      #assert_equal invalid_object, get(h, 'owner')

      # the new way...
      assert_equal e, get(e, 'owner')
      assert_equal E_INVARG, f
      assert_equal E_INVARG, g
      assert_equal E_INVARG, h
    end

    run_test_as('programmer') do
      e = create(NOTHING, NOTHING)
      f = create(NOTHING, AMBIGUOUS_MATCH)
      g = create(NOTHING, FAILED_MATCH)
      h = create(NOTHING, invalid_object)

      # the old way...
      #assert_equal E_PERM, e
      #assert_equal E_PERM, f
      #assert_equal E_PERM, g
      #assert_equal E_PERM, h

      # the new way...
      assert_equal E_PERM, e
      assert_equal E_INVARG, f
      assert_equal E_INVARG, g
      assert_equal E_INVARG, h
    end
  end

  def test_create_errors
    run_test_as('wizard') do
      assert_equal E_ARGS, create()
      assert_equal E_TYPE, create(1)
      assert_equal E_TYPE, create('1')
      assert_equal E_TYPE, create(1, 2)
      assert_equal E_TYPE, create(:object, '2')

      #assert_equal E_PERM, create(AMBIGUOUS_MATCH)
      #assert_equal E_PERM, create(FAILED_MATCH)
      #assert_equal E_PERM, create(invalid_object)
      assert_equal E_INVARG, create(AMBIGUOUS_MATCH)
      assert_equal E_INVARG, create(FAILED_MATCH)
      assert_equal E_INVARG, create(invalid_object)

      assert_equal E_TYPE, create([1])
      assert_equal E_TYPE, create(['1'])
      assert_equal E_INVARG, create([NOTHING])
      assert_equal E_INVARG, create([AMBIGUOUS_MATCH])
      assert_equal E_INVARG, create([FAILED_MATCH])
      assert_equal E_INVARG, create([invalid_object])

      # Test that If two objects define the same property by name, a
      # new object cannot be created using both of them as parents.

      a = create(NOTHING)
      b = create(NOTHING)

      add_property(a, 'foo', 123, [a, ''])
      add_property(b, 'foo', 'abc', [b, ''])

      max = max_object()
      assert_equal E_INVARG, create([a, b])
      assert_equal E_INVARG, create([b, a])
      assert_equal max, max_object()

      # Test that duplicate objects are not allowed.

      assert_equal E_INVARG, create([a, a])
      assert_equal E_INVARG, create([b, b])
    end

    # A variety of tests that check permissions.

    wizard = nil
    a = b = nil

    run_test_as('wizard') do
      wizard = player

      a = create(NOTHING)
      b = create(a, a)
      set(b, 'f', 1)

      assert_equal wizard, get(a, 'owner')
      assert_equal a, get(b, 'owner')
    end

    programmer = nil
    c = d = nil

    run_test_as('programmer') do
      programmer = player

      assert_equal E_PERM, create(a)

      #assert_equal E_PERM, create(b, invalid_object)
      #assert_equal E_PERM, create(b, wizard)
      #assert_equal E_PERM, create(b, NOTHING)
      assert_equal E_INVARG, create(b, invalid_object)
      assert_equal E_PERM, create(b, wizard)
      assert_equal E_PERM, create(b, NOTHING)

      assert_not_equal E_PERM, create(b)

      assert_equal E_PERM, create([a, b])
      assert_equal E_PERM, create([b, a])

      c = create(NOTHING)
      d = create(b, programmer)
      set(d, 'f', 1)

      assert_equal programmer, get(c, 'owner')
      assert_equal programmer, get(d, 'owner')
    end

    run_test_as('wizard') do
      assert_not_equal E_PERM, create(a)
      assert_not_equal E_PERM, create(b, wizard)
      assert_not_equal E_PERM, create(c, programmer)
      assert_not_equal E_PERM, create(d)
    end
  end

  def test_parent_chparent # migrated
    run_test_as('wizard') do
      a = create(NOTHING)
      b = create(NOTHING)
      c = create(NOTHING)

      assert_equal NOTHING, parent(a)
      assert_equal NOTHING, parent(b)
      assert_equal NOTHING, parent(c)

      chparent(a, b)
      chparent(b, c)

      assert_equal b, parent(a)
      assert_equal c, parent(b)
      assert_equal NOTHING, parent(c)

      assert_equal [], children(a)
      assert_equal a, children(b)
      assert_equal b, children(c)

      chparent(a, c)

      assert_equal c, parent(a)
      assert_equal c, parent(b)
      assert_equal NOTHING, parent(c)

      assert_equal [], children(a)
      assert_equal [], children(b)
      assert_equal [b, a], children(c)

      chparent(a, NOTHING)
      chparent(b, NOTHING)
      chparent(c, NOTHING)

      chparents(a, [b, c])
      chparents(b, [c])
      chparents(c, [])

      assert_equal [b, c], parents(a)
      assert_equal c, parents(b)
      assert_equal [], parents(c)

      assert_equal [], children(a)
      assert_equal a, children(b)
      assert_equal [a, b], children(c)

      assert_equal [b, c], ancestors(a)
      assert_equal c, ancestors(b)
      assert_equal [], ancestors(c)

      assert_equal [], descendants(a)
      assert_equal a, descendants(b)
      assert_equal [a, b], descendants(c)

      chparents(a, [c])

      assert_equal c, ancestors(a)
      assert_equal c, ancestors(b)
      assert_equal [], ancestors(c)

      assert_equal [], descendants(a)
      assert_equal [], descendants(b)
      assert_equal [b, a], descendants(c)

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

      # Test that if two objects define the same property by name, the
      # assigned property value (and info) is not preserved across
      # chparent.  Test both the single and multiple inheritance
      # cases.

      add_property(a, 'foo', 'foo', [a, 'c'])
      add_property(b, 'foo', 'foo', [b, ''])

      chparent(c, a)
      assert_equal [player, 'c'], property_info(c, 'foo')
      set(c, 'foo', 'bar')
      assert_equal 'bar', get(c, 'foo')
      chparent(c, b)
      assert_equal [b, ''], property_info(c, 'foo')
      assert_equal 'foo', get(c, 'foo')

      m = create(NOTHING)
      n = create(NOTHING)

      chparents(c, [m, n, a])
      assert_equal [player, 'c'], property_info(c, 'foo')
      set(c, 'foo', 'bar')
      assert_equal 'bar', get(c, 'foo')
      chparents(c, [b, m, n])
      assert_equal [b, ''], property_info(c, 'foo')
      assert_equal 'foo', get(c, 'foo')

      # Test that if a descendent object defines a property with the
      # same name as one defined either on the new parent or on one of
      # its ancestors, chparent/chparents fails.

      chparent c, NOTHING

      add_property(n, 'foo', 'foo', [n, 'rw'])

      # `a' and `b' define `foo' -- so does `n'

      chparent a, m
      assert_equal E_INVARG, chparent(m, n)

      chparent b, m
      assert_equal E_INVARG, chparent(m, n)

      assert_equal E_INVARG, chparents(m, [n])
      assert_equal E_INVARG, chparents(m, [n, c])
      assert_equal E_INVARG, chparents(m, [c, n])
      assert_not_equal E_INVARG, chparents(m, [c])

      delete_property(a, 'foo')
      delete_property(b, 'foo')
    end
  end

  def test_parent_chparent_errors # migrated
    run_test_as('wizard') do
      a = create(NOTHING)
      b = create(NOTHING)
      c = create(NOTHING)

      chparent a, b
      chparent b, c

      #assert_equal E_ARGS, parent()
      #assert_equal E_ARGS, parent(1, 2)
      #assert_equal E_TYPE, parent(1)
      #assert_equal E_TYPE, parent('1')
      assert_equal E_INVARG, parent(:nothing)
      assert_equal E_INVARG, parent(invalid_object)

      #assert_equal E_ARGS, chparent()
      #assert_equal E_ARGS, chparent(1)
      #assert_equal E_ARGS, chparent(1, 2, 3)
      #assert_equal E_TYPE, chparent(1, 2)
      #assert_equal E_TYPE, chparent(:object, 2)
      #assert_equal E_TYPE, chparent(:object, '2')
      assert_equal E_INVARG, chparent(NOTHING, :object)
      assert_equal E_INVARG, chparent(:object, invalid_object)
      assert_equal E_RECMOVE, chparent(:object, :object)
      assert_equal E_RECMOVE, chparent(c, a)

      assert_equal E_INVARG, chparents(:object, [:nothing])
      assert_equal E_INVARG, chparents(:object, [invalid_object])
      assert_equal E_RECMOVE, chparents(:object, [:object])
      assert_equal E_RECMOVE, chparents(c, [a, b])

      # Test that if two objects define the same property by name, a
      # new object cannot be created using both of them as parents.

      d = create(NOTHING)
      e = create(NOTHING)

      add_property(d, 'foo', 123, [d, ''])
      add_property(e, 'foo', 'abc', [e, ''])

      f = create(NOTHING)
      assert_equal E_INVARG, chparents(f, [d, e])

      # Test that duplicate objects are not allowed.

      assert_equal E_INVARG, chparents(f, [d, d])
      assert_equal E_INVARG, chparents(f, [e, e])
    end

    # A variety of tests that check permissions.

    wizard = nil
    a = b = nil

    run_test_as('wizard') do
      wizard = player

      a = create(NOTHING)
      b = create(a, a)
      set(b, 'f', 1)

      assert_equal wizard, get(a, 'owner')
      assert_equal a, get(b, 'owner')
    end

    programmer = nil
    c = d = nil

    run_test_as('programmer') do
      programmer = player

      c = create(NOTHING)
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
      e = create(NOTHING)

      assert_not_equal E_PERM, chparent(e, a)
      assert_not_equal E_PERM, chparent(e, b)
      assert_not_equal E_PERM, chparent(e, c)
      assert_not_equal E_PERM, chparent(e, d)
    end
  end

  def test_that_parent_maps_parents_to_parent # migrated
    run_test_as('programmer') do

      # Test that `parent()' performs the following mapping:
      #   [x, y, z] -> x
      #   [x] -> x
      #   [] -> #-1

      x = create(:nothing)
      y = create(:nothing)
      z = create(:nothing)
      a = create([x, y, z])
      b = create([x])
      c = create([])

      assert_equal [x, y, z], parents(a)
      assert_equal x, parents(b)
      assert_equal [], parents(c)

      assert_equal x, parent(a)
      assert_equal x, parent(b)
      assert_equal NOTHING, parent(c)
    end
  end

  def test_recycle
    run_test_as('wizard') do
      e = kahuna(NOTHING, NOTHING, 'e')
      b = kahuna(e, e, 'b')
      m = kahuna(b, b, 'm')
      n = kahuna(b, b, 'n')

      assert_equal NOTHING, parent(e)
      assert_equal e, parent(b)
      assert_equal b, parent(m)
      assert_equal b, parent(n)

      assert_equal b, children(e)
      assert_equal [m, n], children(b)
      assert_equal [], children(m)
      assert_equal [], children(n)

      assert_equal NOTHING, get(e, 'location')
      assert_equal e, get(b, 'location')
      assert_equal b, get(m, 'location')
      assert_equal b, get(n, 'location')

      assert_equal b, get(e, 'contents')
      assert_equal [m, n], get(b, 'contents')
      assert_equal [], get(m, 'contents')
      assert_equal [], get(n, 'contents')

      recycle(b)

      assert_equal NOTHING, parent(e)
      assert_equal E_INVARG, parent(b)
      assert_equal e, parent(m)
      assert_equal e, parent(n)

      assert_equal [m, n], children(e)
      assert_equal E_INVARG, children(b)
      assert_equal [], children(m)
      assert_equal [], children(n)

      assert_equal NOTHING, get(e, 'location')
      assert_equal E_INVIND, get(b, 'location')
      assert_equal NOTHING, get(m, 'location')
      assert_equal NOTHING, get(n, 'location')

      assert_equal [], get(e, 'contents')
      assert_equal E_INVIND, get(b, 'contents')
      assert_equal [], get(m, 'contents')
      assert_equal [], get(n, 'contents')

      assert_equal 'e', call(e, 'e')
      assert_equal 'e', call(m, 'e')
      assert_equal 'e', call(n, 'e')
      assert_equal 'm', call(m, 'm')
      assert_equal 'n', call(n, 'n')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')
      assert_equal 'm', get(m, 'm')
      assert_equal 'n', get(n, 'n')

      x = kahuna(NOTHING, NOTHING, 'x')
      y = kahuna(NOTHING, NOTHING, 'y')
      b = kahuna([x, y], NOTHING, 'b')
      m = kahuna(b, b, 'm')
      n = kahuna(b, b, 'n')

      assert_equal [], parents(x)
      assert_equal [], parents(y)
      assert_equal [x, y], parents(b)
      assert_equal b, parent(m)
      assert_equal b, parent(n)

      assert_equal b, children(x)
      assert_equal b, children(y)
      assert_equal [m, n], children(b)
      assert_equal [], children(m)
      assert_equal [], children(n)

      assert_equal NOTHING, get(x, 'location')
      assert_equal NOTHING, get(y, 'location')
      assert_equal NOTHING, get(b, 'location')
      assert_equal b, get(m, 'location')
      assert_equal b, get(n, 'location')

      assert_equal [], get(x, 'contents')
      assert_equal [], get(y, 'contents')
      assert_equal [m, n], get(b, 'contents')
      assert_equal [], get(m, 'contents')
      assert_equal [], get(n, 'contents')

      recycle(b)

      assert_equal NOTHING, parent(x)
      assert_equal NOTHING, parent(y)
      assert_equal E_INVARG, parents(b)
      assert_equal [x, y], parents(m)
      assert_equal [x, y], parents(n)

      assert_equal [m, n], children(x)
      assert_equal [m, n], children(y)
      assert_equal E_INVARG, children(b)
      assert_equal [], children(m)
      assert_equal [], children(n)

      assert_equal NOTHING, get(x, 'location')
      assert_equal NOTHING, get(y, 'location')
      assert_equal E_INVIND, get(b, 'location')
      assert_equal NOTHING, get(m, 'location')
      assert_equal NOTHING, get(n, 'location')

      assert_equal [], get(x, 'contents')
      assert_equal [], get(y, 'contents')
      assert_equal E_INVIND, get(b, 'contents')
      assert_equal [], get(m, 'contents')
      assert_equal [], get(n, 'contents')

      assert_equal 'x', call(x, 'x')
      assert_equal 'y', call(m, 'y')
      assert_equal 'y', call(n, 'y')
      assert_equal 'm', call(m, 'm')
      assert_equal 'n', call(n, 'n')

      assert_equal 'y', get(y, 'y')
      assert_equal 'x', get(m, 'x')
      assert_equal 'x', get(n, 'x')
      assert_equal 'm', get(m, 'm')
      assert_equal 'n', get(n, 'n')
    end
  end

  def test_that_recycling_a_parent_does_not_blow_away_all_parents # migrated
    run_test_as('programmer') do
      a = create([])
      b = create([])
      c = create([])

      m = create([a, b, c])

      assert_equal [a, b, c], parents(m)

      recycle(b)

      assert_equal [a, c], parents(m)
    end
  end

  def test_that_recycling_a_parent_merges_remaining_parents_correctly # migrated
    run_test_as('programmer') do
      a = create(:object)
      b = create([a])
      c = create([])

      d = create([])
      e = create(NOTHING)

      m = create([d, e])

      x = create([a, b, c, m])

      assert_equal [a, b, c, m], parents(x)

      recycle(b)

      assert_equal [a, c, m], parents(x)

      recycle(m)

      assert_equal [a, c, d, e], parents(x)

      recycle(e)

      assert_equal [a, c, d], parents(x)
    end
  end

  def test_renumber
    run_test_as('wizard') do
      recycle(create(:nothing)) # create a hole

      l = eval(%|for o in [#0..max_object()]; if (!valid(o)); return o; endif; endfor; return $nothing;|)

      a = create(NOTHING)
      b = create(a)
      c = create(b)

      move(b, a)
      move(c, b)

      assert_equal NOTHING, parent(a)
      assert_equal a, parent(b)
      assert_equal b, parent(c)

      assert_equal b, children(a)
      assert_equal c, children(b)
      assert_equal [], children(c)

      assert_equal NOTHING, get(a, 'location')
      assert_equal a, get(b, 'location')
      assert_equal b, get(c, 'location')

      assert_equal b, get(a, 'contents')
      assert_equal c, get(b, 'contents')
      assert_equal [], get(c, 'contents')

      assert_equal l, renumber(b)

      assert_equal NOTHING, parent(a)
      assert_equal E_INVARG, parent(b)
      assert_equal a, parent(l)
      assert_equal l, parent(c)

      assert_equal l, children(a)
      assert_equal E_INVARG, children(b)
      assert_equal c, children(l)
      assert_equal [], children(c)

      assert_equal NOTHING, get(a, 'location')
      assert_equal E_INVIND, get(b, 'location')
      assert_equal a, get(l, 'location')
      assert_equal l, get(c, 'location')

      assert_equal l, get(a, 'contents')
      assert_equal E_INVIND, get(b, 'contents')
      assert_equal c, get(l, 'contents')
      assert_equal [], get(c, 'contents')

      recycle(create(:nothing)) # create a hole

      l = eval(%|for o in [#0..max_object()]; if (!valid(o)); return o; endif; endfor; return $nothing;|)

      a = create(NOTHING)
      b = create(NOTHING)
      c = create(NOTHING)

      d = create([a, b])
      e = create([a, c])

      move(d, a)
      move(e, b)

      assert_equal [], parents(a)
      assert_equal [], parents(b)
      assert_equal NOTHING, parent(c)
      assert_equal [a, b], parents(d)
      assert_equal [a, c], parents(e)

      assert_equal [d, e], children(a)
      assert_equal d, children(b)
      assert_equal e, children(c)
      assert_equal [], children(d)
      assert_equal [], children(e)

      assert_equal NOTHING, get(a, 'location')
      assert_equal NOTHING, get(b, 'location')
      assert_equal NOTHING, get(c, 'location')
      assert_equal a, get(d, 'location')
      assert_equal b, get(e, 'location')

      assert_equal d, get(a, 'contents')
      assert_equal e, get(b, 'contents')
      assert_equal [], get(c, 'contents')
      assert_equal [], get(d, 'contents')
      assert_equal [], get(e, 'contents')

      assert_equal l, renumber(d)

      assert_equal NOTHING, parent(a)
      assert_equal [], parents(b)
      assert_equal [], parents(c)
      assert_equal E_INVARG, parents(d)
      assert_equal [a, b], parents(l)
      assert_equal [a, c], parents(e)

      assert_equal [l, e], children(a)
      assert_equal l, children(b)
      assert_equal e, children(c)
      assert_equal E_INVARG, children(d)
      assert_equal [], children(l)
      assert_equal [], children(e)

      assert_equal NOTHING, get(a, 'location')
      assert_equal NOTHING, get(b, 'location')
      assert_equal NOTHING, get(c, 'location')
      assert_equal E_INVIND, get(d, 'location')
      assert_equal a, get(l, 'location')
      assert_equal b, get(e, 'location')

      assert_equal l, get(a, 'contents')
      assert_equal e, get(b, 'contents')
      assert_equal [], get(c, 'contents')
      assert_equal E_INVIND, get(d, 'contents')
      assert_equal [], get(l, 'contents')
      assert_equal [], get(e, 'contents')
    end
  end

  def test_move
    run_test_as('wizard') do
      a = create(NOTHING)
      b = create(NOTHING)
      c = create(NOTHING)

      assert_equal NOTHING, get(a, 'location')
      assert_equal NOTHING, get(b, 'location')
      assert_equal NOTHING, get(c, 'location')

      assert_equal [], get(a, 'contents')
      assert_equal [], get(b, 'contents')
      assert_equal [], get(c, 'contents')

      move(b, a)
      move(c, b)

      assert_equal NOTHING, get(a, 'location')
      assert_equal a, get(b, 'location')
      assert_equal b, get(c, 'location')

      assert_equal b, get(a, 'contents')
      assert_equal c, get(b, 'contents')
      assert_equal [], get(c, 'contents')
    end
  end

  def test_pass # migrated
    run_test_as('wizard') do
      e = kahuna(NOTHING, NOTHING, 'e')
      b = kahuna(e, NOTHING, 'b')
      c = kahuna(b, NOTHING, 'c')

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

      chparents(b, [])

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

      assert_equal [b, e], parents(c)

      assert_equal E_VERBNF, call(c, 'goo')
    end
  end

  def test_verbs_and_invocation_and_inheritance # migrated
    run_test_as('wizard') do
      a = create(NOTHING)
      b = create(a)
      c = create(b)

      add_verb(a, ['player', 'xd', 'foo'], ['this', 'none', 'this'])
      set_verb_code(a, 'foo') do |vc|
        vc << %Q|return "foo";|
      end
      add_verb(a, ['player', 'xd', 'a'], ['this', 'none', 'this'])
      set_verb_code(a, 'a') do |vc|
        vc << %Q|return "a";|
      end

      add_verb(b, ['player', 'xd', 'b'], ['this', 'none', 'this'])
      set_verb_code(b, 'b') do |vc|
        vc << %Q|return "b";|
      end

      add_verb(c, ['player', 'xd', 'c'], ['this', 'none', 'this'])
      set_verb_code(c, 'c') do |vc|
        vc << %Q|return "c";|
      end

      assert_equal 'foo', call(a, 'foo')
      assert_equal 'foo', call(b, 'foo')
      assert_equal 'foo', call(c, 'foo')

      assert_equal 'a', call(a, 'a')
      assert_equal 'b', call(b, 'b')
      assert_equal 'c', call(c, 'c')

      chparent(c, a)
      chparent(b, NOTHING)
      chparent(a, b)

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
      chparent c, NOTHING
      chparent b, c

      e = create(NOTHING)
      add_verb(e, ['player', 'xd', 'bar'], ['this', 'none', 'this'])
      set_verb_code(e, 'bar') do |vc|
        vc << %Q|return verb;|
      end

      n = create([e])
      m = create([e, a])

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
  end

  def test_command_verbs_and_inheritance
    run_test_with_prefix_and_suffix_as('wizard') do
      a = create(NOTHING)
      b = create(a)
      c = create(b)
      d = create([here, c])

      add_verb(a, ['player', 'd', 'baz'], ['none', 'none', 'none'])
      set_verb_code(a, 'baz') do |vc|
        vc << %Q|notify(player, "baz");|
      end
      add_verb(b, ['player', 'd', 'bar'], ['none', 'none', 'none'])
      set_verb_code(b, 'bar') do |vc|
        vc << %Q|notify(player, "bar");|
      end
      add_verb(c, ['player', 'd', 'foo'], ['none', 'none', 'none'])
      set_verb_code(c, 'foo') do |vc|
        vc << %Q|notify(player, "foo");|
      end
      add_verb(d, ['player', 'd', 'qnz'], ['none', 'none', 'none'])
      set_verb_code(d, 'qnz') do |vc|
        vc << %Q|notify(player, "qnz");|
      end

      move(player, d)

      assert_equal 'foo', command(%Q|foo|)
      assert_equal 'bar', command(%Q|bar|)
      assert_equal 'baz', command(%Q|baz|)
      assert_equal 'qnz', command(%Q|qnz|)

      chparents(d, [c, here])

      assert_equal 'foo', command(%Q|foo|)
      assert_equal 'bar', command(%Q|bar|)
      assert_equal 'baz', command(%Q|baz|)
      assert_equal 'qnz', command(%Q|qnz|)
    end
  end

  def test_properties_and_inheritance # migrated
    run_test_as('programmer') do
      e = create(NOTHING)
      b = create(NOTHING)
      c = create(NOTHING)

      m = create(e)
      n = create(e)

      add_property(e, 'e1', 'e1', [player, ''])
      add_property(e, 'e', 'e', [player, ''])
      add_property(b, 'b1', ['b1'], [player, 'r'])
      add_property(b, 'b', ['b'], [player, 'r'])
      add_property(c, 'c', location, [player, 'w'])

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal E_PROPNF, property_info(e, 'b')
      assert_equal E_PROPNF, property_info(e, 'b1')
      assert_equal E_PROPNF, property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal E_PROPNF, property_info(n, 'b1')
      assert_equal E_PROPNF, property_info(n, 'b')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal E_PROPNF, get(e, 'b')
      assert_equal E_PROPNF, get(e, 'b1')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(n, 'e1')

      assert_equal 'ee', set(e, 'e', 'ee')
      assert_equal E_PROPNF, set(e, 'b', 'bb')
      assert_equal E_PROPNF, set(e, 'c', 'cc')
      assert_equal '11', set(m, 'e', '11')
      assert_equal 'ee', set(n, 'e', 'ee')

      assert_equal 'ee', get(e, 'e')
      assert_equal E_PROPNF, get(e, 'b')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal '11', get(m, 'e')
      assert_equal 'ee', get(n, 'e')

      assert_equal 'e', set(e, 'e', 'e')

      assert_equal 'e', get(e, 'e')
      assert_equal E_PROPNF, get(e, 'b')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal '11', get(m, 'e')
      assert_equal 'ee', get(n, 'e')

      clear_property(m, 'e')
      clear_property(n, 'e')

      assert chparent(e, b)

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal [player, 'r'], property_info(e, 'b')
      assert_equal [player, 'r'], property_info(e, 'b1')
      assert_equal E_PROPNF, property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal [player, 'r'], property_info(n, 'b1')
      assert_equal [player, 'r'], property_info(n, 'b')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'b', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(n, 'e1')

      assert chparent(e, c)

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal E_PROPNF, property_info(e, 'b')
      assert_equal E_PROPNF, property_info(e, 'b1')
      assert_equal [player, 'w'], property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal E_PROPNF, property_info(n, 'b1')
      assert_equal E_PROPNF, property_info(n, 'b')
      assert_equal [player, 'w'], property_info(m, 'c')
      assert_equal [player, 'w'], property_info(n, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal E_PROPNF, get(e, 'b')
      assert_equal E_PROPNF, get(e, 'b1')
      assert_equal location, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(n, 'e1')
      assert_equal location, get(m, 'c')
      assert_equal location, get(n, 'c')

      delete_property(c, 'c')
      add_property(c, 'c', 'c', [player, 'c'])

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal E_PROPNF, property_info(e, 'b')
      assert_equal E_PROPNF, property_info(e, 'b1')
      assert_equal [player, 'c'], property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal E_PROPNF, property_info(n, 'b1')
      assert_equal E_PROPNF, property_info(n, 'b')
      assert_equal [player, 'c'], property_info(m, 'c')
      assert_equal [player, 'c'], property_info(n, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal E_PROPNF, get(e, 'b')
      assert_equal E_PROPNF, get(e, 'b1')
      assert_equal 'c', get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(n, 'e1')
      assert_equal 'c', get(m, 'c')
      assert_equal 'c', get(n, 'c')

      assert_equal E_PROPNF, delete_property(m, 'e')
      assert_equal E_PROPNF, delete_property(n, 'e')

      delete_property(c, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal E_PROPNF, get(e, 'b')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')

      add_property(c, 'c', location, [player, 'w'])

      assert chparents(e, [b, c])

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal [player, 'r'], property_info(e, 'b')
      assert_equal [player, 'r'], property_info(e, 'b1')
      assert_equal [player, 'w'], property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, 'r'], property_info(m, 'b')
      assert_equal [player, 'r'], property_info(m, 'b1')
      assert_equal [player, 'w'], property_info(m, 'c')
      assert_equal [player, ''], property_info(n, 'e')
      assert_equal [player, ''], property_info(n, 'e1')
      assert_equal [player, 'r'], property_info(n, 'b')
      assert_equal [player, 'r'], property_info(n, 'b1')
      assert_equal [player, 'w'], property_info(n, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'b', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal location, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal 'b', get(m, 'b')
      assert_equal 'b', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal location, get(m, 'c')
      assert_equal location, get(n, 'c')

      assert_equal 'ee', set(e, 'e', 'ee')
      assert_equal 'bb', set(e, 'b', ['bb'])
      assert_equal location, set(e, 'c', location)
      assert_equal '11', set(m, 'e', '11')
      assert_equal 'ee', set(n, 'e', 'ee')
      assert_equal '22', set(m, 'b', ['22'])
      assert_equal 'bb', set(n, 'b',  ['bb'])
      assert_equal location, set(m, 'c', location)
      assert_equal location, set(n, 'c', location)

      assert_equal 'ee', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'bb', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal location, get(e, 'c')
      assert_equal '11', get(m, 'e')
      assert_equal 'ee', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal '22', get(m, 'b')
      assert_equal 'bb', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal location, get(m, 'c')
      assert_equal location, get(n, 'c')

      assert_equal 'e', set(e, 'e',  'e')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'bb', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal location, get(e, 'c')
      assert_equal '11', get(m, 'e')
      assert_equal 'ee', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal '22', get(m, 'b')
      assert_equal 'bb', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal location, get(m, 'c')
      assert_equal location, get(n, 'c')

      clear_property(m, 'e')
      clear_property(n, 'e')
      clear_property(m, 'b')
      clear_property(n, 'b')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'bb', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal location, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal 'bb', get(m, 'b')
      assert_equal 'bb', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal location, get(m, 'c')
      assert_equal location, get(n, 'c')

      assert_equal 'b', set(e, 'b',  ['b'])

      assert chparent(e, [c, b])

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal [player, 'r'], property_info(e, 'b')
      assert_equal [player, 'r'], property_info(e, 'b1')
      assert_equal [player, 'w'], property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, 'r'], property_info(m, 'b')
      assert_equal [player, 'r'], property_info(m, 'b1')
      assert_equal [player, 'w'], property_info(m, 'c')
      assert_equal [player, ''], property_info(n, 'e')
      assert_equal [player, ''], property_info(n, 'e1')
      assert_equal [player, 'r'], property_info(n, 'b')
      assert_equal [player, 'r'], property_info(n, 'b1')
      assert_equal [player, 'w'], property_info(n, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'b', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal location, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal 'b', get(m, 'b')
      assert_equal 'b', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal location, get(m, 'c')
      assert_equal location, get(n, 'c')

      assert_equal E_PROPNF, delete_property(m, 'e')
      assert_equal E_PROPNF, delete_property(n, 'e')

      delete_property(c, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'b', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal 'b', get(m, 'b')
      assert_equal 'b', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal E_PROPNF, get(m, 'c')
      assert_equal E_PROPNF, get(n, 'c')

      r = create([])

      add_property(r, 'rrrr', 'rrrr', [player, ''])
      add_property(r, 'rrr', 'rrr', [player, 'r'])
      add_property(r, 'rr', 'rr', [player, 'w'])

      assert chparents(e, [b, r, c])

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal [player, 'r'], property_info(e, 'b')
      assert_equal [player, 'r'], property_info(e, 'b1')
      assert_equal [player, 'w'], property_info(e, 'rr')
      assert_equal [player, 'r'], property_info(e, 'rrr')
      assert_equal [player, ''], property_info(e, 'rrrr')
      assert_equal E_PROPNF, property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, 'r'], property_info(m, 'b')
      assert_equal [player, 'r'], property_info(m, 'b1')
      assert_equal [player, 'w'], property_info(m, 'rr')
      assert_equal [player, 'r'], property_info(m, 'rrr')
      assert_equal [player, ''], property_info(m, 'rrrr')
      assert_equal E_PROPNF, property_info(m, 'c')
      assert_equal [player, ''], property_info(n, 'e')
      assert_equal [player, ''], property_info(n, 'e1')
      assert_equal [player, 'r'], property_info(n, 'b')
      assert_equal [player, 'r'], property_info(n, 'b1')
      assert_equal [player, 'w'], property_info(n, 'rr')
      assert_equal [player, 'r'], property_info(n, 'rrr')
      assert_equal [player, ''], property_info(n, 'rrrr')
      assert_equal E_PROPNF, property_info(n, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'b', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal 'rr', get(e, 'rr')
      assert_equal 'rrr', get(e, 'rrr')
      assert_equal 'rrrr', get(e, 'rrrr')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal 'b', get(m, 'b')
      assert_equal 'b', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal 'rr', get(m, 'rr')
      assert_equal 'rrr', get(m, 'rrr')
      assert_equal 'rrrr', get(m, 'rrrr')
      assert_equal 'rr', get(n, 'rr')
      assert_equal 'rrr', get(n, 'rrr')
      assert_equal 'rrrr', get(n, 'rrrr')
      assert_equal E_PROPNF, get(m, 'c')
      assert_equal E_PROPNF, get(n, 'c')

      delete_property(r, 'rrr')

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal [player, 'r'], property_info(e, 'b')
      assert_equal [player, 'r'], property_info(e, 'b1')
      assert_equal [player, 'w'], property_info(e, 'rr')
      assert_equal E_PROPNF, property_info(e, 'rrr')
      assert_equal [player, ''], property_info(e, 'rrrr')
      assert_equal E_PROPNF, property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, 'r'], property_info(m, 'b')
      assert_equal [player, 'r'], property_info(m, 'b1')
      assert_equal [player, 'w'], property_info(m, 'rr')
      assert_equal E_PROPNF, property_info(m, 'rrr')
      assert_equal [player, ''], property_info(m, 'rrrr')
      assert_equal E_PROPNF, property_info(m, 'c')
      assert_equal [player, ''], property_info(n, 'e')
      assert_equal [player, ''], property_info(n, 'e1')
      assert_equal [player, 'r'], property_info(n, 'b')
      assert_equal [player, 'r'], property_info(n, 'b1')
      assert_equal [player, 'w'], property_info(n, 'rr')
      assert_equal E_PROPNF, property_info(n, 'rrr')
      assert_equal [player, ''], property_info(n, 'rrrr')
      assert_equal E_PROPNF, property_info(n, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'b', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal 'rr', get(e, 'rr')
      assert_equal E_PROPNF, get(e, 'rrr')
      assert_equal 'rrrr', get(e, 'rrrr')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal 'b', get(m, 'b')
      assert_equal 'b', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal 'rr', get(m, 'rr')
      assert_equal E_PROPNF, get(m, 'rrr')
      assert_equal 'rrrr', get(m, 'rrrr')
      assert_equal 'rr', get(n, 'rr')
      assert_equal E_PROPNF, get(n, 'rrr')
      assert_equal 'rrrr', get(n, 'rrrr')
      assert_equal E_PROPNF, get(m, 'c')
      assert_equal E_PROPNF, get(n, 'c')

      assert chparents(e, [c, b])

      assert_equal [player, ''], property_info(e, 'e')
      assert_equal [player, ''], property_info(e, 'e1')
      assert_equal [player, 'r'], property_info(e, 'b')
      assert_equal [player, 'r'], property_info(e, 'b1')
      assert_equal E_PROPNF, property_info(e, 'rr')
      assert_equal E_PROPNF, property_info(e, 'rrr')
      assert_equal E_PROPNF, property_info(e, 'rrrr')
      assert_equal E_PROPNF, property_info(e, 'c')
      assert_equal [player, ''], property_info(m, 'e')
      assert_equal [player, ''], property_info(m, 'e1')
      assert_equal [player, 'r'], property_info(m, 'b')
      assert_equal [player, 'r'], property_info(m, 'b1')
      assert_equal E_PROPNF, property_info(m, 'rr')
      assert_equal E_PROPNF, property_info(m, 'rrr')
      assert_equal E_PROPNF, property_info(m, 'rrrr')
      assert_equal E_PROPNF, property_info(m, 'c')
      assert_equal [player, ''], property_info(n, 'e')
      assert_equal [player, ''], property_info(n, 'e1')
      assert_equal [player, 'r'], property_info(n, 'b')
      assert_equal [player, 'r'], property_info(n, 'b1')
      assert_equal E_PROPNF, property_info(n, 'rr')
      assert_equal E_PROPNF, property_info(n, 'rrr')
      assert_equal E_PROPNF, property_info(n, 'rrrr')
      assert_equal E_PROPNF, property_info(n, 'c')

      assert_equal 'e', get(e, 'e')
      assert_equal 'e1', get(e, 'e1')
      assert_equal 'b', get(e, 'b')
      assert_equal 'b1', get(e, 'b1')
      assert_equal E_PROPNF, get(e, 'rr')
      assert_equal E_PROPNF, get(e, 'rrr')
      assert_equal E_PROPNF, get(e, 'rrrr')
      assert_equal E_PROPNF, get(e, 'c')
      assert_equal 'e', get(m, 'e')
      assert_equal 'e', get(n, 'e')
      assert_equal 'e1', get(m, 'e1')
      assert_equal 'e1', get(n, 'e1')
      assert_equal 'b', get(m, 'b')
      assert_equal 'b', get(n, 'b')
      assert_equal 'b1', get(m, 'b1')
      assert_equal 'b1', get(n, 'b1')
      assert_equal E_PROPNF, get(m, 'rr')
      assert_equal E_PROPNF, get(m, 'rrr')
      assert_equal E_PROPNF, get(m, 'rrrr')
      assert_equal E_PROPNF, get(n, 'rr')
      assert_equal E_PROPNF, get(n, 'rrr')
      assert_equal E_PROPNF, get(n, 'rrrr')
      assert_equal E_PROPNF, get(m, 'c')
      assert_equal E_PROPNF, get(n, 'c')
    end
  end

  def test_leak_memory
    run_test_as('wizard') do
      simplify command %Q|; return #1.x;|
      simplify command %Q|; return #1.x;|
      simplify command %Q|; return #1.x;|
      simplify command %Q|; return #1.name;|
    end
  end

  def test_that_adding_and_deleting_properties_works_with_multiple_inheritance # migrated
    run_test_as('programmer') do
      a = create(NOTHING)
      add_property(a, 'x', 1, [player, ''])
      add_property(a, 'y', 2, [player, ''])
      add_property(a, 'z', 3, [player, ''])

      b = create(a)
      add_property(b, 'm', 4, [player, ''])
      add_property(b, 'n', 5, [player, ''])

      c = create(b)
      add_property(c, 'c', 6, [player, ''])

      d = create([a, c])

      assert_equal 1, get(d, 'x')
      assert_equal 2, get(d, 'y')
      assert_equal 3, get(d, 'z')
      assert_equal 4, get(d, 'm')
      assert_equal 5, get(d, 'n')
      assert_equal 6, get(d, 'c')

      assert_equal 1, get(c, 'x')
      assert_equal 2, get(c, 'y')
      assert_equal 3, get(c, 'z')
      assert_equal 4, get(c, 'm')
      assert_equal 5, get(c, 'n')
      assert_equal 6, get(c, 'c')

      delete_property(a, 'y')

      assert_equal 1, get(d, 'x')
      assert_equal E_PROPNF, get(d, 'y')
      assert_equal 3, get(d, 'z')
      assert_equal 4, get(d, 'm')
      assert_equal 5, get(d, 'n')
      assert_equal 6, get(d, 'c')

      assert_equal 1, get(c, 'x')
      assert_equal E_PROPNF, get(c, 'y')
      assert_equal 3, get(c, 'z')
      assert_equal 4, get(c, 'm')
      assert_equal 5, get(c, 'n')
      assert_equal 6, get(c, 'c')

      delete_property(b, 'm')

      assert_equal 1, get(d, 'x')
      assert_equal E_PROPNF, get(d, 'y')
      assert_equal 3, get(d, 'z')
      assert_equal E_PROPNF, get(d, 'm')
      assert_equal 5, get(d, 'n')
      assert_equal 6, get(d, 'c')

      assert_equal 1, get(c, 'x')
      assert_equal E_PROPNF, get(c, 'y')
      assert_equal 3, get(c, 'z')
      assert_equal E_PROPNF, get(c, 'm')
      assert_equal 5, get(c, 'n')
      assert_equal 6, get(c, 'c')

      add_property(a, 's', 7, [player, ''])

      assert_equal 1, get(d, 'x')
      assert_equal 3, get(d, 'z')
      assert_equal 5, get(d, 'n')
      assert_equal 6, get(d, 'c')
      assert_equal 7, get(d, 's')

      assert_equal 1, get(c, 'x')
      assert_equal 3, get(c, 'z')
      assert_equal 5, get(c, 'n')
      assert_equal 6, get(c, 'c')
      assert_equal 7, get(c, 's')

      add_property(b, 't', 8, [player, ''])

      assert_equal 1, get(d, 'x')
      assert_equal 3, get(d, 'z')
      assert_equal 5, get(d, 'n')
      assert_equal 6, get(d, 'c')
      assert_equal 7, get(d, 's')
      assert_equal 8, get(d, 't')

      assert_equal 1, get(c, 'x')
      assert_equal 3, get(c, 'z')
      assert_equal 5, get(c, 'n')
      assert_equal 6, get(c, 'c')
      assert_equal 7, get(c, 's')
      assert_equal 8, get(c, 't')
    end
  end

  def test_clear_properties
    x = nil

    run_test_as('programmer') do
      x = create(NOTHING)
      set(x, 'f', 1)

      a = create(x)

      add_property(x, 'x', 'x', [player, 'rc'])
      assert_equal [player, 'rc'], property_info(a, 'x')
      assert_equal 'x', get(a, 'x')

      b = create(NOTHING)
      chparent(b, x)
      assert_equal [player, 'rc'], property_info(b, 'x')
      assert_equal 'x', get(b, 'x')

      set(b, 'x', ['x'])
      assert_equal 'x', get(b, 'x')

      c = create(x)
      assert_equal [player, 'rc'], property_info(c, 'x')
      assert_equal 'x', get(c, 'x')

      set_property_info(c, 'x', [player, ''])
      assert_equal [player, ''], property_info(c, 'x')
    end

    run_test_as('programmer') do
      a = create(x)
      assert_equal [player, 'rc'], property_info(a, 'x')
      assert_equal 'x', get(a, 'x')

      set(a, 'x', ['x'])
      assert_equal 'x', get(a, 'x')

      b = create(NOTHING)
      chparent(b, x)
      assert_equal [player, 'rc'], property_info(b, 'x')
      assert_equal 'x', get(b, 'x')

      set_property_info(b, 'x', [player, ''])
      assert_equal [player, ''], property_info(b, 'x')
    end
  end

  def test_ancestors_and_descendants
    run_test_as('programmer') do
      a = create(NOTHING)
      b = create(a)
      c = create(b)
      d = create(c)
      e = create(d)

      assert_equal d, parent(e)
      assert_equal c, parent(d)
      assert_equal b, parent(c)
      assert_equal a, parent(b)
      assert_equal NOTHING, parent(a)

      assert_equal [], ancestors(a)
      assert_equal a, ancestors(b)
      assert_equal [b, a], ancestors(c)
      assert_equal [c, b, a], ancestors(d)
      assert_equal [d, c, b, a], ancestors(e)

      assert_equal b, children(a)
      assert_equal c, children(b)
      assert_equal d, children(c)
      assert_equal e, children(d)
      assert_equal [], children(e)

      assert_equal [b, c, d, e], descendants(a)
      assert_equal [c, d, e], descendants(b)
      assert_equal [d, e], descendants(c)
      assert_equal e, descendants(d)
      assert_equal [], descendants(e)

      assert_equal E_RECMOVE, chparent(a, e)
      assert_equal E_RECMOVE, chparent(a, d)
      assert_equal 0, chparent(d, NOTHING)
      assert_equal 0, chparent(a, e)

      assert_equal e, parent(a)
      assert_equal a, parent(b)
      assert_equal b, parent(c)
      assert_equal NOTHING, parent(d)
      assert_equal d, parent(e)

      assert_equal [e, d], ancestors(a)
      assert_equal [a, e, d], ancestors(b)
      assert_equal [b, a, e, d], ancestors(c)
      assert_equal [], ancestors(d)
      assert_equal d, ancestors(e)

      assert_equal b, children(a)
      assert_equal c, children(b)
      assert_equal [], children(c)
      assert_equal e, children(d)
      assert_equal a, children(e)

      assert_equal [b, c], descendants(a)
      assert_equal c, descendants(b)
      assert_equal [], descendants(c)
      assert_equal [e, a, b, c], descendants(d)
      assert_equal [a, b, c], descendants(e)

      recycle(a)

      assert_equal E_INVARG, parent(a)
      assert_equal e, parent(b)
      assert_equal b, parent(c)
      assert_equal NOTHING, parent(d)
      assert_equal d, parent(e)

      assert_equal E_INVARG, ancestors(a)
      assert_equal [e, d], ancestors(b)
      assert_equal [b, e, d], ancestors(c)
      assert_equal [], ancestors(d)
      assert_equal d, ancestors(e)

      assert_equal E_INVARG, children(a)
      assert_equal c, children(b)
      assert_equal [], children(c)
      assert_equal e, children(d)
      assert_equal b, children(e)

      assert_equal E_INVARG, descendants(a)
      assert_equal c, descendants(b)
      assert_equal [], descendants(c)
      assert_equal [e, b, c], descendants(d)
      assert_equal [b, c], descendants(e)
    end
  end

  def test_various_things_that_can_go_wrong
    run_test_as('programmer') do
      a = create(NOTHING)
      b = create(NOTHING)
      c = create(NOTHING)

      m = create(a)
      n = create(b)

      z = create(NOTHING)

      add_property(a, 'foo', 0, [player, ''])
      add_property(b, 'foo', 0, [player, ''])
      add_property(c, 'foo', 0, [player, ''])

      add_property(m, 'bar', 0, [player, ''])
      add_property(n, 'baz', 0, [player, ''])

      add_property(z, 'bar', 0, [player, ''])

      assert_equal E_INVARG, chparent(a, b)
      assert_equal E_INVARG, chparent(a, c)
      assert_equal E_INVARG, chparent(a, z)

      assert_equal 0, chparent(c, z)
      assert_equal 0, chparent(c, NOTHING)

      assert_equal 0, chparent(z, c)
      assert_equal 0, chparent(z, NOTHING)

      assert_equal 0, chparents(c, [z])
      assert_equal 0, chparents(c, [])

      assert_equal 0, chparents(z, [c])
      assert_equal 0, chparents(z, [])

      assert_equal E_INVARG, chparents(c, [a, z])
      assert_equal E_INVARG, chparents(c, [b, z])
      assert_equal E_INVARG, chparents(c, [m, z])
      assert_equal E_INVARG, chparents(c, [n, z])

      assert_equal E_INVARG, chparents(z, [a, b])
      assert_equal E_INVARG, chparents(z, [b, c])
      assert_equal E_INVARG, chparents(z, [m, n])
    end
  end

  def test_ways_of_specifying_nothing
    run_test_as('programmer') do
      a = create([])
      b = create(NOTHING)
      assert_equal E_INVARG, create([NOTHING])

      m = create(a)
      n = create([b])

      assert_equal NOTHING, parent(a)
      assert_equal NOTHING, parent(b)
      assert_equal [], parents(a)
      assert_equal [], parents(b)
      assert_equal a, parent(m)
      assert_equal b, parent(n)

      x = kahuna(NOTHING, NOTHING, 'x')
      recycle(x)

      assert_equal E_INVARG, create(x)
      assert_equal E_INVARG, create([x])
    end
  end

  def test_for_identified_but_unfixed_bugs
    who = nil
    run_test_as('programmer') do
      who = player
    end
    run_test_as('wizard') do
      # Test that creating a `c' property ignores the specified owner.
      # I contend that this is a bug on the basis of the "least surprise"
      # rule -- I understand why this happens, but ignoring the specified
      # owner is surprising!
      a = create(NOTHING)
      assert_equal player, get(a, 'owner')
      add_property(a, 'foo', 0, [who, 'r'])
      add_property(a, 'bar', '', [who, 'rc'])
      assert_equal [who, 'r'], property_info(a, 'foo')
      assert_equal [player, 'rc'], property_info(a, 'bar')
    end
  end

  def test_verb_cache # migrated
    run_test_as('wizard') do
      vcs = verb_cache_stats
      unless (vcs[0] == 0 && vcs[1] == 0 && vcs[2] == 0 && vcs[3] == 0)
        s = create(NOTHING);
        t = create(NOTHING);
        m = create(s);
        n = create(t);
        a = create([m, n])

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

  def test_that_descendants_returns_an_empty_list_if_an_object_has_no_children
    run_test_as('programmer') do
      o = create(:nothing)
      assert_equal [], descendants(o)
    end
  end

  def test_that_ancestors_returns_an_empty_list_if_an_object_has_no_children
    run_test_as('programmer') do
      o = create(:nothing)
      assert_equal [], ancestors(o)
    end
  end

  def test_that_ancestors_serializes_the_inheritance_graph_correctly
    run_test_as('programmer') do
      o = create(:nothing)
      a = create(o)
      b = create(o)
      c = create(o)
      m = create([a, b])
      n = create([b, c])
      z = create([m, n])
      assert_equal [m, a, o, b, n, c], ancestors(z)
    end
  end

  def test_that_descendants_serializes_the_inheritance_graph_correctly
    run_test_as('programmer') do
      o = create(:nothing)
      a = create(o)
      b = create(o)
      c = create(o)
      m = create([a, b])
      n = create([b, c])
      z = create([m, n])
      assert_equal [a, m, z, b, n, c], descendants(o)
    end
  end

  def test_that_ancestors_includes_the_object_if_the_second_argument_is_true
    run_test_as('programmer') do
      o = create(:nothing)
      a = create(o)
      b = create(o)
      c = create(o)
      m = create([a, b])
      n = create([b, c])
      z = create([m, n])
      assert_equal [z, m, a, o, b, n, c], ancestors(z, 1)
    end
  end

  def test_that_descendants_includes_the_object_if_the_second_argument_is_true
    run_test_as('programmer') do
      o = create(:nothing)
      a = create(o)
      b = create(o)
      c = create(o)
      m = create([a, b])
      n = create([b, c])
      z = create([m, n])
      assert_equal [o, a, m, z, b, n, c], descendants(o, 1)
    end
  end

  def test_that_ancestors_does_not_include_the_object_if_the_second_argument_is_false
    run_test_as('programmer') do
      o = create(:nothing)
      a = create(o)
      b = create(o)
      c = create(o)
      m = create([a, b])
      n = create([b, c])
      z = create([m, n])
      assert_equal [m, a, o, b, n, c], ancestors(z, 0)
    end
  end

  def test_that_descendants_does_not_include_the_object_if_the_second_argument_is_false
    run_test_as('programmer') do
      o = create(:nothing)
      a = create(o)
      b = create(o)
      c = create(o)
      m = create([a, b])
      n = create([b, c])
      z = create([m, n])
      assert_equal [a, m, z, b, n, c], descendants(o, 0)
    end
  end

  private

  def kahuna(parent, location, name)
    object = create(parent)
    move(object, location)
    set(object, 'name', name)
    add_property(object, name, name, [player, ''])
    add_verb(object, [player, 'xd', name], ['this', 'none', 'this'])
    set_verb_code(object, name) do |vc|
      vc << %|return this.#{name};|
    end
    object
  end

end
