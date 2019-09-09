require 'test_helper'

# Run the tests in test/basic
# Props to Elly Fong-Jones (https://github.com/elly) for the
# test files.

class TestBasic < Test::Unit::TestCase

  Dir['test/basic/*'].each do |dir|
    define_method((['test', 'everything', 'in'] + dir.split('/')[1..-1]).join('_')) do
      run_test_as('wizard') do
        Dir["#{dir}/*in"].each do |test|
          inp = open(test)
          outp = open(test.gsub(/in$/, 'out'))
          while (inl = inp.gets)
            assert_equal outp.gets.chomp, simplify(command(%Q|; return toliteral(#{inl.chomp});|))
          end
          inp.close
          outp.close
        end
      end
    end
  end

end
