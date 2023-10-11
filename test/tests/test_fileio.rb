require 'test_helper'

class TestFileio < Test::Unit::TestCase

  def test_that_file_operations_fail_for_non_wizards
    run_test_as('programmer') do
      assert_equal E_PERM, file_open('', '')
      assert_equal E_PERM, file_close(0)
      assert_equal E_PERM, file_name(0)
      assert_equal E_PERM, file_openmode(0)
      assert_equal E_PERM, file_readline(0)
      assert_equal E_PERM, file_readlines(0, 1, 2)
      assert_equal E_PERM, file_writeline(0, '')
      assert_equal E_PERM, file_read(0, 1)
      assert_equal E_PERM, file_write(0, '')
      assert_equal E_PERM, file_tell(0)
      assert_equal E_PERM, file_seek(0, 1, '')
      assert_equal E_PERM, file_eof(0)
      assert_equal E_PERM, file_size('')
      assert_equal E_PERM, file_mode('')
      assert_equal E_PERM, file_last_access('')
      assert_equal E_PERM, file_last_modify('')
      assert_equal E_PERM, file_last_change('')
      assert_equal E_PERM, file_size(0)
      assert_equal E_PERM, file_mode(0)
      assert_equal E_PERM, file_last_access(0)
      assert_equal E_PERM, file_last_modify(0)
      assert_equal E_PERM, file_last_change(0)
      assert_equal E_PERM, file_stat('')
      assert_equal E_PERM, file_stat(0)
      assert_equal E_PERM, file_rename('', '')
      assert_equal E_PERM, file_remove('')
      assert_equal E_PERM, file_mkdir('')
      assert_equal E_PERM, file_rmdir('')
      assert_equal E_PERM, file_list('')
      assert_equal E_PERM, file_list('', 1)
      assert_equal E_PERM, file_type('')
      assert_equal E_PERM, file_mode('')
      assert_equal E_PERM, file_chmod('', '')
      # ...well, except for file_version()
      assert_not_equal E_PERM, file_version()
    end
  end

  def test_that_opening_closing_reading_and_writing_a_text_file_works
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-tn')
      file_writeline(fh, "one two three four five")
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-tn')
      line = file_readline(fh)
      file_close(fh)
      assert_equal 'one two three four five', line
      assert_equal 24, file_size('test_fileio.tmp')
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_opening_closing_reading_and_writing_a_binary_file_works
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-bn')
      file_write(fh, '~A7~CE~F7~8E~0C~D2~16~C9~F6~F2~01~19')
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-bn')
      line = file_read(fh, 12)
      file_close(fh)
      assert_equal '~A7~CE~F7~8E~0C~D2~16~C9~F6~F2~01~19', line
      assert_equal 12, file_size('test_fileio.tmp')
      file_remove('test_fileio.tmp')
    end
  end

  # In both tests below the null byte is intentional.

  def test_that_reading_binary_data_in_text_mode_ignores_the_binary_data
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-bn')
      file_write(fh, 'abc~A7~CE~F7~8E~00~D2~16~C9~F6~F2~01~19123~0A')
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-tn')
      line = file_readline(fh)
      file_close(fh)
      assert_equal 'abc123', line
      file_remove('test_fileio.tmp')
    end
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-bn')
      file_write(fh, 'abc~A7~CE~F7~8E~00~D2~16~C9~F6~F2~01~19123')
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-tn')
      line = file_readline(fh)
      file_close(fh)
      assert_equal 'abc123', line
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_writing_an_invalid_binary_string_fails
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-bn')
      file_writeline(fh, 'abc~ZZ')
      file_write(fh, 'abc~ZZ')
      file_close(fh)
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_readlines_reads_lines
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-tn')
      file_writeline(fh, 'one')
      file_writeline(fh, 'two')
      file_writeline(fh, 'three')
      file_writeline(fh, 'four')
      file_writeline(fh, 'five')
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-tn')
      assert_equal E_INVARG, file_readlines(fh, 0, 4)
      assert_equal E_INVARG, file_readlines(fh, 5, 1)
      lines = file_readlines(fh, 1, 500)
      file_close(fh)
      assert_equal(['one', 'two', 'three', 'four', 'five'], lines)
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_readlines_reads_blank_lines
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-tn')
      file_writeline(fh, '')
      file_writeline(fh, '')
      file_writeline(fh, '')
      file_writeline(fh, '')
      file_writeline(fh, '')
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-tn')
      assert_equal E_INVARG, file_readlines(fh, 0, 4)
      assert_equal E_INVARG, file_readlines(fh, 5, 1)
      lines = file_readlines(fh, 1, 500)
      file_close(fh)
      assert_equal(['', '', '', '', ''], lines)
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_readlines_reads_lines_written_with_write
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-tn')
      file_write(fh, 'one')
      file_write(fh, 'two')
      file_write(fh, 'three')
      file_write(fh, 'four')
      file_write(fh, 'five')
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-tn')
      lines = file_readlines(fh, 1, 5)
      file_close(fh)
      assert_equal('onetwothreefourfive', lines)
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_reading_text_in_binary_mode_is_ok
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-tn')
      file_writeline(fh, "one two three four five")
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-bn')
      line = file_read(fh, 24)
      file_close(fh)
      assert_equal 'one two three four five~0A', line
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_writing_and_reading_a_very_long_file_in_text_mode_works
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-tn')
      10.times { file_writeline(fh, '1234567890' * 1000) }
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-tn')
      output = command %|; return file_readlines(#{fh}, 1, 100);|
      file_close(fh)
      assert_equal("{1, {" + "\"#{'1234567890' * 1000}\", " * 9 + "\"#{'1234567890' * 1000}\"" + "}}", output)
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_writing_and_reading_a_very_long_file_in_binary_mode_works
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-bn')
      10.times { file_write(fh, '1234567890' * 1000) }
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-bn')
      output = command %|; return file_read(#{fh}, 100000);|
      file_close(fh)
      assert_equal("{1, \"#{'1234567890' * 10000}\"}", output)
      file_remove('test_fileio.tmp')
    end
  end

  # I don't necessarily agree with the output of the following two
  # tests, but at least the semantics are clear.

  def test_that_reading_a_zero_length_file_in_text_mode_is_an_error
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-tn')
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-tn')
      line = file_readline(fh)
      eof = file_eof(fh)
      file_close(fh)
      assert_equal E_FILE, line
      assert_equal 1, eof
      file_remove('test_fileio.tmp')
    end
  end

  def test_that_reading_a_zero_length_file_in_binary_mode_is_an_error
    run_test_as('wizard') do
      fh = file_open('test_fileio.tmp', 'w-bn')
      file_close(fh)
      fh = file_open('test_fileio.tmp', 'r-bn')
      line = file_read(fh, 100)
      eof = file_eof(fh)
      file_close(fh)
      assert_equal E_FILE, line
      assert_equal 1, eof
      file_remove('test_fileio.tmp')
    end
  end

end
