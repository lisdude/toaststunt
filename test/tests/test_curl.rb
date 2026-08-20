require 'test_helper'
require 'socket'
require 'zlib'

# Tests for the curl() builtin.  These require the server to be built with
# libcurl (CURL_FOUND) and started with outbound networking enabled (the
# default, or -o); the whole file is skipped otherwise.
class TestCurl < Test::Unit::TestCase

  SimpleRequest = Struct.new(:method, :path, :headers, :body)

  @@curl_available = nil

  def setup
    omit('curl() unavailable (not compiled in, or outbound networking disabled)') unless curl_available?
  end

  def test_that_non_wizards_can_not_call_curl
    run_test_as('programmer') do
      assert_equal E_PERM, curl('"http://example.com/"')
    end
  end

  def test_that_a_basic_get_works
    with_http_server([response('200 OK', 'hello world')]) do |port, requests|
      run_test_as('wizard') do
        assert_equal 'hello world', curl(%|"http://127.0.0.1:#{port}/basic"|)
      end
      assert_equal 'GET', requests[0].method
      assert_equal '/basic', requests[0].path
    end
  end

  def test_that_the_legacy_include_headers_form_still_works
    with_http_server([response('200 OK', 'payload')]) do |port, requests|
      run_test_as('wizard') do
        r = curl(%|"http://127.0.0.1:#{port}/", 1|)
        assert r.index('HTTP/1.1 200 OK'), "headers not included in #{r.inspect}"
        assert r.index('payload'), "body missing from #{r.inspect}"
      end
    end
  end

  def test_that_non_map_legacy_truthy_values_still_include_headers
    with_http_server([response('200 OK', 'payload')]) do |port, requests|
      run_test_as('wizard') do
        r = curl(%|"http://127.0.0.1:#{port}/", {"legacy truthy value"}|)
        assert r.index('HTTP/1.1 200 OK'), "headers not included in #{r.inspect}"
        assert r.index('payload'), "body missing from #{r.inspect}"
      end
    end
  end

  def test_that_dict_remains_available_with_an_options_map
    with_dict_server do |port, commands|
      run_test_as('wizard') do
        r = curl(%|"dict://127.0.0.1:#{port}/d:toast", ["timeout" -> 5, "max_size" -> 4096]|)
        assert r.index('definition body'), "DICT response missing from #{r.inspect}"
      end
      assert commands.any? { |line| line.start_with?('DEFINE ') }, commands.inspect
    end
  end

  def test_that_post_with_a_body_works
    with_http_server([response('200 OK', 'created')]) do |port, requests|
      run_test_as('wizard') do
        assert_equal 'created', curl(%|"http://127.0.0.1:#{port}/submit", ["body" -> "name=toast"]|)
      end
      assert_equal 'POST', requests[0].method
      assert_equal 'name=toast', requests[0].body
    end
  end

  def test_that_explicit_methods_work
    with_http_server([response('200 OK', 'ok'), response('200 OK', 'ok')]) do |port, requests|
      run_test_as('wizard') do
        curl(%|"http://127.0.0.1:#{port}/thing", ["method" -> "delete"]|)
        curl(%|"http://127.0.0.1:#{port}/thing", ["method" -> "PUT", "body" -> "data"]|)
      end
      assert_equal 'DELETE', requests[0].method
      assert_equal 'PUT', requests[1].method
      assert_equal 'data', requests[1].body
    end
  end

  def test_that_binary_string_bodies_are_decoded
    with_http_server([response('200 OK', 'ok')]) do |port, requests|
      run_test_as('wizard') do
        curl(%|"http://127.0.0.1:#{port}/", ["body" -> "line1~0Aline2"]|)
      end
      assert_equal "line1\nline2", requests[0].body
    end
  end

  def test_that_request_headers_are_sent
    with_http_server([response('200 OK', 'ok')]) do |port, requests|
      run_test_as('wizard') do
        curl(%|"http://127.0.0.1:#{port}/", ["headers" -> ["X-Custom" -> "hi there", "Authorization" -> "Bearer token123"]]|)
      end
      assert_equal 'hi there', requests[0].headers['x-custom']
      assert_equal 'Bearer token123', requests[0].headers['authorization']
    end
  end

  def test_that_list_valued_headers_are_sent_repeatedly
    with_http_server([response('200 OK', 'ok')]) do |port, requests|
      run_test_as('wizard') do
        curl(%|"http://127.0.0.1:#{port}/", ["headers" -> ["X-Many" -> {"one", "two"}]]|)
      end
      assert_equal ['one', 'two'], requests[0].headers['x-many']
    end
  end

  def test_that_header_injection_is_rejected
    run_test_as('wizard') do
      # Names and values containing CR/LF are rejected outright, as are
      # names containing non-token characters.  (The first two are wrapped
      # in a catch because the raised error carries the offending name,
      # whose CR/LF would otherwise mangle the test protocol.)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["headers" -> ["X-Bad" + chr(13) + chr(10) + "Injected" -> "v"]]|, catch: true)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["headers" -> ["X-Bad" -> "v" + chr(13) + chr(10) + "Injected: 1"]]|, catch: true)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["headers" -> ["X Spaced Name" -> "v"]]|)
    end
  end

  def test_that_the_json_request_option_serializes_and_sets_content_type
    with_http_server([response('200 OK', 'ok')]) do |port, requests|
      run_test_as('wizard') do
        curl(%|"http://127.0.0.1:#{port}/api", ["json" -> ["key" -> "value", "n" -> 5]]|)
      end
      assert_equal 'POST', requests[0].method
      assert_equal 'application/json', requests[0].headers['content-type']
      assert_equal '{"key":"value","n":5}', requests[0].body
    end
  end

  def test_that_parse_returns_parsed_json
    with_http_server([response('200 OK', '{"answer": 42, "list": [1, 2]}')]) do |port, requests|
      run_test_as('wizard') do
        assert_equal({'answer' => 42, 'list' => [1, 2]}, curl(%|"http://127.0.0.1:#{port}/api", ["parse" -> 1]|))
      end
    end
  end

  def test_that_parse_of_invalid_json_returns_an_error_map
    with_http_server([response('200 OK', 'this is not json')]) do |port, requests|
      run_test_as('wizard') do
        r = curl(%|"http://127.0.0.1:#{port}/", ["parse" -> 1]|)
        assert_equal E_INVARG, r['error']
      end
    end
  end

  def test_that_parse_rejects_comments_and_trailing_content
    with_http_server([
      response('200 OK', '12abc'),
      response('200 OK', '/* comment */ 12'),
      response('200 OK', "12\r\n")
    ]) do |port, requests|
      run_test_as('wizard') do
        assert_equal E_INVARG, curl(%|"http://127.0.0.1:#{port}/suffix", ["parse" -> 1]|)['error']
        assert_equal E_INVARG, curl(%|"http://127.0.0.1:#{port}/comment", ["parse" -> 1]|)['error']
        assert_equal 12, curl(%|"http://127.0.0.1:#{port}/whitespace", ["parse" -> 1]|)
      end
    end
  end

  def test_that_full_returns_status_headers_body_and_url
    with_http_server([response('404 Not Found', 'nope', ['X-Reason: missing'])]) do |port, requests|
      run_test_as('wizard') do
        r = curl(%|"http://127.0.0.1:#{port}/~gone", ["full" -> 1]|)
        assert_equal 404, r['status']
        assert_equal 'nope', r['body']
        assert_equal 'missing', r['headers']['X-Reason']
        assert_equal "http://127.0.0.1:#{port}/~gone", r['url']
      end
    end
  end

  def test_that_full_and_parse_compose
    with_http_server([response('200 OK', '{"ok": 1}')]) do |port, requests|
      run_test_as('wizard') do
        r = curl(%|"http://127.0.0.1:#{port}/", ["full" -> 1, "parse" -> 1]|)
        assert_equal 200, r['status']
        assert_equal({'ok' => 1}, r['body'])
      end
    end
  end

  def test_that_redirects_are_not_followed_by_default
    with_http_server([response('302 Found', '', ['Location: http://127.0.0.1:1/elsewhere'])]) do |port, requests|
      run_test_as('wizard') do
        assert_equal 302, curl(%|"http://127.0.0.1:#{port}/", ["full" -> 1]|)['status']
      end
    end
  end

  def test_that_follow_redirects_works
    with_http_server([response('200 OK', 'final destination')]) do |port2, requests2|
      with_http_server([response('302 Found', '', ["Location: http://127.0.0.1:#{port2}/landed"])]) do |port1, requests1|
        run_test_as('wizard') do
          r = curl(%|"http://127.0.0.1:#{port1}/", ["follow_redirects" -> 1, "full" -> 1]|)
          assert_equal 200, r['status']
          assert_equal 'final destination', r['body']
          assert_equal "http://127.0.0.1:#{port2}/landed", r['url']
        end
      end
    end
  end

  def test_that_redirected_posts_switch_cleanly_to_get
    with_http_server([response('200 OK', 'done')]) do |port2, requests2|
      with_http_server([response('302 Found', '', ["Location: http://127.0.0.1:#{port2}/target"])]) do |port1, requests1|
        run_test_as('wizard') do
          assert_equal 'done', curl(%|"http://127.0.0.1:#{port1}/start", ["body" -> "data", "follow_redirects" -> 1]|)
        end
        assert_equal 'POST', requests1[0].method
        assert_equal 'data', requests1[0].body
        assert_equal 'GET', requests2[0].method
        assert_equal '', requests2[0].body
      end
    end
  end

  def test_that_redirected_custom_methods_keep_their_bodies
    with_http_server([response('200 OK', 'done')]) do |port2, requests2|
      with_http_server([response('302 Found', '', ["Location: http://127.0.0.1:#{port2}/target"])]) do |port1, requests1|
        run_test_as('wizard') do
          assert_equal 'done', curl(%|"http://127.0.0.1:#{port1}/start", ["method" -> "PUT", "body" -> "data", "follow_redirects" -> 1]|)
        end
        assert_equal 'PUT', requests1[0].method
        assert_equal 'data', requests1[0].body
        assert_equal 'PUT', requests2[0].method
        assert_equal 'data', requests2[0].body
      end
    end
  end

  def test_that_oversized_responses_are_rejected
    with_http_server([response('200 OK', 'x' * 4096)]) do |port, requests|
      run_test_as('wizard') do
        r = curl(%|"http://127.0.0.1:#{port}/", ["max_size" -> 100]|)
        assert_equal E_QUOTA, r['error']
      end
    end
  end

  def test_that_invalid_server_response_caps_use_the_default
    with_http_server([response('200 OK', 'x' * (10 * 1024 * 1024 + 1))]) do |port, requests|
      run_test_as('wizard') do
        evaluate('add_property($server_options, "curl_max_response_bytes", 0, {player, "rw"})')
        begin
          assert_equal E_QUOTA, curl(%|"http://127.0.0.1:#{port}/", ["max_size" -> 20 * 1024 * 1024]|)['error']
        ensure
          evaluate('delete_property($server_options, "curl_max_response_bytes")')
        end
      end
    end
  end

  def test_that_the_limit_applies_to_the_decoded_response
    compressed = Zlib.gzip('a')
    with_http_server([response('200 OK', compressed, ['Content-Encoding: gzip'])]) do |port, requests|
      run_test_as('wizard') do
        assert_equal 'a', curl(%|"http://127.0.0.1:#{port}/", ["max_size" -> 10]|)
      end
    end
  end

  def test_that_invalid_options_are_rejected
    run_test_as('wizard') do
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["no_such_option" -> 1]|)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["method" -> "TRACE"]|)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["method" -> "GET", "body" -> "x"]|)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["body" -> "x", "json" -> "y"]|)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["timeout" -> 0]|)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["timeout" -> 99999]|)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["timeout" -> 5], 5|)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", ["include_headers" -> 1, "full" -> 1]|)
    end
  end

  def test_that_legacy_timeouts_out_of_range_are_rejected
    run_test_as('wizard') do
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", 0, 0|)
      assert_equal E_INVARG, curl(%q|"http://127.0.0.1:1/", 0, 99999|)
    end
  end

  def test_that_unsupported_protocols_are_rejected
    run_test_as('wizard') do
      assert_equal E_INVARG, curl(%q|"file:///etc/passwd", ["timeout" -> 2]|)['error']
      assert_equal E_INVARG, curl(%q|"gopher://127.0.0.1:1/", ["timeout" -> 2]|)['error']
    end
  end

  def test_that_curl_works_with_threading_disabled
    with_http_server([response('200 OK', 'sync')]) do |port, requests|
      run_test_as('wizard') do
        r = simplify command %|; set_thread_mode(0); return curl("http://127.0.0.1:#{port}/");|
        assert_equal 'sync', r
      end
    end
  end

  def test_that_repeated_response_headers_become_a_list
    with_http_server([response('200 OK', 'ok', ['X-Dup: a', 'X-Dup: b'])]) do |port, requests|
      run_test_as('wizard') do
        r = curl(%|"http://127.0.0.1:#{port}/", ["full" -> 1]|)
        assert_equal ['a', 'b'], r['headers']['X-Dup']
      end
    end
  end

  def test_that_the_user_agent_can_be_overridden
    with_http_server([response('200 OK', 'ok'), response('200 OK', 'ok')]) do |port, requests|
      run_test_as('wizard') do
        curl(%|"http://127.0.0.1:#{port}/"|)
        curl(%|"http://127.0.0.1:#{port}/", ["user_agent" -> "MyMOO/1.0"]|)
      end
      assert requests[0].headers['user-agent'] =~ /^ToastStunt\//, "default UA was #{requests[0].headers['user-agent'].inspect}"
      assert_equal 'MyMOO/1.0', requests[1].headers['user-agent']
    end
  end

  private

  # Evaluate `curl(<args>)` in the MOO, where `args` is raw MOO source for
  # the argument list.  With catch: true, the call is wrapped in a catch
  # expression so raised errors come back as plain error values.
  def curl(args, catch: false)
    expr = "curl(#{args})"
    expr = "`#{expr} ! ANY'" if catch
    simplify command %|; return #{expr};|
  end

  # A tiny single-purpose HTTP server.  `responses` is a list of raw
  # response strings handed out in order (one per connection); requests
  # received are collected into the array yielded alongside the port.
  def with_http_server(responses)
    server = TCPServer.new('127.0.0.1', 0)
    port = server.addr[1]
    requests = []
    thread = Thread.new do
      responses.each do |response|
        client = server.accept
        begin
          requests << read_request(client)
          client.write(response)
        ensure
          client.close rescue nil
        end
      end
    end
    begin
      yield port, requests
      thread.join(10)
    ensure
      server.close rescue nil
      thread.kill
    end
  end

  def with_dict_server
    server = TCPServer.new('127.0.0.1', 0)
    port = server.addr[1]
    commands = []
    thread = Thread.new do
      client = server.accept
      begin
        client.write("220 test dictionary server ready\r\n")
        while (line = client.gets)
          commands << line.chomp
          case line
          when /\ACLIENT /
            client.write("250 client accepted\r\n")
          when /\ADEFINE /
            client.write("150 1 definitions retrieved\r\n")
            client.write("151 \"toast\" test \"test dictionary\"\r\n")
            client.write("definition body\r\n.\r\n250 ok\r\n")
          when /\AQUIT/
            client.write("221 bye\r\n")
            break
          else
            client.write("500 unsupported command\r\n")
          end
        end
      ensure
        client.close rescue nil
      end
    end
    begin
      yield port, commands
      thread.join(10)
    ensure
      server.close rescue nil
      thread.kill
    end
  end

  def read_request(client)
    request_line = client.gets("\r\n").to_s.chomp
    method, path, = request_line.split(' ')
    headers = {}
    while (line = client.gets("\r\n").to_s.chomp) != ''
      name, value = line.split(':', 2)
      key = name.to_s.downcase
      headers[key] = headers.key?(key) ? [headers[key], value.to_s.strip].flatten : value.to_s.strip
    end
    body = headers['content-length'] ? client.read(headers['content-length'].to_i) : ''
    SimpleRequest.new(method, path, headers, body)
  end

  def response(status, body, extra_headers = [])
    lines = ["HTTP/1.1 #{status}", "Content-Length: #{body.bytesize}", "Connection: close"]
    lines += extra_headers
    lines.join("\r\n") + "\r\n\r\n" + body
  end

  # Probed once for the whole file (class-level), not per-test.
  def curl_available?
    return @@curl_available unless @@curl_available.nil?
    @@curl_available = false
    run_test_as('wizard') do
      unless simplify(command %q|; return `function_info("curl") ! E_INVARG => 0';|) == 0
        # curl() exists; a raised E_PERM here means outbound networking is off
        # (an error *map* -- connection refused -- means it is on).
        @@curl_available = curl(%q|"http://127.0.0.1:1/", ["timeout" -> 1]|) != E_PERM
      end
    end
    @@curl_available
  end

end
