require 'test_helper'

class TestHttp < Test::Unit::TestCase

  def test_that_non_wizards_can_not_call_the_no_arg_version_of_read_http
    run_test_as('programmer') do
      assert_equal E_PERM, read_http('request')
    end
  end

  def test_that_non_wizards_can_not_call_read_http_on_a_connection_they_do_not_own
    other = nil
    run_test_as('programmer') do
      other = player
    end
    run_test_as('programmer') do
      assert_equal E_PERM, read_http('response', other)
    end
  end

  def test_that_types_other_than_request_and_response_fail
    run_test_as('wizard') do
      assert_equal E_ARGS, read_http()
      assert_equal E_INVARG, read_http('foobar')
      assert_equal E_INVARG, read_http('')
    end
  end

  def test_that_forcing_bad_input_fails_gracefully
    run_test_as('wizard') do
      # Since this is an extreme edge-case, the server doesn't
      # work too hard -- it just returns zero (0) and moves on.
      result = parse(:request) do |message|
        message << "~ZZ~!!"
      end
      assert_equal 0, result

      result = parse(:request) do |message|
        message << "GET /bad HTTP/1.1~0D~0A"
        message << "~ZZ~!!"
      end
      assert_equal 0, result
    end
  end

  def test_that_bad_requests_fail
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << 'foobar~0D~0A'
      end
      assert_equal 'INVALID_METHOD', result['error'][0]
      result = parse(:request) do |message|
        message << "GET /~FFbad HTTP/1.1~0D~0A"
      end
      assert_equal 'INVALID_PATH', result['error'][0]
      result = parse(:request) do |message|
        message << "GET /bad HTTP/1.1~0D~0A"
        message << 'foo~00bar~0D~0A'
      end
      assert_equal 'INVALID_HEADER_TOKEN', result['error'][0]
    end
  end

  def test_that_bad_responses_fail
    run_test_as('wizard') do
      result = parse(:response) do |message|
        message << 'foobar~0D~0A'
      end
      assert_equal 'INVALID_CONSTANT', result['error'][0]
      result = parse(:response) do |message|
        message << "HTTP/1.1 2~00~00 Yow~0D~0A"
      end
      assert_equal 'INVALID_STATUS', result['error'][0]
      result = parse(:response) do |message|
        message << "HTTP/1.1 404 Not Found~0D~0A"
        message << 'foo~FFbar~0D~0A'
      end
      assert_equal 'INVALID_HEADER_TOKEN', result['error'][0]
    end
  end

  def test_that_it_is_invalid_to_read_from_a_connection_that_is_already_being_read
    run_test_as('wizard') do
      other = player
      task = simplify command %Q|; fork task (0); set_connection_option(#{other}, "hold-input", 1); read_http("request", #{other}); set_connection_option(#{other}, "hold-input", 0); endfork; return task;|
      # nest the following test to keep the original connection open
      run_test_as('wizard') do
        assert_equal E_INVARG, read(other)
        kill_task(task)
      end
    end
  end

  def test_that_we_can_kill_a_task_but_still_read_more_http_on_the_connection
    run_test_as('wizard') do
      other = player
      task = simplify command %Q|; fork task (0); set_connection_option(#{other}, "hold-input", 1); force_input(#{other}, "GET /1~0D~0Aone: two~0D~0A"); read_http("request", #{other}); set_connection_option(#{other}, "hold-input", 0); endfork; fork (0); suspend(); endfork; return task;|
      run_test_as('wizard') do
        kill_task(task)
        result = simplify command %Q|; set_connection_option(#{other}, "hold-input", 1); force_input(#{other}, "GET /2~0D~0Afoo: bar~0D~0A~0D~0A"); result = read_http("request", #{other}); set_connection_option(#{other}, "hold-input", 0); return result;|
        assert_equal 'GET', result['method']
        assert_equal '/2', result['uri']
        assert_equal({'foo' => 'bar'}, result['headers'])
        result = simplify command %Q|; set_connection_option(#{other}, "hold-input", 1); force_input(#{other}, "HTTP/1.1 500 Error~0D~0Afoo: bar~0D~0A~0D~0A"); result = read_http("response", #{other}); set_connection_option(#{other}, "hold-input", 0); return result;|
        assert_equal 500, result['status']
        assert_equal({'foo' => 'bar'}, result['headers'])
      end
    end
  end

  def test_that_we_can_abruptly_close_a_reading_connection
    run_test_as('wizard') do
      other = player
      task = simplify command %Q|; fork task (0); set_connection_option(#{other}, "hold-input", 1); force_input(#{other}, "GET /1~0D~0Aone: two~0D~0A"); read_http("request", #{other}); set_connection_option(#{other}, "hold-input", 0); endfork; fork (0); suspend(); endfork; return task;|
      run_test_as('wizard') do
        boot_player(other)
      end
    end
  end

  def test_that_chopped_up_requests_work
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "G"
        message << "E"
        message << "T"
        message << " "
        message << "/"
        message << "t"
        message << "e"
        message << "s"
        message << "t"
        message << " "
        message << "H"
        message << "T"
        message << "T"
        message << "P"
        message << "/"
        message << "1"
        message << "."
        message << "1"
        message << "~0D~0A"
        message << "h"
        message << "e"
        message << "a"
        message << "d"
        message << "e"
        message << "r"
        message << ":"
        message << " "
        message << "t"
        message << "e"
        message << "s"
        message << "t"
        message << "~0D~0A"
        message << "~0D~0A"
      end
      assert_equal 'GET', result['method']
      assert_equal '/test', result['uri']
      assert_equal({'header' => 'test'}, result['headers'])
    end
  end

  def test_that_chopped_up_responses_work
    run_test_as('wizard') do
      result = parse(:response) do |message|
        message << "H"
        message << "T"
        message << "T"
        message << "P"
        message << "/"
        message << "1"
        message << "."
        message << "1"
        message << " "
        message << "2"
        message << "0"
        message << "0"
        message << " "
        message << "O"
        message << "K"
        message << "~0D~0A"
        message << "h"
        message << "e"
        message << "a"
        message << "d"
        message << "e"
        message << "r"
        message << ":"
        message << " "
        message << "t"
        message << "e"
        message << "s"
        message << "t"
        message << "~0D~0A"
        message << "~0D~0A"
      end
      assert_equal 200, result['status']
      assert_equal({'header' => 'test'}, result['headers'])
    end
  end

  def test_that_curl_get_request_works
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "GET /test HTTP/1.1~0D~0A"
        message << "User-Agent: curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1~0D~0A"
        message << "Host: 0.0.0.0=5000~0D~0A"
        message << "Accept: */*~0D~0A"
        message << "~0D~0A"
      end
      assert_equal 'GET', result['method']
      assert_equal '/test', result['uri']
      assert_equal({"User-Agent" => "curl/7.18.0 (i486-pc-linux-gnu) libcurl/7.18.0 OpenSSL/0.9.8g zlib/1.2.3.3 libidn/1.1", "Host" => "0.0.0.0=5000", "Accept" => "*/*"}, result['headers'])
      assert_equal nil, result['body']
    end
  end

  def test_that_firefox_get_request_works
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "GET /favicon.ico HTTP/1.1~0D~0AHost: localhost:8888~0D~0AUser-Agent: Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20~0D~0AAccept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8~0D~0AAccept-Language: en-us,en;q=0.5~0D~0AAccept-Encoding: gzip,deflate~0D~0AAccept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.7~0D~0AKeep-Alive: 115~0D~0AConnection: keep-alive~0D~0ACookie: _session=OTQ5OThGRjgwMjlGN0VDMjc5MDkzNTU2NzlFOTk3MkVGM0Y2ODA2QkM1RDk1MTA0OTMwQzFBNjc5NjNGNzMwMg==-eyJkYXRhIjp7fSwic3RhbXAiOjEzMTUwODA5MzN9~0D~0A~0D~0A"
      end
      assert_equal 'GET', result['method']
      assert_equal '/favicon.ico', result['uri']
      assert_equal({"Host" => "localhost:8888", "User-Agent" => "Mozilla/5.0 (Macintosh; U; Intel Mac OS X 10.6; en-US; rv:1.9.2.20) Gecko/20110803 Firefox/3.6.20", "Accept" => "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8", "Accept-Language" => "en-us,en;q=0.5", "Accept-Encoding" => "gzip,deflate", "Accept-Charset" => "ISO-8859-1,utf-8;q=0.7,*;q=0.7", "Keep-Alive" => "115", "Connection" => "keep-alive", "Cookie" => "_session=OTQ5OThGRjgwMjlGN0VDMjc5MDkzNTU2NzlFOTk3MkVGM0Y2ODA2QkM1RDk1MTA0OTMwQzFBNjc5NjNGNzMwMg==-eyJkYXRhIjp7fSwic3RhbXAiOjEzMTUwODA5MzN9"}, result['headers'])
      assert_equal nil, result['body']
    end
  end

  def test_that_chrome_get_request_works
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "GET /favicon.ico HTTP/1.1~0D~0AHost: localhost:8888~0D~0AConnection: keep-alive~0D~0AAccept: */*~0D~0AUser-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.109 Safari/535.1~0D~0AAccept-Encoding: gzip,deflate,sdch~0D~0AAccept-Language: en-US,en;q=0.8~0D~0AAccept-Charset: ISO-8859-1,utf-8;q=0.7,*;q=0.3~0D~0ACookie: _session=MUExRjI3NDE3NTM3NTg1RkI2OTA0RjAyNkU0MkIxRjhCMzJGOTQ5MzNEN0E3NDc1RjkyNkYxNDFCQ0NFRTU0NQ==-eyJkYXRhIjp7fSwic3RhbXAiOjEzMTUwODA3MDN9~0D~0A~0D~0A"
      end
      assert_equal 'GET', result['method']
      assert_equal '/favicon.ico', result['uri']
      assert_equal({"Host" => "localhost:8888", "Connection" => "keep-alive", "Accept" => "*/*", "User-Agent" => "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_6_8) AppleWebKit/535.1 (KHTML, like Gecko) Chrome/13.0.782.109 Safari/535.1", "Accept-Encoding" => "gzip,deflate,sdch", "Accept-Language" => "en-US,en;q=0.8", "Accept-Charset" => "ISO-8859-1,utf-8;q=0.7,*;q=0.3", "Cookie" => "_session=MUExRjI3NDE3NTM3NTg1RkI2OTA0RjAyNkU0MkIxRjhCMzJGOTQ5MzNEN0E3NDc1RjkyNkYxNDFCQ0NFRTU0NQ==-eyJkYXRhIjp7fSwic3RhbXAiOjEzMTUwODA3MDN9"}, result['headers'])
      assert_equal nil, result['body']
    end
  end

  def test_that_no_headers_no_body_request_works
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "GET /get_no_headers_no_body/world HTTP/1.1~0D~0A"
        message << "~0D~0A"
      end
      assert_equal 'GET', result['method']
      assert_equal '/get_no_headers_no_body/world', result['uri']
      assert_equal({}, result['headers'])
      assert_equal nil, result['body']
    end
  end

  def test_that_one_header_no_body_request_works
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "GET /get_one_header_no_body HTTP/1.1~0D~0A"
        message << "Accept: */*~0D~0A"
        message << "~0D~0A"
      end
      assert_equal 'GET', result['method']
      assert_equal '/get_one_header_no_body', result['uri']
      assert_equal({"Accept" => "*/*"}, result['headers'])
      assert_equal nil, result['body']
    end
  end

  def test_that_funky_content_length_request_works
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "GET /get_funky_content_length_body_hello HTTP/1.0~0D~0A"
        message << "conTENT-Length: 5~0D~0A"
        message << "~0D~0A"
        message << "HELLO"
      end
      assert_equal 'GET', result['method']
      assert_equal '/get_funky_content_length_body_hello', result['uri']
      assert_equal({"conTENT-Length" => "5"}, result['headers'])
      assert_equal "HELLO", result['body']
    end
  end

  def test_that_chunked_all_your_base_request_works
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "POST /post_chunked_all_your_base HTTP/1.1~0D~0A"
        message << "Transfer-Encoding: chunked~0D~0A"
        message << "~0D~0A"
        message << "1e~0D~0Aall your base are belong to us~0D~0A"
        message << "0~0D~0A"
        message << "~0D~0A"
      end
      assert_equal 'POST', result['method']
      assert_equal '/post_chunked_all_your_base', result['uri']
      assert_equal({"Transfer-Encoding" => "chunked"}, result['headers'])
      assert_equal "all your base are belong to us", result['body']
    end
  end

  def test_that_line_folding_in_header_request_works
    run_test_as('wizard') do
      result = parse(:request) do |message|
        message << "GET / HTTP/1.1~0D~0A"
        message << "Line1:   abc~0D~0A"
        message << "~09def~0D~0A"
        message << " ghi~0D~0A"
        message << "~09~09jkl~0D~0A"
        message << "  mno ~0D~0A"
        message << "~09 ~09qrs~0D~0A"
        message << "Line2: ~09 line2~09~0D~0A"
        message << "~0D~0A"
      end
      assert_equal 'GET', result['method']
      assert_equal '/', result['uri']
      assert_equal({"Line1" => "abcdefghijklmno qrs", "Line2" => "line2~09"}, result['headers'])
      assert_equal nil, result['body']
    end
  end

  def test_that_no_headers_no_body_response_works
    run_test_as('wizard') do
      result = parse(:response) do |message|
        message << "HTTP/1.1 404 Not Found~0D~0A~0D~0A"
      end
      assert_equal 404, result['status']
      assert_equal({}, result['headers'])
      assert_equal nil, result['body']
    end
  end

  def test_that_no_reason_phrase_response_works
    run_test_as('wizard') do
      result = parse(:response) do |message|
        message << "HTTP/1.1 301~0D~0A~0D~0A"
      end
      assert_equal 301, result['status']
      assert_equal({}, result['headers'])
      assert_equal nil, result['body']
    end
  end

  def test_that_trailing_space_on_chunked_body_works
    run_test_as('wizard') do
      result = parse(:response) do |message|
        message << "HTTP/1.1 200 OK~0D~0A"
        message << "Content-Type: text/plain~0D~0A"
        message << "Transfer-Encoding: chunked~0D~0A"
        message << "~0D~0A"
        message << "25  ~0D~0A"
        message << "This is the data in the first chunk~0D~0A"
        message << "~0D~0A"
        message << "1C~0D~0A"
        message << "and this is the second one~0D~0A"
        message << "~0D~0A"
        message << "0  ~0D~0A"
        message << "~0D~0A"
      end
      assert_equal 200, result['status']
      assert_equal({"Content-Type" => "text/plain", "Transfer-Encoding" => "chunked"}, result['headers'])
      assert_equal 'This is the data in the first chunk~0D~0Aand this is the second one~0D~0A', result['body']
    end
  end

  def parse(type)
    message = []
    yield message
    simplify command %Q|; set_connection_option(player, "hold-input", 1); set_connection_option(player, "binary", 1); resume(#{@task}, {player, #{value_ref(message)}}); result = read_http("#{type.to_s}", player); set_connection_option(player, "hold-input", 0); set_connection_option(player, "binary", 0); return result; |
  end

  def setup
    run_test_as('wizard') do
      @task = simplify command %Q|; fork task (0); while (1); {player, message} = suspend(); while (message); {top, @message} = message; force_input(player, top); endwhile; endwhile; endfork; return task;|
    end
  end

  def teardown
    run_test_as('wizard') do
      kill_task(@task)
    end
  end

end
