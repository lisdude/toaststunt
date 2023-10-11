require 'socket'
require 'yaml'

require 'moo_obj'
require 'moo_err'
require 'general_expression_parser'
require 'fast_error_parser'

class String

  def binary_string_to_binary
    self.gsub(/~([a-fA-F0-9]{2})/) do
      [$1].pack('H*')
    end
  end

end

module MooSupport

  NOTHING = MooObj.new('#-1')
  AMBIGUOUS_MATCH = MooObj.new('#-2')
  FAILED_MATCH = MooObj.new('#-3')
  SYSTEM = MooObj.new('#0')
  OBJECT = MooObj.new('#1')
  ANONYMOUS = MooObj.new('#5')
  WAIF = MooObj.new('#7')

  E_NONE = MooErr.new('E_NONE')
  E_TYPE = MooErr.new('E_TYPE')
  E_DIV = MooErr.new('E_DIV')
  E_PERM = MooErr.new('E_PERM')
  E_PROPNF = MooErr.new('E_PROPNF')
  E_VERBNF = MooErr.new('E_VERBNF')
  E_VARNF = MooErr.new('E_VARNF')
  E_INVIND = MooErr.new('E_INVIND')
  E_RECMOVE = MooErr.new('E_RECMOVE')
  E_MAXREC = MooErr.new('E_MAXREC')
  E_RANGE = MooErr.new('E_RANGE')
  E_ARGS = MooErr.new('E_ARGS')
  E_NACC = MooErr.new('E_NACC')
  E_INVARG = MooErr.new('E_INVARG')
  E_QUOTA = MooErr.new('E_QUOTA')
  E_FLOAT = MooErr.new('E_FLOAT')
  E_FILE = MooErr.new('E_FILE')
  E_EXEC = MooErr.new('E_EXEC')
  E_INTRPT = MooErr.new('E_INTRPT')

  TYPE_INT = 0
  TYPE_OBJ = 1
  TYPE_ANON = 12

  raise '"./test.yml" configuration file not found' unless File.exist?('./test.yml')

  @@options = YAML.load(File.open('./test.yml'))

  @@options['verbose'] = true if ARGV.include?('--loud')
  @@options['verbose'] = false if ARGV.include?('--quiet')

  protected

  def options
    @@options
  end

  def send_string(string)
    puts "> " + string if options['verbose']
    @sock.puts string
  end

  def reset
    # The helpers below are unchanged during a test run.
    # Reset them before running a new test.
    @me = nil
    @here = nil
  end

  def run_test_with_prefix_and_suffix_as(*params)
    reset
    @host = options['host']
    @port = options['port']
    @sock = TCPSocket.open @host, @port
    send_string "connect #{params.join(' ')}"
    # I haven't found a better solution than both run_test_as/
    # run_test_with_prefix_and_suffix_as.  I need to handle both
    # commands and the evaluation of moocode (by far the most
    # common case) which suspends (not a common, but still more
    # common than commands).
    send_string "PREFIX -=!-^-!=-"
    send_string "SUFFIX -=!-v-!=-"
    begin
      yield
    rescue
      @sock.close
      raise $!
    end
  end

  def run_test_as(*params)
    reset
    @host = options['host']
    @port = options['port']
    @sock = TCPSocket.open @host, @port
    send_string "connect #{params.join(' ')}"
    begin
      yield
    rescue
      @sock.close
      raise $!
    end
  end

  def command(command)
    raise 'failed to enclose test in "run_test_as" block' unless @sock
    send_string command

    acc = []
    state = :looking
    while (state != :done)
      line = @sock.gets.chomp
      state = :found and next if line == '-=!-^-!=-' and (state == :looking or state == :found)
      state = :done and next if line == '-=!-v-!=-' and state == :found
      acc << line and next if state == :found
    end
    acc.each { |a| puts "< " + a } if options['verbose']
    acc.length > 0 ? acc.length > 1 ? acc : acc[0] : nil
  end

  def simplify(result)
    result ? (result[1] == ?2 ? fast_error_parse(result) : general_expression_parse(result)) : false
  end

  def true_or_false(result)
    result == 0 || result == "" || result == [] || result == {} ? false : true
  end

  def evaluate(expression)
    simplify command "; return #{expression};"
  end

  def _(wrapped)
    simplify(command(%Q|; return #{wrapped};|))
  end

  ## Support

  def nothing
    NOTHING
  end

  def invalid_object
    simplify command %Q|; return toobj(2147483647);|
  end

  def me
    @me ||= simplify command %Q|; return player;|
  end

  alias player me

  def here
    @here ||= simplify command %Q|; return player.location;|
  end

  alias location here

  def set(object, property, value)
    simplify command %|; return #{obj_ref(object)}.#{property} = #{value_ref(value)};|
  end

  def get(object, property)
    simplify command %|; return #{obj_ref(object)}.#{property};|
  end

  def call(object, verb, *args)
    simplify command %|; return #{obj_ref(object)}:#{verb}(#{args.map{|a| value_ref(a)}.join(', ')});|
  end

  def eval(string)
    simplify command %|; #{string}; |
  end

  ## Manipulating MOO Values

  def length(value)
    simplify command %Q|; return length(#{value_ref(value)});|
  end

  def is_member(value, collection)
    simplify command %Q|; return is_member(#{value_ref(value)}, #{value_ref(collection)});|
  end

  ### General Operations Applicable to all Values

  def typeof(value)
    simplify command %Q|; return typeof(#{value_ref(value)});|
  end

  def tostr(value)
    simplify command %Q|; return tostr(#{value_ref(value)});|
  end

  def toliteral(value)
    simplify command %Q|; return toliteral(#{value_ref(value)});|
  end

  def value_hash(str, algo = nil, binary = nil)
    if binary
      simplify command %Q|; return value_hash(#{value_ref(str)}, #{value_ref(algo)}, #{value_ref(binary)});|
    elsif algo
      simplify command %Q|; return value_hash(#{value_ref(str)}, #{value_ref(algo)});|
    else
      simplify command %Q|; return value_hash(#{value_ref(str)});|
    end
  end

  def value_hmac(str, key, algo = nil, binary = nil)
    if binary
      simplify command %Q|; return value_hmac(#{value_ref(str)}, #{value_ref(key)}, #{value_ref(algo)}, #{value_ref(binary)});|
    elsif algo
      simplify command %Q|; return value_hmac(#{value_ref(str)}, #{value_ref(key)}, #{value_ref(algo)});|
    else
      simplify command %Q|; return value_hmac(#{value_ref(str)}, #{value_ref(key)});|
    end
  end

  ### Operations on Numbers

  def random_bytes(count)
    simplify command %Q|; return random_bytes(#{value_ref(count)});|
  end

  def random(max = nil)
    if max
      simplify command %Q|; return random(#{value_ref(max)});|
    else
      simplify command %Q|; return random();|
    end
  end

  ### Operations on Strings

  def strtr(source, from, to, case_matters = nil)
    if case_matters
      simplify command %Q|; return strtr(#{value_ref(source)}, #{value_ref(from)}, #{value_ref(to)}, #{value_ref(case_matters)});|
    else
      simplify command %Q|; return strtr(#{value_ref(source)}, #{value_ref(from)}, #{value_ref(to)});|
    end
  end

  def index(source, what, case_matters = nil, offset = nil)
    if offset
      simplify command %Q|; return index(#{value_ref(source)}, #{value_ref(what)}, #{value_ref(case_matters)}, #{value_ref(offset)});|
    elsif case_matters
      simplify command %Q|; return index(#{value_ref(source)}, #{value_ref(what)}, #{value_ref(case_matters)});|
    else
      simplify command %Q|; return index(#{value_ref(source)}, #{value_ref(what)});|
    end
  end

  def rindex(source, what, case_matters = nil, offset = nil)
    if offset
      simplify command %Q|; return rindex(#{value_ref(source)}, #{value_ref(what)}, #{value_ref(case_matters)}, #{value_ref(offset)});|
    elsif case_matters
      simplify command %Q|; return rindex(#{value_ref(source)}, #{value_ref(what)}, #{value_ref(case_matters)});|
    else
      simplify command %Q|; return rindex(#{value_ref(source)}, #{value_ref(what)});|
    end
  end

  def encode_binary(str)
    simplify command %Q|; return encode_binary(#{value_ref(str)});|
  end

  def decode_binary(str)
    simplify command %Q|; return decode_binary(#{value_ref(str)});|
  end

  def encode_base64(str, safe = nil)
    if safe
      simplify command %Q|; return encode_base64(#{value_ref(str)}, #{value_ref(safe)});|
    else
      simplify command %Q|; return encode_base64(#{value_ref(str)});|
    end
  end

  def decode_base64(str, safe = nil)
    if safe
      simplify command %Q|; return decode_base64(#{value_ref(str)}, #{value_ref(safe)});|
    else
      simplify command %Q|; return decode_base64(#{value_ref(str)});|
    end
  end

  def string_hash(str, algo = nil, binary = nil)
    if binary
      simplify command %Q|; return string_hash(#{value_ref(str)}, #{value_ref(algo)}, #{value_ref(binary)});|
    elsif algo
      simplify command %Q|; return string_hash(#{value_ref(str)}, #{value_ref(algo)});|
    else
      simplify command %Q|; return string_hash(#{value_ref(str)});|
    end
  end

  def binary_hash(str, algo = nil, binary = nil)
    if binary
      simplify command %Q|; return binary_hash(#{value_ref(str)}, #{value_ref(algo)}, #{value_ref(binary)});|
    elsif algo
      simplify command %Q|; return binary_hash(#{value_ref(str)}, #{value_ref(algo)});|
    else
      simplify command %Q|; return binary_hash(#{value_ref(str)});|
    end
  end

  def string_hmac(str, key, algo = nil, binary = nil)
    if binary
      simplify command %Q|; return string_hmac(#{value_ref(str)}, #{value_ref(key)}, #{value_ref(algo)}, #{value_ref(binary)});|
    elsif algo
      simplify command %Q|; return string_hmac(#{value_ref(str)}, #{value_ref(key)}, #{value_ref(algo)});|
    else
      simplify command %Q|; return string_hmac(#{value_ref(str)}, #{value_ref(key)});|
    end
  end

  def binary_hmac(str, key, algo = nil, binary = nil)
    if binary
      simplify command %Q|; return binary_hmac(#{value_ref(str)}, #{value_ref(key)}, #{value_ref(algo)}, #{value_ref(binary)});|
    elsif algo
      simplify command %Q|; return binary_hmac(#{value_ref(str)}, #{value_ref(key)}, #{value_ref(algo)});|
    else
      simplify command %Q|; return binary_hmac(#{value_ref(str)}, #{value_ref(key)});|
    end
  end

  def salt(prefix, input)
    simplify command %Q|; return salt(#{value_ref(prefix)}, #{value_ref(input)});|
  end

  def crypt(key, setting)
    simplify command %Q|; return crypt(#{value_ref(key)}, #{value_ref(setting)});|
  end

  ### Operations on Maps

  def mapkeys(map)
    simplify command %Q|; return mapkeys(#{value_ref(map)});|
  end

  def mapvalues(map)
    simplify command %Q|; return mapvalues(#{value_ref(map)});|
  end

  def mapdelete(map, key)
    simplify command %Q|; return mapdelete(#{value_ref(map)}, #{value_ref(key)});|
  end

  ## Manipulating Objects
  ### Fundamental Operations on Objects

  def create(*args)
    simplify command %Q|; return create(#{args.map{|a| value_ref(a)}.join(', ')});|
  end

  def valid(object)
    true_or_false simplify command %Q|; return valid(#{obj_ref(object)});|
  end

  def chparent(object, parent, anons = [])
    unless anons.empty?
      simplify command %Q|; return chparent(#{obj_ref(object)}, #{obj_ref(parent)}, {#{anons.map{|a| obj_ref(a)}.join(', ')}});|
    else
      simplify command %Q|; return chparent(#{obj_ref(object)}, #{obj_ref(parent)});|
    end
  end

  def chparents(object, parents, anons = [])
    unless anons.empty?
      simplify command %Q|; return chparents(#{obj_ref(object)}, {#{parents.map{|a| obj_ref(a)}.join(', ')}}, {#{anons.map{|a| obj_ref(a)}.join(', ')}});|
    else
      simplify command %Q|; return chparents(#{obj_ref(object)}, {#{parents.map{|a| obj_ref(a)}.join(', ')}});|
    end
  end

  def parent(*args)
    simplify command %Q|; return parent(#{args.map{|a| obj_ref(a)}.join(', ')});|
  end

  def parents(*args)
    simplify command %Q|; return parents(#{args.map{|a| obj_ref(a)}.join(', ')});|
  end

  def children(object)
    simplify command %Q|; return children(#{obj_ref(object)});|
  end

  def ancestors(object, *args)
    unless args.empty?
      simplify command %Q|; return ancestors(#{obj_ref(object)}, #{args.map{|a| value_ref(a)}.join(', ')});|
    else
      simplify command %Q|; return ancestors(#{obj_ref(object)});|
    end
  end

  def descendants(object, *args)
    unless args.empty?
      simplify command %Q|; return descendants(#{obj_ref(object)}, #{args.map{|a| value_ref(a)}.join(', ')});|
    else
      simplify command %Q|; return descendants(#{obj_ref(object)});|
    end
  end

  def isa(object, parent)
    simplify command %Q|; return isa(#{obj_ref(object)}, #{obj_ref(parent)});|
  end

  def recycle(object)
    simplify command %Q|; return recycle(#{obj_ref(object)});|
  end

  def max_object
    simplify command %Q|; return max_object();|
  end

  ### Object Movement

  def move(what, where)
    simplify command %Q|; return move(#{obj_ref(what)}, #{obj_ref(where)});|
  end

  ### Operations on Properties

  def properties(object)
    simplify command %Q|; return properties(#{obj_ref(object)});|
  end

  def add_property(object, property, value, property_info)
    property_info = property_info_to_s(property_info)
    simplify command %Q|; return add_property(#{obj_ref(object)}, #{value_ref(property)}, #{value_ref(value)}, #{property_info});|
  end

  def property_info(object, property)
    simplify command %Q|; return property_info(#{obj_ref(object)}, #{value_ref(property)});|
  end

  def set_property_info(object, property, property_info)
    property_info = property_info_to_s(property_info)
    simplify command %Q|; return set_property_info(#{obj_ref(object)}, #{value_ref(property)}, #{property_info});|
  end

  def is_clear_property(object, property)
    simplify command %Q|; return is_clear_property(#{obj_ref(object)}, #{value_ref(property)});|
  end

  def clear_property(object, property)
    simplify command %Q|; return clear_property(#{obj_ref(object)}, #{value_ref(property)});|
  end

  def delete_property(object, property)
    simplify command %Q|; return delete_property(#{obj_ref(object)}, #{value_ref(property)});|
  end

  ### Operations on Verbs

  def verbs(object)
    simplify command %Q|; return verbs(#{obj_ref(object)});|
  end

  def add_verb(object, verb_info, verb_args)
    verb_info = verb_info_to_s(verb_info)
    verb_args = verb_args_to_s(verb_args)
    simplify command %Q|; return add_verb(#{obj_ref(object)}, #{verb_info}, #{verb_args});|
  end

  def verb_info(object, verb)
    simplify command %Q|; return verb_info(#{obj_ref(object)}, #{value_ref(verb)});|
  end

  def verb_args(object, verb)
    simplify command %Q|; return verb_args(#{obj_ref(object)}, #{value_ref(verb)});|
  end

  def set_verb_info(object, verb, verb_info)
    verb_info = verb_info_to_s(verb_info)
    simplify command %Q|; return set_verb_info(#{obj_ref(object)}, #{value_ref(verb)}, #{verb_info});|
  end

  def set_verb_args(object, verb, verb_args)
    verb_args = verb_args_to_s(verb_args)
    simplify command %Q|; return set_verb_args(#{obj_ref(object)}, #{value_ref(verb)}, #{verb_args});|
  end

  def verb_code(object, verb)
    simplify command %Q|; return verb_code(#{obj_ref(object)}, #{value_ref(verb)});|
  end

  def set_verb_code(object, verb, vc = [])
    if block_given?
      yield vc
    end
    code = ''
    vc.each { |v| code << v.inspect << ',' }
    code = code.empty? ? '{,' : '{' + code
    code[-1] = '}'
    simplify command %Q|; return set_verb_code(#{object}, #{value_ref(verb)}, #{code});|
  end

  def delete_verb(object, verb)
    simplify command %Q|; return delete_verb(#{obj_ref(object)}, #{value_ref(verb)});|
  end

  def respond_to(object, verb)
    simplify command %Q|; return respond_to(#{obj_ref(object)}, #{value_ref(verb)});|
  end

  def disassemble(object, verb)
    simplify command %Q|; return disassemble(#{obj_ref(object)}, #{value_ref(verb)});|
  end

  ## Operations on Network Connections

  def read(connection = nil, non_blocking = nil)
    if (connection && non_blocking)
      simplify command %Q|; return read(#{value_ref(connection)}, #{value_ref(non_blocking)});|
    elsif (connection)
      simplify command %Q|; return read(#{value_ref(connection)});|
    else
      simplify command %Q|; return read();|
    end
  end

  def read_http(connection = nil, type = nil)
    if (connection && type)
      simplify command %Q|; return read_http(#{value_ref(connection)}, #{value_ref(type)});|
    elsif (connection)
      simplify command %Q|; return read_http(#{value_ref(connection)});|
    else
      simplify command %Q|; return read_http();|
    end
  end

  def set_connection_option(connection, option, value)
    simplify command %Q|; return set_connection_option(#{value_ref(connection)}, #{value_ref(option)}, #{value_ref(value)});|
  end

  def switch_player(old_player, new_player)
    command %Q|; switch_player(#{value_ref(old_player)}, #{value_ref(new_player)}); notify(#{value_ref(new_player)}, "{-1, 0}"); notify(#{value_ref(new_player)}, "-=!-v-!=-");|
  end

  def boot_player(player)
    simplify command %Q|; return boot_player(#{value_ref(player)});|
  end

  ## MOO-Code Evaluation and Task Manipulation

  def function_info(name)
    simplify command %Q|; return function_info("#{name}");|
  end

  def has_function?(name)
    true_or_false simplify command %Q|; return `function_info("#{name}") ! E_INVARG => {}';|
  end

  def queued_tasks()
    simplify command %Q|; return queued_tasks();|
  end

  def kill_task(task_id)
    simplify command %Q|; return kill_task(#{value_ref(task_id)});|
  end

  def resume(task_id, value = nil)
    if (value)
      simplify command %Q|; return resume(#{value_ref(task_id)}, #{value_ref(value)});|
    else
      simplify command %Q|; return resume(#{value_ref(task_id)});|
    end
  end

  def callers()
    simplify command %Q|; return callers();|
  end

  def task_stack(task_id)
    simplify command %Q|; return task_stack(#{value_ref(task_id)});|
  end

  def set_task_local(value)
    simplify command %Q|; return set_task_local(#{value_ref(value)});|
  end

  def task_local()
    simplify command %Q|; return task_local();|
  end

  ## Administrative Operations

  def reset_max_object
    simplify command %|; return reset_max_object();|
  end

  def renumber(object)
    simplify command %|; return renumber(#{obj_ref(object)});|
  end

  def server_log(message)
    simplify command %|; return server_log(#{value_ref(message)});|
  end

  def shutdown
    simplify command %|; shutdown();|
  end

  def run_gc
    simplify command %|; return run_gc();|
  end

  def gc_stats
    simplify command %|; return gc_stats();|
  end

  ## Server Statistics and Miscellaneous Information

  def verb_cache_stats
    simplify command %|; return verb_cache_stats();|
  end

  ## FileIO Operations

  def file_version
    simplify command %|; return file_version();|
  end

  def file_open(pathname, mode)
    simplify command %|; return file_open(#{value_ref(pathname)}, #{value_ref(mode)});|
  end

  def file_close(fh)
    simplify command %|; return file_close(#{value_ref(fh)});|
  end

  def file_name(fh)
    simplify command %|; return file_name(#{value_ref(fh)});|
  end

  def file_openmode(fh)
    simplify command %|; return file_openmode(#{value_ref(fh)});|
  end

  def file_readline(fh)
    simplify command %|; return file_readline(#{value_ref(fh)});|
  end

  def file_readlines(fh, s, e)
    simplify command %|; return file_readlines(#{value_ref(fh)}, #{value_ref(s)}, #{value_ref(e)});|
  end

  def file_writeline(fh, line)
    simplify command %|; return file_writeline(#{value_ref(fh)}, #{value_ref(line)});|
  end

  def file_read(fh, bytes)
    simplify command %|; return file_read(#{value_ref(fh)}, #{value_ref(bytes)});|
  end

  def file_write(fh, data)
    simplify command %|; return file_write(#{value_ref(fh)}, #{value_ref(data)});|
  end

  def file_tell(fh)
    simplify command %|; return file_tell(#{value_ref(fh)});|
  end

  def file_seek(fh, loc, whence)
    simplify command %|; return file_seek(#{value_ref(fh)}, #{value_ref(loc)}, #{value_ref(whence)});|
  end

  def file_eof(fh)
    simplify command %|; return file_eof(#{value_ref(fh)});|
  end

  def file_size(value)
    simplify command %|; return file_size(#{value_ref(value)});|
  end

  def file_mode(value)
    simplify command %|; return file_mode(#{value_ref(value)});|
  end

  def file_last_access(value)
    simplify command %|; return file_last_access(#{value_ref(value)});|
  end

  def file_last_modify(value)
    simplify command %|; return file_last_modify(#{value_ref(value)});|
  end

  def file_last_change(value)
    simplify command %|; return file_last_change(#{value_ref(value)});|
  end

  def file_stat(value)
    simplify command %|; return file_stat(#{value_ref(value)});|
  end

  def file_rename(oldpath, newpath)
    simplify command %|; return file_rename(#{value_ref(oldpath)}, #{value_ref(newpath)});|
  end

  def file_remove(pathname)
    simplify command %|; return file_remove(#{value_ref(pathname)});|
  end

  def file_mkdir(pathname)
    simplify command %|; return file_mkdir(#{value_ref(pathname)});|
  end

  def file_rmdir(pathname)
    simplify command %|; return file_rmdir(#{value_ref(pathname)});|
  end

  def file_list(pathname, detailed = nil)
    if detailed
      simplify command %|; return file_list(#{value_ref(pathname)}, #{value_ref(detailed)});|
    else
      simplify command %|; return file_list(#{value_ref(pathname)});|
    end
  end

  def file_type(pathname)
    simplify command %|; return file_type(#{value_ref(pathname)});|
  end

  def file_mode(filename)
    simplify command %|; return file_mode(#{value_ref(filename)});|
  end

  def file_chmod(filename, mode)
    simplify command %|; return file_chmod(#{value_ref(filename)}, #{value_ref(mode)});|
  end

  ## Exec Operations

  def exec(args, input = nil)
    if input
      simplify command %|; return exec(#{value_ref(args)}, #{value_ref(input)});|
    else
      simplify command %|; return exec(#{value_ref(args)});|
    end
  end

  ## System Operations

  def getenv(name)
    simplify command %|; return getenv(#{value_ref(name)});|
  end

  protected

  def obj_ref(obj_ref)
    case obj_ref
    when Integer
      "##{obj_ref.to_s}"
    when Symbol
      "$#{obj_ref.to_s}"
    else
      obj_ref
    end
  end

  def obj_ref_or_list_of_obj_refs(complex)
    case complex
    when Array
      '{' + complex.map { |o| obj_ref(o) }.join(', ') + '}'
    else
      obj_ref(complex)
    end
  end

  def value_ref(value)
    case value
    when String
      "\"#{value.gsub('\\', '\\\\\\\\').gsub('"', '\"')}\""
    when Symbol
      "$#{value.to_s}"
    when MooErr
      value.to_s
    when MooObj
      value.to_s
    when Array
      '{' + value.map { |o| value_ref(o) }.join(', ') + '}'
    when Hash
      '[' + value.map { |k, v| "#{value_ref(k)} -> #{value_ref(v)}" }.join(', ') + ']'
    else
      value
    end
  end

  private

  def property_info_to_s(property_info)
    unless property_info.class == String
      owner, flags = property_info
      property_info = %Q|{#{obj_ref(owner)}, "#{flags}"}|
    end
    property_info
  end

  def verb_info_to_s(verb_info)
    unless verb_info.class == String
      owner, flags, name = verb_info
      verb_info = %Q|{#{obj_ref(owner)}, "#{flags}", "#{name}"}|
    end
    verb_info
  end

  def verb_args_to_s(verb_args)
    unless verb_args.class == String
      dobj, prep, iobj = verb_args
      verb_args = %Q|{"#{dobj}", "#{prep}", "#{iobj}"}|
    end
    verb_args
  end

  GENERAL_EXPRESSION_PARSER = GeneralExpressionParser.new
  GENERAL_EXPRESSION_TRANSFORM = GeneralExpressionTransform.new

  def general_expression_parse(str)
    GENERAL_EXPRESSION_TRANSFORM.apply(GENERAL_EXPRESSION_PARSER.parse(str))[1]
  end

  FAST_ERROR_PARSER = FastErrorParser.new
  FAST_ERROR_TRANSFORM = FastErrorTransform.new

  def fast_error_parse(str)
    FAST_ERROR_TRANSFORM.apply(FAST_ERROR_PARSER.parse(str))
  end

end
