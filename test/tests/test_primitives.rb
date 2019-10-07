require 'test_helper'

class TestPrimitives < Test::Unit::TestCase

  def setup
    run_test_as('wizard') do
      simplify command %Q|; for task in (queued_tasks()); kill_task(task[1]); endfor |
    end
  end

  def test_that_server_behavior_is_unchanged_when_no_prototypes_exist
    run_test_as('programmer') do
      assert_equal E_TYPE, simplify(command(%Q|; return 123:foo();|))
      assert_equal E_TYPE, simplify(command(%Q|; return 1.23:foo();|))
      assert_equal E_TYPE, simplify(command(%Q|; return "abc":foo();|))
      assert_equal E_TYPE, simplify(command(%Q|; return E_NONE:foo();|))
      assert_equal E_TYPE, simplify(command(%Q|; return {}:foo();|))
      assert_equal E_TYPE, simplify(command(%Q|; return []:foo();|))
    end
  end

  [['integer', 'int_proto', 123], ['float', 'float_proto', 1.23],
   ['string', 'str_proto', "abc"], ['error', 'err_proto', E_NONE],
   ['list', 'list_proto', {}], ['map', 'map_proto', []]
  ].each do |name, proto, value|
    class_eval %Q{
      def test_that_the_#{name}_prototype_works
        run_test_as('wizard') do
          begin
            o = create(:nothing)
            add_property(:system, "#{proto}", o, [player, ''])
            assert_equal E_VERBNF, simplify(command(%Q|; return #{value.inspect}:foo();|))
            add_verb(:#{proto}, [player, 'xd', 'foo'], ['this', 'none', 'this'])
            assert_equal 0, simplify(command(%Q|; return #{value.inspect}:foo();|))
          ensure
            delete_property(:system, "#{proto}")
            recycle(o)
          end
        end
      end
    }
  end

  def test_that_queued_tasks_includes_this
    run_test_as('wizard') do
      begin
        o = create(:nothing)
        add_property(:system, "map_proto", o, [player, ''])
        add_verb(o, [player, 'xd', 'suspend'], ['this', 'none', 'this'])
        set_verb_code(o, 'suspend') do |vc|
          vc << %Q|fork t (0)|
          vc << %Q|  return suspend();|
          vc << %Q|endfork|
          vc << %Q|return t;|
        end
        assert_equal [], queued_tasks()
        t = simplify(command(%Q|; return ["one" -> 1, "two" -> 2]:suspend();|))
        q = queued_tasks()
        assert_equal({"one" => 1, "two" => 2}, q[8])
      ensure
        kill_task(t)
        delete_property(:system, "map_proto")
        recycle(o)
      end
    end
  end

  def test_that_callers_includes_this
    run_test_as('wizard') do
      begin
        o = create(:nothing)
        add_property(:system, "list_proto", o, [player, ''])
        add_verb(o, [player, 'xd', 'foo'], ['this', 'none', 'this'])
        add_verb(o, [player, 'xd', 'bar'], ['this', 'none', 'this'])
        set_verb_code(o, 'foo') do |vc|
          vc << %Q|return this:bar();|
        end
        set_verb_code(o, 'bar') do |vc|
          vc << %Q|return callers();|
        end
        c = simplify(command(%Q|; return {1, 2, "three", 4.0}:foo();|))
        assert_equal([1, 2, 'three', 4.0], c[0][0])
      ensure
        delete_property(:system, "list_proto")
        recycle(o)
      end
    end
  end

  def test_that_inheritance_works
    run_test_as('wizard') do
      begin
        base = create(:nothing)
        list = create(base)
        map = create(base)
        add_property(:system, "list_proto", list, [player, ''])
        add_property(:system, "map_proto", map, [player, ''])
        add_verb(base, [player, 'xd', 'length'], ['this', 'none', 'this'])
        set_verb_code(base, 'length') do |vc|
          vc << %Q|return length(this);|
        end
        assert_equal 2, simplify(command(%Q|; return ["one" -> 1, "two" -> 2]:length();|))
        assert_equal 4, simplify(command(%Q|; return {1, 2, "three", 4.0}:length();|))
      ensure
        delete_property(:system, "list_proto")
        delete_property(:system, "map_proto")
        recycle(list)
        recycle(map)
        recycle(base)
      end
    end
  end

  def test_that_pass_works
    run_test_as('wizard') do
      begin
        base = create(:nothing)
        list = create(base)
        map = create(base)
        add_property(:system, "list_proto", list, [player, ''])
        add_property(:system, "map_proto", map, [player, ''])
        add_verb(base, [player, 'xd', 'length'], ['this', 'none', 'this'])
        set_verb_code(base, 'length') do |vc|
          vc << %Q|return length(this);|
        end
        add_verb(list, [player, 'xd', 'length'], ['this', 'none', 'this'])
        set_verb_code(list, 'length') do |vc|
          vc << %Q|return tostr(pass());|
        end
        add_verb(map, [player, 'xd', 'length'], ['this', 'none', 'this'])
        set_verb_code(map, 'length') do |vc|
          vc << %Q|return tostr(pass());|
        end
        assert_equal '2', simplify(command(%Q|; return ["one" -> 1, "two" -> 2]:length();|))
        assert_equal '4', simplify(command(%Q|; return {1, 2, "three", 4.0}:length();|))
      ensure
        delete_property(:system, "list_proto")
        delete_property(:system, "map_proto")
        recycle(list)
        recycle(map)
        recycle(base)
      end
    end
  end

end
