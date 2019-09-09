require 'open3'

require 'test_helper'

class Array

  def include_sequence?(sequence)
    items = self.to_enum
    sequence.each do |i|
      return false unless loop { break true if items.next == i }
    end
    true
  end

end

class TestCannedDbs < Test::Unit::TestCase

  private

  def diff(first, second)
    _, diff, _, wait = Open3.popen3 %[diff #{first} #{second}]
    wait.value

    diff.readlines.map(&:chomp)
  end

  def log_and_diff(original, backup)
    _, _, log, wait = Open3.popen3 %[./moo #{original} #{backup} 9899]
    wait.value

    _, diff, _, wait = Open3.popen3 %[diff #{original} #{backup}]
    wait.value

    [log.readlines.map(&:chomp), diff.readlines.map(&:chomp)]
  end

  public

  def test_that_creating_garbage_and_then_shutting_down_leaves_a_pending_anonymous_object
    log1, diff1 = log_and_diff('test/Anon1.db', '/tmp/Foo.db')
    log2, diff2 = log_and_diff('/tmp/Foo.db', '/tmp/Bar.db')

    delta = [
      '< 0 values pending finalization',
      '---',
      '> 1 values pending finalization',
      '> 12',
      '> 4'
    ]

    assert log1.none? { |l| l =~ /pre_destroy called/ }
    assert log2.any? { |l| l =~ /pre_destroy called/ }

    assert diff1.include_sequence? delta
    assert_equal [], diff2
  end

  def test_that_creating_cyclic_garbage_and_then_shutting_down_leaves_pending_anonymous_objects
    log1, diff1 = log_and_diff('test/Anon2.db', '/tmp/Foo.db')
    log2, diff2 = log_and_diff('/tmp/Foo.db', '/tmp/Bar.db')

    delta = [
      '< 0 values pending finalization',
      '---',
      '> 2 values pending finalization',
      '> 12',
      '> 4',
      '> 12',
      '> 5'
    ]

    assert log1.none? { |l| l =~ /pre_destroy called/ }
    assert log2.any? { |l| l =~ /pre_destroy called on A/ }
    assert log2.any? { |l| l =~ /pre_destroy called on B/ }

    assert diff1.include_sequence? delta
    assert_equal [], diff2
  end

  def test_that_creating_garbage_and_letting_the_server_clean_up_leaves_the_database_as_is
    log1, diff1 = log_and_diff('test/Anon3.db', '/tmp/Foo.db')
    log2, diff2 = log_and_diff('/tmp/Foo.db', '/tmp/Bar.db')

    assert log1.any? { |l| l =~ /pre_destroy called/ }
    assert log2.any? { |l| l =~ /pre_destroy called/ }

    assert_equal [], diff1
    assert_equal [], diff2
  end

  def test_that_chains_of_objects_are_cleaned_up_correctly
    log1, diff1 = log_and_diff('test/Anon4.db', '/tmp/Foo.db')
    log2, diff2 = log_and_diff('/tmp/Foo.db', '/tmp/Bar.db')
    log3, diff3 = log_and_diff('/tmp/Bar.db', '/tmp/Baz.db')

    assert log1.any? { |l| l =~ /\$one is valid/ }
    assert log1.any? { |l| l =~ /\$one\.two is valid/ }
    assert log2.any? { |l| l =~ /\$one is valid/ }
    assert log2.any? { |l| l =~ /\$one\.two is invalid/ }
    assert log3.any? { |l| l =~ /\$one is invalid/ }
    assert log3.any? { |l| l =~ /\$one\.two is invalid/ }

    assert_equal [], diff('test/Anon4.db', '/tmp/Baz.db')

    delta2 = [
      '< 0 values pending finalization',
      '---',
      '> 1 values pending finalization',
      '> 12',
      '> 4'
    ]

    delta3 = [
      '< 1 values pending finalization',
      '< 12',
      '< 4',
      '---',
      '> 0 values pending finalization'
    ]

    assert diff2.include_sequence? delta2
    assert diff3.include_sequence? delta3
  end

  def test_that_loaded_anonymous_objects_are_accounted_for_correctly
    log1, diff1 = log_and_diff('test/Anon5.db', '/tmp/Foo.db')
    log2, diff2 = log_and_diff('/tmp/Foo.db', '/tmp/Bar.db')
    log3, diff3 = log_and_diff('/tmp/Bar.db', '/tmp/Baz.db')

    delta1 = [
      '10c10',
      '< 4',
      '---',
      '> 5'
    ]

    assert diff1.include_sequence? delta1
    assert_equal [], diff2
    assert_equal [], diff3
  end

  def test_that_invalid_values_pending_finalization_are_removed
    log1, diff1 = log_and_diff('test/Anon6.db', '/tmp/Foo.db')

    delta1 = [
      '< 1 values pending finalization',
      '< 12',
      '< -1',
      '---',
      '> 0 values pending finalization'
    ]

    assert diff1.include_sequence? delta1

    assert log1.any? { |l| l =~ /shazam/ }
  end

  def test_that_check_for_invalid_objects_succeeds
    log, _ = log_and_diff('test/Broken1.db', '/dev/null')

    assert log.any? { |l| l =~ /#0.parent = #103 <invalid> ... removed/ }
    assert log.any? { |l| l =~ /#0.child = #104 <invalid> ... removed/ }
    assert log.any? { |l| l =~ /#0.location = #101 <invalid> ... fixed/ }
    assert log.any? { |l| l =~ /#0.content = #102 <invalid> ... removed/ }
  end

  def test_that_check_for_invalid_object_types_succeeds
    log, _ = log_and_diff('test/Broken2.db', '/dev/null')

    assert log.any? { |l| l =~ /#0.parents is not an object or list of objects/ }
    assert log.any? { |l| l =~ /#0.children is not a list of objects/ }
    assert log.any? { |l| l =~ /#0.location is not an object/ }
    assert log.any? { |l| l =~ /#0.contents is not a list of objects/ }
    assert log.any? { |l| l =~ /#1.parents is not an object or list of objects/ }
    assert log.any? { |l| l =~ /#1.children is not a list of objects/ }
    assert log.any? { |l| l =~ /#1.location is not an object/ }
    assert log.any? { |l| l =~ /#1.contents is not a list of objects/ }
  end


  def test_that_check_for_cycles_succeeds
    log, _ = log_and_diff('test/Broken3.db', '/dev/null')

    assert log.any? { |l| l =~ /Cycle in parent chain of #0/ }
    assert log.any? { |l| l =~ /Cycle in location chain of #0/ }
    assert log.any? { |l| l =~ /Cycle in parent chain of #1/ }
    assert log.any? { |l| l =~ /Cycle in location chain of #1/ }
    assert log.any? { |l| l =~ /Cycle in parent chain of #2/ }
    assert log.any? { |l| l =~ /Cycle in location chain of #2/ }
    assert log.any? { |l| l =~ /Cycle in parent chain of #3/ }
    assert log.any? { |l| l =~ /Cycle in location chain of #3/ }
  end

  def test_that_check_for_inconsistencies_top_down_succeeds
    log, _ = log_and_diff('test/Broken4.db', '/dev/null')

    assert log.any? { |l| l =~ /#0 not in it's location's \(#2\) contents/ }
    assert log.any? { |l| l =~ /#0 not in it's parent's \(#1\) children/ }
    assert log.any? { |l| l =~ /#1 not in it's location's \(#2\) contents/ }
    assert log.any? { |l| l =~ /#2 not in it's parent's \(#1\) children/ }
    assert log.any? { |l| l =~ /#3 not in it's location's \(#2\) contents/ }
    assert log.any? { |l| l =~ /#3 not in it's parent's \(#1\) children/ }
  end

  def test_that_check_for_inconsistencies_bottom_up_succeeds
    log, _ = log_and_diff('test/Broken5.db', '/dev/null')

    assert log.any? { |l| l =~ /#1 not in it's child's \(#0\) parents/ }
    assert log.any? { |l| l =~ /#2 not in it's content's \(#3\) location/ }
  end

end
