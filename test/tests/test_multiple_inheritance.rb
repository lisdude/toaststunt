require 'test_helper'

# Regression tests for crashes and corruption specific to multiple
# inheritance -- objects whose `parents' is a list rather than a single
# object.  Every one of these took the server down (or silently forged a
# Var) before the fixes in db_properties.cc / db_objects.cc / db_verbs.cc.
class TestMultipleInheritance < Test::Unit::TestCase

  # A "diamond": d reaches a through both b and c.  Re-parenting a used to
  # rebuild d's *pre-change* ancestor list out of c's *post-change*
  # ancestry, which sent the property fixup off the end of d's propval.
  def test_that_chparent_above_a_diamond_does_not_crash
    run_test_as('programmer') do
      a = create(NOTHING); b = create(a); c = create(a)
      d = create(NOTHING)
      assert_equal 0, chparents(d, [b, c])

      p = create(NOTHING)
      add_property(p, 'pp', 42, [player, 'r'])

      assert_equal 0, chparent(a, p)

      assert_equal [b, c], parents(d)
      assert ancestors(d).include?(p)
      assert_equal 42, simplify(command("; return #{d}.pp;"))
      assert_equal 42, simplify(command("; return #{c}.pp;"))
    end
  end

  # Same shape with properties on both sides of the move.  That is the
  # variant that used to hand back forged Vars rather than crash: an
  # ancestor list would come back containing a string.
  def test_that_a_diamond_survives_a_chparent_that_changes_the_property_count
    run_test_as('programmer') do
      a = create(NOTHING); add_property(a, 'pa', 1, [player, 'rc'])
      b = create(a);       add_property(b, 'pb', 2, [player, 'rc'])
      c = create(NOTHING)
      assert_equal 0, chparents(c, [a, b])

      d = create(NOTHING); add_property(d, 'pd', 3, [player, 'rc'])
      assert_equal 0, chparent(a, d)

      ancestors(c).each { |x| assert_kind_of MooObj, x }
      assert_equal 1, simplify(command("; return #{c}.pa;"))
      assert_equal 2, simplify(command("; return #{c}.pb;"))
      assert_equal 3, simplify(command("; return #{c}.pd;"))
    end
  end

  # `j = i + i' in check_for_duplicates() let {a,b,b} through.
  def test_that_duplicate_parents_are_rejected_at_any_position
    run_test_as('programmer') do
      a = create(NOTHING); b = create(NOTHING); c = create(NOTHING)
      x = create(NOTHING)

      assert_equal E_INVARG, chparents(x, [a, a])
      assert_equal E_INVARG, chparents(x, [a, b, a])
      assert_equal E_INVARG, chparents(x, [a, b, b])
      assert_equal E_INVARG, chparents(x, [a, b, c, c])
      assert_equal E_INVARG, chparents(x, [a, b, c, b])

      assert_equal 0, chparents(x, [a, b, c])
      assert_equal [a, b, c], parents(x)
    end
  end

  # Once duplicates were accepted, renumber() stored one Var past the end of
  # a children list and left a stale objid behind in the child's parents.
  def test_that_renumber_keeps_a_multi_parent_hierarchy_consistent
    run_test_as('wizard') do
      hole = create(NOTHING)
      q = create(NOTHING); p = create(NOTHING)
      x = create(NOTHING)
      assert_equal 0, chparents(x, [q, p])
      recycle(hole)                     # free up a lower object number

      renumber(p)

      parents(x).each { |z| assert_equal true, valid(z) }
      assert_equal 2, parents(x).length
      assert_equal 0, chparents(x, [])
      assert_equal [], parents(x)
    end
  end

  # bf_recreate() never checked its parent the way bf_create() does.
  def test_that_recreate_rejects_an_invalid_parent
    run_test_as('programmer') do
      x = create(NOTHING)
      y = create(NOTHING)
      recycle(y)

      assert_equal E_INVARG, simplify(command("; return recreate(#{y}, #99999);"))
      assert_equal y, simplify(command("; return recreate(#{y}, #{x});"))
      assert_equal x, parent(y)
    end
  end

  # Several hierarchy walks followed every *path* through the graph rather
  # than every *node*, which is exponential once diamonds stack up -- inside
  # builtins that the task timeout cannot preempt.  Rather than pin a
  # wall-clock budget, check how the cost grows: eight more diamond levels
  # multiply a path-following walk by 256 and a node-following walk by one.
  def test_that_hierarchy_walks_do_not_grow_exponentially
    run_test_as('wizard') do
      shallow = walk_seconds(stacked_diamonds(8))
      deep    = walk_seconds(stacked_diamonds(16))
      budget  = [shallow * 8, 0.05].max
      assert deep < budget,
             "hierarchy walks cost #{shallow}s at 8 diamond levels and #{deep}s at 16"
    end
  end

  # isa() is the starkest of them: it was over a second at fourteen levels.
  def test_that_isa_does_not_blow_up_on_stacked_diamonds
    run_test_as('programmer') do
      l = stacked_diamonds(16)
      elapsed = simplify(command("; x = #{l}; t = ftime(); r = `isa(x, #6) ! ANY'; return ftime() - t;"))
      assert elapsed < 0.5, "isa() over 16 stacked diamonds took #{elapsed}s"
    end
  end

  private

  # Builds `depth' stacked diamonds and returns the bottom object.
  def stacked_diamonds(depth)
    l = simplify(command("; l = #1; for i in [1..#{depth}]; a = create(l); b = create(l); l = create({a, b}); endfor; return l;"))
    assert_kind_of MooObj, l
    l
  end

  # One pass over every walk that used to follow paths instead of nodes:
  # the chparent property check and verb lookup (via create), callable-verb
  # lookup, isa(), and the descendant scan add_property does.
  def walk_seconds(o)
    simplify(command(<<-MOO))
      ; x = #{o}; t = ftime();
        c = create(x);
        r = `x:nosuchverb() ! ANY';
        s = `isa(x, #6) ! ANY';
        `add_property(x, "mi_probe", 1, {player, "rc"}) ! ANY';
        `delete_property(x, "mi_probe") ! ANY';
        return ftime() - t;
    MOO
  end
end
