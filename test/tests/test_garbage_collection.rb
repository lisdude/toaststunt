require 'test_helper'

class TestGarbageCollection < Test::Unit::TestCase

  def setup
    run_test_as('programmer') do
      @obj = simplify(command(%Q|; o = create($nothing); o.r = 1; o.w = 1; return o;|))
    end
  end

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

  def test_that_run_gc_requires_wizard_perms
    run_test_as('programmer') do
      assert_equal E_PERM, run_gc
    end
    run_test_as('wizard') do
      assert_not_equal E_PERM, run_gc
    end
  end

  def test_that_gc_stats_requires_wizard_perms
    run_test_as('programmer') do
      assert_equal E_PERM, gc_stats
    end
    run_test_as('wizard') do
      assert_not_equal E_PERM, gc_stats
    end
  end

  def drain
    while (gc = gc_stats)["purple"] != 0 || gc["black"] != 0
      run_gc
    end
  end

  def test_that_a_nested_list_doesnt_create_a_possible_root
    run_test_as('wizard') do
      drain
      simplify(command("; {1, {2, {3}, 4}, 5};"))
      gc = gc_stats
      assert_equal 0, gc["purple"]
      assert_equal 0, gc["black"]
    end
  end

  def test_that_a_nested_map_doesnt_create_a_possible_root
    run_test_as('wizard') do
      drain
      simplify(command("; [1 -> [4 -> [5 -> 6], 7 -> 8], 2 -> 3];"))
      gc = gc_stats
      assert_equal 0, gc["purple"]
      assert_equal 0, gc["black"]
    end
  end

  def test_that_a_nested_list_with_an_anonymous_object_does_create_a_possible_root
    # the outermost list is indicated possibly cyclic because
    # the anonymous object _could_ be mutated to point to it
    run_test_as('wizard') do
      drain
      simplify(command("; {1, {2, {3, create($anonymous, 1)}, 4}, 5};"))
      gc = gc_stats
      assert_equal 0, gc["purple"]
      assert gc["black"] > 0
    end
  end

  def test_that_a_nested_map_with_an_anonymous_object_does_create_a_possible_root
    # the outermost map is indicated possibly cyclic because
    # the anonymous object _could_ be mutated to point to it
    run_test_as('wizard') do
      drain
      simplify(command("; [1 -> [4 -> [5 -> create($anonymous, 1)], 7 -> 8], 2 -> 3];"))
      gc = gc_stats
      assert_equal 0, gc["purple"]
      assert gc["black"] > 0
    end
  end

  ## tests meant to be run while running valgrind

  def test_that_this_doesnt_crash_the_server
    run_test_as('wizard') do
      simplify(command(%Q|; create($nothing, 1); run_gc();|))
    end
  end

  def test_that_this_doesnt_leak_memory
    run_test_as('wizard') do
      a = create([])
      b = create([])
      c = create([])
      d = create([])
      m = create([a, b, c], 1)
      assert_equal [_(a), _(b), _(c)], parents(m)
      recycle(a)
      valid(m)
    end
  end

  def test_that_running_the_garbage_collector_destroys_values_with_cyclic_references
    run_test_as('wizard') do
      a = create(:object)
      add_property(a, 'next', 0, [player, ''])
      add_property(a, 'recycle_called', 0, [player, ''])
      add_verb(a, ['player', 'xd', 'recycle'], ['this', 'none', 'this'])
      set_verb_code(a, 'recycle') do |vc|
        vc << %Q<typeof(this) == ANON || raise(E_INVARG);>
        vc << %Q<#{a}.recycle_called = #{a}.recycle_called + 1;>
      end
      simplify(command("; run_gc();"))
      assert_equal 0, get(a, 'recycle_called')
      simplify(command("; x = create(#{a}, 1); x.next = create(#{a}, 1); x.next.next = x; x = 0; run_gc();"))
      assert_equal 2, get(a, 'recycle_called')
      simplify(command("; x = {create(#{a}, 1)}; x[1].next = x; x = 0; run_gc();"))
      assert_equal 3, get(a, 'recycle_called')
      simplify(command("; x = [1 -> create(#{a}, 1)]; x[1].next = x; x = 0; run_gc();"))
      assert_equal 4, get(a, 'recycle_called')
    end
  end

  def test_the_garbage_collector_by_fuzzing_1
    run_test_as('wizard') do
      a = create(:object, 0)
      simplify(command(%Q|; add_property(#{a}, "next", #{a}, {player, ""}); |))
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        lines = <<-EOF
          r = o = #{a}.next = create($nothing, 1);
          for i in [1..1000];
            ticks_left() < 1000 || seconds_left() < 2 && suspend(0);
            if ((e = random(8)) == 1)
              n = create($nothing, 1);
              add_property(n, "next", o, {player, ""});
              o = n;
            elseif (e == 2)
              n = create($nothing, 1);
              add_property(n, "foo", o, {player, ""});
              add_property(n, "bar", o, {player, ""});
              o = n;
            elseif (e == 3)
              n = create(#{a}, 1);
              n.next = o;
              o = n;
            elseif (e == 4)
              o = {o};
            elseif (e == 5)
              o = {o, o};
            elseif (e == 6)
              o = [1 -> o];
            elseif (e == 7)
              o = [1 -> o, 2 -> o];
            endif
          endfor;
          add_property(r, "next", o, {player, ""});
          #{a}.next = 0;
        EOF
        lines.split("\n").each do |line|
          vc << line
        end
      end
      call(a, 'go')
      run_gc()
    end
  end

  def test_the_garbage_collector_by_fuzzing_2
    run_test_as('wizard') do
      a = create(:object, 0)
      simplify(command(%Q|; add_property(#{a}, "next", #{a}, {player, ""}); |))
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        lines = <<-EOF
          r = o = #{a}.next = create($nothing, 1);
          for i in [1..1000];
            ticks_left() < 1000 || seconds_left() < 2 && suspend(0);
            if ((e = random(8)) == 1)
              n = create($nothing, 1);
              add_property(n, "next", o, {player, ""});
              o = n;
            elseif (e == 2)
              n = create($nothing, 1);
              add_property(n, "foo", o, {player, ""});
              add_property(n, "bar", o, {player, ""});
              o = n;
            elseif (e == 3)
              n = create(#{a}, 1);
              n.next = o;
              o = n;
            elseif (e == 4)
              o = {o};
            elseif (e == 5)
              o = {o, o};
            elseif (e == 6)
              o = [1 -> o];
            elseif (e == 7)
              o = [1 -> o, 2 -> o];
            endif
          endfor;
          add_property(r, "next", o, {player, ""});
          recycle(#{a});
        EOF
        lines.split("\n").each do |line|
          vc << line
        end
      end
      call(a, 'go')
      run_gc()
    end
  end

  def test_the_garbage_collector_by_fuzzing_3
    run_test_as('wizard') do
      a = create(:object, 0)
      simplify(command(%Q|; add_property(#{a}, "next", #{a}, {player, ""}); |))
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        lines = <<-EOF
          r = o = #{a}.next = create($nothing, 1);
          for i in [1..1000];
            ticks_left() < 1000 || seconds_left() < 2 && suspend(0);
            if ((e = random(8)) == 1)
              n = create($nothing, 1);
              add_property(n, "next", o, {player, ""});
              o = n;
            elseif (e == 2)
              n = create($nothing, 1);
              add_property(n, "foo", o, {player, ""});
              add_property(n, "bar", o, {player, ""});
              o = n;
            elseif (e == 3)
              n = create(#{a}, 1);
              n.next = o;
              o = n;
            elseif (e == 4)
              o = {o};
            elseif (e == 5)
              o = {o, o};
            elseif (e == 6)
              o = [1 -> o];
            elseif (e == 7)
              o = [1 -> o, 2 -> o];
            endif
          endfor;
          #{a}.next = 0;
        EOF
        lines.split("\n").each do |line|
          vc << line
        end
      end
      call(a, 'go')
      run_gc()
    end
  end

  def test_the_garbage_collector_by_fuzzing_4
    run_test_as('wizard') do
      a = create(:object, 0)
      simplify(command(%Q|; add_property(#{a}, "next", #{a}, {player, ""}); |))
      add_verb(a, ['player', 'xd', 'go'], ['this', 'none', 'this'])
      set_verb_code(a, 'go') do |vc|
        lines = <<-EOF
          r = o = #{a}.next = create($nothing, 1);
          for i in [1..1000];
            ticks_left() < 1000 || seconds_left() < 2 && suspend(0);
            if ((e = random(8)) == 1)
              n = create($nothing, 1);
              add_property(n, "next", o, {player, ""});
              o = n;
            elseif (e == 2)
              n = create($nothing, 1);
              add_property(n, "foo", o, {player, ""});
              add_property(n, "bar", o, {player, ""});
              o = n;
            elseif (e == 3)
              n = create(#{a}, 1);
              n.next = o;
              o = n;
            elseif (e == 4)
              o = {o};
            elseif (e == 5)
              o = {o, o};
            elseif (e == 6)
              o = [1 -> o];
            elseif (e == 7)
              o = [1 -> o, 2 -> o];
            endif
          endfor;
          recycle(#{a});
        EOF
        lines.split("\n").each do |line|
          vc << line
        end
      end
      call(a, 'go')
      run_gc()
    end
  end

  def test_that_a_single_cyclic_self_reference_doesnt_crash_the_server_or_leak_memory
    x = nil
    run_test_as('wizard') do
      x = create(:nothing)
      add_property(x, 'next', 0, [player, ''])
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create(#{x}, 1); o.next = o; run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create(#{x}, 1); o.next = o; recycle(o); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create($nothing, 1); add_property(o, "next", o, {player, ""}); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create($nothing, 1); add_property(o, "next", o, {player, ""}); recycle(o); run_gc();|))
    end
  end

  def test_that_dual_cyclic_self_references_dont_crash_the_server_or_leak_memory
    a = nil
    run_test_as('wizard') do
      a = create(:nothing)
      add_property(a, 'x', 0, [player, ''])
      add_property(a, 'y', 0, [player, ''])
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create(#{a}, 1); o.x = o; o.y = o; run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create(#{a}, 1); o.x = o; o.y = o; recycle(o); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create($nothing, 1); add_property(o, "x", o, {player, ""}); add_property(o, "y", o, {player, ""}); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create($nothing, 1); add_property(o, "x", o, {player, ""}); add_property(o, "y", o, {player, ""}); recycle(o); run_gc();|))
    end
  end

  #
  def test_that_cyclic_references_through_lists_and_maps_dont_crash_the_server_or_leak_memory
    run_test_as('wizard') do
      simplify(command(%Q|; x = {o = create($nothing, 1), o, o}; add_property(o, "next", x, {player, ""}); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; x = {o = create($nothing, 1), o, o}; add_property(o, "next", x, {player, ""}); recycle(o); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; x = [1 -> o = create($nothing, 1), 2 -> o, 3 -> o]; add_property(o, "next", x, {player, ""}); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; x = [1 -> o = create($nothing, 1), 2 -> o, 3 -> o]; add_property(o, "next", x, {player, ""}); recycle(o); run_gc();|))
    end
  end

  # the following tests contrived cases where a specific value type
  # (object, list, map) will be the root of the cycle
  def test_that_any_value_may_be_the_root_of_the_cycle
    run_test_as('wizard') do
      simplify(command(%Q|; x = y = {create($nothing, 1)}; add_property(x[1], "next", x, {player, ""}); run_gc(); suspend(0); x = y = 0; run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; x = y = create($nothing, 1); add_property(x, "next", {x}, {player, ""}); run_gc(); suspend(0); x = y = 0; run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; x = y = [1 -> create($nothing, 1)]; add_property(x[1], "next", x, {player, ""}); run_gc(); suspend(0); x = y = 0; run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; x = y = create($nothing, 1); add_property(x, "next", [1 -> x], {player, ""}); run_gc(); suspend(0); x = y = 0; run_gc();|))
    end
  end

  def test_that_long_cyclic_chains_of_objects_dont_crash_the_server_or_leak_memory
    a = nil
    run_test_as('wizard') do
      a = create(:nothing)
      add_property(a, 'next', 0, [player, ''])
    end
    run_test_as('wizard') do
      9.times do |n|
        simplify(command(%Q|; x = y = create(#{a}, 1); for i in [1..#{n + 1}]; x.next = create(#{a}, 1); x = x.next; endfor; x.next = y; run_gc();|))
      end
    end
    run_test_as('wizard') do
      9.times do |n|
        simplify(command(%Q|; x = y = create(#{a}, 1); for i in [1..#{n + 1}]; x.next = create(#{a}, 1); x = x.next; endfor; x.next = y; recycle(y); run_gc();|))
      end
    end
    run_test_as('wizard') do
      9.times do |n|
        simplify(command(%Q|; x = y = create($nothing, 1); for i in [1..#{n + 1}]; add_property(x, "next", create($nothing, 1), {player, ""}); x = x.next; endfor; add_property(x, "next", y, {player, ""}); run_gc();|))
      end
    end
    run_test_as('wizard') do
      8.times do |n|
        simplify(command(%Q|; x = y = create($nothing, 1); for i in [1..#{n + 1}]; add_property(x, "next", create($nothing, 1), {player, ""}); x = x.next; endfor; add_property(x, "next", y, {player, ""}); recycle(y); run_gc();|))
      end
    end
  end

  def test_that_anonymous_objects_made_invalid_dont_crash_the_server_or_leak_memory
    run_test_as('wizard') do
      simplify(command(%Q|; o = create($nothing); p = create(o, 1); add_property(o, "foo", 0, {player, ""}); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; o = create($nothing); p = create(o, 1); add_property(p, "next", p, {player, ""}); add_property(o, "foo", 0, {player, ""}); run_gc();|))
    end
  end

  # these tests add/leave a purple root value, and then destroy that value...
  def test_contrived_example_that_accesses_an_already_recycled_object
    run_test_as('wizard') do
      simplify(command(%Q|; x = y = create($nothing, 1); add_property(x, "next", create($nothing, 1), {player, ""}); add_property(x.next, "next", x, {player, ""}); recycle(y); run_gc();|))
    end
    run_test_as('wizard') do
      simplify(command(%Q|; x = y = create($nothing, 1); add_property(x, "next", create($nothing, 1), {player, ""}); add_property(x.next, "next", x, {player, ""}); recycle(x); run_gc();|))
    end
  end

  # these tests ensure that the heavily shared empty list/map are always green
  def test_that_empty_lists_and_maps_are_green
    run_test_as('wizard') do
      gc = simplify(command(%Q|; o = p = create($nothing, 1); add_property(o, "list", {}, {player, ""}); run_gc(); suspend(0); o = p = 0; run_gc(); suspend(0); x = {}; return gc_stats();|))
      assert_equal 0, gc['purple']
    end
    run_test_as('wizard') do
      gc = simplify(command(%Q|; o = p = create($nothing, 1); add_property(o, "map", [], {player, ""}); run_gc(); suspend(0); o = p = 0; run_gc(); suspend(0); x = []; return gc_stats();|))
      assert_equal 0, gc['purple']
    end
  end

end
