require 'test_helper'

class TestPropertyExists < Test::Unit::TestCase
  def property_exists(object, name, direct = nil)
    args = [obj_ref(object), value_ref(name)]
    args << value_ref(direct) unless direct.nil?
    evaluate("property_exists(#{args.join(', ')})")
  end

  def test_builtin_and_missing_properties
    run_test_as('programmer') do
      o = create(NOTHING)
      assert_equal 1, property_exists(o, 'name')
      assert_equal 1, property_exists(o, 'name', 0)
      assert_equal 0, property_exists(o, 'name', 1)
      assert_equal 0, property_exists(o, 'missing')
      assert_equal 0, property_exists(o, 'missing', 1)
    end
  end

  def test_direct_and_inherited_properties_do_not_depend_on_value
    run_test_as('programmer') do
      parent = create(NOTHING)
      add_property(parent, 'example', 0, [player, 'r'])
      child = create(parent)
      [0, '', [], [1, 2], 'value'].each do |value|
        set(parent, 'example', value)
        assert_equal 1, property_exists(parent, 'example')
        assert_equal 1, property_exists(parent, 'example', 1)
        assert_equal 1, property_exists(parent, 'example', -1)
        assert_equal 1, property_exists(child, 'example')
        assert_equal 1, property_exists(child, 'example', 0)
        assert_equal 0, property_exists(child, 'example', 1)
      end
    end
  end

  def test_clear_values_through_several_ancestors
    run_test_as('programmer') do
      root = create(NOTHING)
      add_property(root, 'example', 0, [player, 'r'])
      middle = create(root)
      child = create(middle)
      clear_property(middle, 'example')
      clear_property(child, 'example')
      assert_equal 1, is_clear_property(child, 'example')
      assert_equal 1, property_exists(child, 'example')
      assert_equal 0, property_exists(child, 'example', 1)
      assert_equal 1, property_exists(root, 'example', 1)
    end
  end

  def test_anonymous_objects
    run_test_as('programmer') do
      result = simplify(command('; p = create($nothing); add_property(p, "example", 0, {player, "r"}); a = create(p, 1); clear_property(a, "example"); return {property_exists(a, "example"), property_exists(a, "example", 1), property_exists(a, "name"), property_exists(a, "name", 1), property_exists(a, "missing"), is_clear_property(a, "example")};'))
      assert_equal [1, 0, 1, 0, 0, 1], result
    end
  end

  def test_multiple_inheritance_finds_properties_from_both_parents
    run_test_as('programmer') do
      left = create(NOTHING)
      right = create(NOTHING)
      add_property(left, 'left_prop', 0, [player, 'r'])
      add_property(right, 'right_prop', '', [player, 'r'])
      child = create(NOTHING)
      chparents(child, [left, right])
      assert_equal 1, property_exists(child, 'left_prop')
      assert_equal 1, property_exists(child, 'right_prop')
      assert_equal 0, property_exists(child, 'left_prop', 1)
      assert_equal 0, property_exists(child, 'right_prop', 1)
    end
  end

  def test_temporary_anonymous_argument_remains_alive_during_direct_check
    run_test_as('programmer') do
      result = simplify(command('; p = create($nothing); add_property(p, "example", 0, {player, "r"}); return {property_exists(create(p, 1), "example"), property_exists(create(p, 1), "example", 1), property_exists(create(p, 1), "name", 1)};'))
      assert_equal [1, 0, 0], result
    end
  end

  def test_property_name_matching_is_case_insensitive
    run_test_as('programmer') do
      o = create(NOTHING)
      add_property(o, 'Example', 0, [player, 'r'])
      assert_equal 1, property_exists(o, 'EXAMPLE')
      assert_equal 1, property_exists(o, 'example', 1)
      assert_equal 1, property_exists(o, 'NAME')
    end
  end

  def test_errors_for_invalid_objects_types_and_argument_counts
    run_test_as('programmer') do
      assert_equal E_INVARG, property_exists(NOTHING, 'name')
      o = create(NOTHING)
      recycle(o)
      assert_equal E_INVARG, property_exists(o, 'name')
      assert_equal E_INVARG, property_exists(o, 'name', 1)
      assert_equal E_TYPE, evaluate('property_exists(0, "name")')
      assert_equal E_TYPE, evaluate('property_exists(player, 0)')
      assert_equal E_TYPE, evaluate('property_exists(player, "name", "yes")')
      assert_equal E_ARGS, evaluate('property_exists()')
      assert_equal E_ARGS, evaluate('property_exists(player, "name", 0, 0)')
    end
  end

  def test_unreadable_custom_properties_raise_even_with_direct_only
    parent = child = nil
    run_test_as('wizard') do
      parent = create(NOTHING)
      set(parent, 'f', 1)
      add_property(parent, 'private_prop', 0, [player, ''])
      child = create(parent)
      assert_equal 1, property_exists(parent, 'private_prop', 1)
    end
    run_test_as('programmer') do
      assert_equal E_PERM, property_exists(parent, 'private_prop')
      assert_equal E_PERM, property_exists(parent, 'private_prop', 1)
      assert_equal E_PERM, property_exists(child, 'private_prop')
      assert_equal E_PERM, property_exists(child, 'private_prop', 1)
      assert_equal 0, property_exists(child, 'missing')
    end
  end

  def test_lookup_reflects_add_delete_and_reparent_operations
    run_test_as('programmer') do
      root = create(NOTHING)
      child = create(root)
      assert_equal 0, property_exists(child, 'example')
      add_property(root, 'example', 0, [player, 'r'])
      assert_equal 1, property_exists(child, 'example')
      delete_property(root, 'example')
      assert_equal 0, property_exists(child, 'example')
      other = create(NOTHING)
      add_property(other, 'example', 0, [player, 'r'])
      chparent(child, other)
      assert_equal 1, property_exists(child, 'example')
      assert_equal 0, property_exists(child, 'example', 1)
    end
  end

  def test_waif_definitions_defaults_and_instance_overrides
    run_test_as('programmer') do
      parent = create(:waif)
      add_property(parent, ':inherited', 0, [player, ''])
      child = create(parent)
      add_property(child, ':own', '', [player, ''])
      add_property(child, 'class_only', 1, [player, ''])
      assert_equal [0, 1, 1, 0, 1, 1, 0, 0], moo(%Q|
        w = #{child}:new();
        return {valid(w), property_exists(w, "own"), property_exists(w, "own", 1),
                property_exists(w, "missing"), property_exists(w, "inherited"),
                property_exists(w, "inherited", 0), property_exists(w, "inherited", 1),
                property_exists(w, "class_only")};|)
      assert_equal [1, 0, 42, 1, 1], moo(%Q|
        w = #{child}:new(); w.inherited = 42;
        return {property_exists(w, "inherited"), property_exists(w, "inherited", 1),
                w.inherited, property_exists(w, "own", -1), property_exists(w, "own", 2)};|)
    end
  end

  def test_multiple_inheritance_uses_definition_owner_not_instance_value
    run_test_as('programmer') do
      left = create(:waif)
      right = create(:waif)
      add_property(left, ':left', 1, [player, ''])
      add_property(right, ':right', 2, [player, ''])
      child = create([left, right])
      add_property(child, ':own', 3, [player, ''])
      assert_equal [1, 0, 1, 0, 1], moo(%Q|
        w = #{child}:new(); w.right = 20;
        return {property_exists(w, "left"), property_exists(w, "left", 1),
                property_exists(w, "right"), property_exists(w, "right", 1),
                property_exists(w, "own", 1)};|)
    end
  end

  def test_names_follow_waif_read_case_and_prefix_rules
    run_test_as('programmer') do
      klass = create(:waif)
      add_property(klass, ':Color', 1, [player, ''])
      add_property(klass, '::Color', 2, [player, ''])
      add_property(klass, ':only', 3, [player, ''])
      add_property(klass, ':', 4, [player, ''])
      assert_equal [1, 1, 1, 0, 1], moo(%Q|
        w = #{klass}:new();
        return {property_exists(w, "color"), property_exists(w, "COLOR", 1),
                property_exists(w, ":color"), property_exists(w, ":only"),
                property_exists(w, "")};|)
    end
  end

  def test_intrinsics_match_object_direct_only_semantics
    run_test_as('programmer') do
      klass = create(:waif)
      add_property(klass, ':owner', 999, [player, ''])
      assert_equal [[1, 0], [1, 0], [1, 0], [1, 0]], moo(%Q|
        w = #{klass}:new(); r = {};
        for name in ({"owner", "CLASS", "wizard", "programmer"})
          r = {@r, {property_exists(w, name), property_exists(w, name, 1)}};
        endfor
        return r;|)
      assert_equal [1, 0], moo('return {property_exists(player, "owner"), property_exists(player, "owner", 1)};')
      assert_equal 0, moo(%Q|return property_exists(#{klass}:new(), "name");|)
    end
  end

  def test_property_owner_chown_read_and_wizard_permissions
    run_test_as('wizard') do
      waif_owner = create(:object)
      property_owner = create(:object)
      outsider = create(:object)
      parent = create(:waif)
      add_property(parent, ':chowned', 0, [property_owner, 'c'])
      add_property(parent, ':private', 0, [property_owner, ''])
      add_property(parent, ':public', 0, [property_owner, 'r'])
      add_property(parent, ':owner', 999, [property_owner, ''])
      child = create(parent)
      # The factory runs as its nonwizard owner; the caller retains wizard
      # permissions until selecting the requester for the existence check.
      add_verb(child, [waif_owner, 'xd', 'make'], ['this', 'none', 'this'])
      assert_equal [], set_verb_code(child, 'make', ['return this:new();'])
      assert_equal waif_owner, moo(%Q|return #{child}:make().owner;|)
      assert_equal 0, moo(%Q|#{child}.r = 0; return #{child}.r;|)
      {
        waif_owner => {'chowned' => 1, 'private' => E_PERM, 'public' => 1},
        property_owner => {'chowned' => E_PERM, 'private' => 1, 'public' => 1},
        outsider => {'chowned' => E_PERM, 'private' => E_PERM, 'public' => 1}
      }.each do |requester, properties|
        properties.each do |name, expected|
          assert_equal expected, moo(%Q|
            w = #{child}:make(); set_task_perms(#{requester});
            return property_exists(w, "#{name}");|)
        end
      end
      assert_equal E_PERM, moo(%Q|
        w = #{child}:make(); set_task_perms(#{outsider});
        return property_exists(w, "private", 1);|)
      assert_equal 0, moo(%Q|
        w = #{child}:make(); set_task_perms(#{waif_owner});
        return property_exists(w, "chowned", 1);|)
      assert_equal [1, 1], moo(%Q|
        w = #{child}:make(); return {property_exists(w, "chowned"), property_exists(w, "private")};|)
      assert_equal [1, 0], moo(%Q|
        w = #{child}:make(); set_task_perms(#{outsider});
        return {property_exists(w, "owner"), property_exists(w, "owner", 1)};|)
    end
  end

  def test_definition_changes_refresh_existing_waifs
    run_test_as('programmer') do
      parent = create(:waif)
      add_property(parent, ':keep', 5, [player, ''])
      child = create(parent)
      assert_equal [1, 1, 0, 1, 123, 0, 0], moo(%Q|
        w = #{child}:new(); w.keep = 123;
        add_property(#{child}, ":new", 0, {player, ""});
        r = {property_exists(w, "new", 1)};
        set_property_info(#{parent}, ":keep", {player, "", ":renamed"});
        r = {@r, property_exists(w, "renamed"), property_exists(w, "keep"),
             property_exists(w, "renamed", 0), w.renamed};
        delete_property(#{child}, ":new");
        delete_property(#{parent}, ":renamed");
        return {@r, property_exists(w, "new"), property_exists(w, "renamed")};|)
    end
  end

  def test_permission_changes_apply_to_the_same_existing_waif
    run_test_as('wizard') do
      waif_owner = create(:object)
      property_owner = create(:object)
      requester = create(:object)
      klass = create(:waif)
      add_property(klass, ':changing', 0, [property_owner, 'r'])
      add_verb(klass, [waif_owner, 'xd', 'make'], ['this', 'none', 'this'])
      set_verb_code(klass, 'make', ['return this:new();'])
      add_verb(klass, [requester, 'xd', 'probe'], ['this', 'none', 'this'])
      set_verb_code(klass, 'probe', ['return property_exists(args[1], "changing");'])
      assert_equal [1, 1, 1, 1, 1], moo(%Q|
        w = #{klass}:make(); r = {#{klass}:probe(w)};
        set_property_info(#{klass}, ":changing", {#{property_owner}, ""});
        try
          #{klass}:probe(w); r = {@r, 0};
        except (E_PERM)
          r = {@r, 1};
        endtry
        set_property_info(#{klass}, ":changing", {#{requester}, ""});
        r = {@r, #{klass}:probe(w)};
        set_property_info(#{klass}, ":changing", {#{requester}, "c"});
        try
          #{klass}:probe(w); r = {@r, 0};
        except (E_PERM)
          r = {@r, 1};
        endtry
        set_property_info(#{klass}, ":changing", {#{requester}, "rc"});
        return {@r, #{klass}:probe(w)};|)
    end
  end

  def test_property_layout_changes_beyond_the_waif_bitmap
    run_test_as('programmer') do
      klass = create(:waif)
      assert_equal [1, 1, 1, 1, 0, 1, 42], moo(%Q|
        for i in [1..110]
          add_property(#{klass}, ":p" + tostr(i), 0, {player, ""});
        endfor
        w = #{klass}:new(); w.p110 = 42;
        r = {property_exists(w, "p1", 1), property_exists(w, "p96", 1),
             property_exists(w, "p97", 1), property_exists(w, "p110", 1)};
        delete_property(#{klass}, ":p1");
        return {@r, property_exists(w, "p1"), property_exists(w, "p110"), w.p110};|)
    end
  end

  def test_reparenting_updates_existing_waif_inheritance
    run_test_as('programmer') do
      left = create(:waif)
      right = create(:waif)
      add_property(left, ':left', 0, [player, ''])
      add_property(right, ':right', 0, [player, ''])
      child = create(left)
      assert_equal [1, 0, 1, 0], moo(%Q|
        w = #{child}:new(); before = property_exists(w, "left");
        chparent(#{child}, #{right});
        return {before, property_exists(w, "left"), property_exists(w, "right"),
                property_exists(w, "right", 1)};|)
    end
  end

  def test_recycled_class_is_invalid_for_all_property_names
    run_test_as('programmer') do
      klass = create(:waif)
      add_property(klass, ':x', 1, [player, ''])
      assert_equal [[1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1], NOTHING], moo(%Q|
        w = #{klass}:new(); w.x = 123; recycle(#{klass}); r = {};
        for name in ({"x", "missing", "owner", "class", "wizard", "programmer"})
          for direct in ({0, 1})
            try
              property_exists(w, name, direct); r = {@r, 0};
            except (E_INVARG)
              r = {@r, 1};
            endtry
          endfor
        endfor
        return {r, w.class};|)
    end
  end

  def test_property_changes_do_not_create_direct_definitions_on_objects_or_anons
    run_test_as('programmer') do
      parent = create(:object)
      add_property(parent, 'value', 0, [player, ''])
      child = create(parent)
      assert_equal [1, 0, 1, 0, 1, 0, 0], moo(%Q|
        a = create(#{parent}, 1); a.value = 9; #{child}.value = 10;
        return {property_exists(#{parent}, "value", 1), property_exists(#{child}, "value", 1),
                property_exists(a, "value"), property_exists(a, "value", 1),
                property_exists(a, "owner"), property_exists(a, "owner", 1),
                property_exists(a, "missing")};|)
      assert_equal 1, moo(%Q|return property_exists(create(#{parent}, 1), "value");|)
      assert_equal 0, moo(%Q|return property_exists(create(#{parent}, 1), "value", 1);|)
    end
  end

  def test_object_permission_checks_still_precede_direct_filtering
    run_test_as('wizard') do
      owner = create(:object)
      other = create(:object)
      parent = create(:object)
      add_property(parent, 'private', 0, [owner, ''])
      child = create(parent)
      assert_equal 1, moo(%Q|set_task_perms(#{owner}); return property_exists(#{child}, "private");|)
      assert_equal 0, moo(%Q|set_task_perms(#{owner}); return property_exists(#{child}, "private", 1);|)
      assert_equal E_PERM, moo(%Q|set_task_perms(#{other}); return property_exists(#{child}, "private");|)
      assert_equal E_PERM, moo(%Q|set_task_perms(#{other}); return property_exists(#{child}, "private", 1);|)
      assert_equal [1, 0], moo(%Q|return {property_exists(#{child}, "private"), property_exists(#{child}, "private", 1)};|)
    end
  end

  def test_recycled_objects_and_invalid_anonymous_objects_still_raise_invarg
    run_test_as('programmer') do
      parent = create(:object)
      assert_equal E_INVARG, moo(%Q|a = create(#{parent}, 1); recycle(a); return property_exists(a, "owner");|)
      obj = create(:object)
      assert_equal E_INVARG, moo(%Q|recycle(#{obj}); return property_exists(#{obj}, "owner");|)
    end
  end

  def test_recycling_an_ancestor_preserves_valid_targets
    run_test_as('programmer') do
      parent = create(:object)
      assert_equal 1, moo(%Q|a = create(#{parent}, 1); recycle(#{parent}); return property_exists(a, "owner");|)
      waif_parent = create(:waif)
      add_property(waif_parent, ':lost', 0, [player, ''])
      child = create(waif_parent)
      add_property(child, ':own', 0, [player, ''])
      assert_equal [0, 1, 1], moo(%Q|
        w = #{child}:new(); w.lost = 5; recycle(#{waif_parent});
        return {property_exists(w, "lost"), property_exists(w, "own", 1), property_exists(w, "class")};|)
    end
  end

  def test_invalid_objects_types_and_argument_counts_are_unchanged
    run_test_as('programmer') do
      klass = create(:waif)
      {
        'property_exists()' => E_ARGS,
        'property_exists(player)' => E_ARGS,
        'property_exists(player, "name", 0, 0)' => E_ARGS,
        'property_exists(42, "x")' => E_TYPE,
        'property_exists({}, "x")' => E_TYPE,
        'property_exists(TRUE, "x")' => E_TYPE,
        'property_exists(#-1, "name")' => E_INVARG,
        'property_exists(player, 42)' => E_TYPE,
        'property_exists(player, "name", "yes")' => E_TYPE,
        'property_exists(player, "name", TRUE)' => E_TYPE,
        "property_exists(#{klass}:new(), 42)" => E_TYPE,
        "property_exists(#{klass}:new(), \"owner\", {})" => E_TYPE,
        "property_exists(#{klass}:new(), \"owner\", FALSE)" => E_TYPE
      }.each do |call, expected|
        assert_equal expected, moo("return #{call};"), call
      end
    end
  end

  def test_temporary_waif_lifetime_is_held_until_the_check_finishes
    run_test_as('programmer') do
      klass = create(:waif)
      add_property(klass, ':value', 0, [player, ''])
      assert_equal [1, 1, 0, 1, 0], moo(%Q|
        return {property_exists(#{klass}:new(), "value"), property_exists(#{klass}:new(), "value", 1),
                property_exists(#{klass}:new(), "missing"), property_exists(#{klass}:new(), "class"),
                property_exists(#{klass}:new(), "class", 1)};|)
    end
  end

  def test_anonymous_permissions_precede_direct_filtering
    run_test_as('wizard') do
      parent = create(NOTHING)
      requester = create(NOTHING)
      add_property(parent, 'private', 0, [player, ''])
      add_property(parent, 'public', 0, [player, 'r'])
      assert_equal [E_PERM, E_PERM, 1, 0, 0, 1, 0], moo(%Q|
        a = create(#{parent}, 1); set_task_perms(#{requester});
        return {`property_exists(a, "private") ! E_PERM => E_PERM',
                `property_exists(a, "private", 1) ! E_PERM => E_PERM',
                property_exists(a, "public"), property_exists(a, "public", 1),
                property_exists(a, "missing"), property_exists(a, "owner"),
                property_exists(a, "owner", 1)};|)
    end
  end

  def test_existence_does_not_call_waif_index_verbs
    # Dictionary indexing requires a wizard-owned class; exercise the real
    # callback to show that the zero call count is meaningful.
    run_test_as('wizard') do
      klass = create(:waif)
      add_property(klass, 'index_calls', 0, [player, ''])
      add_property(klass, ':value', 0, [player, ''])
      add_verb(klass, [player, 'xd', ':_index'], ['this', 'none', 'this'])
      assert_equal [], set_verb_code(klass, ':_index', [
        "#{klass}.index_calls = #{klass}.index_calls + 1;",
        'return 42;'
      ])
      assert_equal [1, 0, 0, 42, 1], moo(%Q|
        w = #{klass}:new(); r = {property_exists(w, "value"), property_exists(w, "missing"),
                               #{klass}.index_calls};
        v = w["missing"];
        return {@r, v, #{klass}.index_calls};|)
    end
  end

  private

  def moo(code)
    simplify(command('; ' + code.gsub(/\s*\n\s*/, ' ')))
  end
end
