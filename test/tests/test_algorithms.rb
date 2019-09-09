require 'test_helper'

class TestAlgorithms < Test::Unit::TestCase

  def test_that_encode_base64_works
    run_test_as('programmer') do
      assert_equal '', encode_base64('')
      assert_equal 'YQ==', encode_base64('a')
      assert_equal 'YWE=', encode_base64('aa')
      assert_equal 'YWFh', encode_base64('aaa')
      assert_equal '/w==', encode_base64('~FF')
      assert_equal '+w==', encode_base64('~FB')
      assert_equal 'MTIzNDU2Nzg5ADEyMzQ1Njc4OQAxMjM0NTY3ODkA', encode_base64('123456789~00123456789~00123456789~00')
      assert_equal "p85EjtJMFmH28gFVWjK8sCnsAoZ2zZsF5jbzLnZ4PPDRRUDH2vPHwKpDHtLQA10h9wzJGfCCZ3bkGToC8H74vsxqJ0bCw7gckR9+9/VQ2Q7Y1onahrWV4GYMSpIBK4quLn87iA==", encode_base64("~A7~CED~8E~D2L~16a~F6~F2~01UZ2~BC~B0)~EC~02~86v~CD~9B~05~E66~F3.vx<~F0~D1E@~C7~DA~F3~C7~C0~AAC~1E~D2~D0~03]!~F7~0C~C9~19~F0~82gv~E4~19:~02~F0~7E~F8~BE~CCj'F~C2~C3~B8~1C~91~1F~7E~F7~F5P~D9~0E~D8~D6~89~DA~86~B5~95~E0f~0CJ~92~01+~8A~AE.~7F;~88")
    end
  end

  def test_that_url_safe_encode_base64_works
    run_test_as('programmer') do
      assert_equal '', encode_base64('', 1)
      assert_equal 'YQ', encode_base64('a', 1)
      assert_equal 'YWE', encode_base64('aa', 1)
      assert_equal 'YWFh', encode_base64('aaa', 1)
      assert_equal '_w', encode_base64('~FF', 1)
      assert_equal '-w', encode_base64('~FB', 1)
      assert_equal 'MTIzNDU2Nzg5ADEyMzQ1Njc4OQAxMjM0NTY3ODkA', encode_base64('123456789~00123456789~00123456789~00', 1)
      assert_equal "p85EjtJMFmH28gFVWjK8sCnsAoZ2zZsF5jbzLnZ4PPDRRUDH2vPHwKpDHtLQA10h9wzJGfCCZ3bkGToC8H74vsxqJ0bCw7gckR9-9_VQ2Q7Y1onahrWV4GYMSpIBK4quLn87iA", encode_base64("~A7~CED~8E~D2L~16a~F6~F2~01UZ2~BC~B0)~EC~02~86v~CD~9B~05~E66~F3.vx<~F0~D1E@~C7~DA~F3~C7~C0~AAC~1E~D2~D0~03]!~F7~0C~C9~19~F0~82gv~E4~19:~02~F0~7E~F8~BE~CCj'F~C2~C3~B8~1C~91~1F~7E~F7~F5P~D9~0E~D8~D6~89~DA~86~B5~95~E0f~0CJ~92~01+~8A~AE.~7F;~88", 1)
    end
  end

  def test_that_encode_base64_expects_moo_binary_string_input
    run_test_as('programmer') do
      assert_equal 'Zm9vYmFy', encode_base64('~66~6f~6f~62~61~72')
      assert_equal 'Zm9vYmFy', encode_base64('~66~6f~6f~62~61~72', 1)
    end
  end

  def test_that_encode_base64_on_bad_moo_binary_string_input_fails
    run_test_as('programmer') do
      assert_equal E_INVARG, encode_base64('~00~#!~00')
      assert_equal E_INVARG, encode_base64('~00~#!~00', 1)
    end
  end

  def test_that_encode_base64_gets_the_length_correct
    run_test_as('programmer') do
      assert_equal 4, simplify(command(%Q|; return length(encode_base64("a"));|))
      assert_equal 4, simplify(command(%Q|; return length(encode_base64("aa"));|))
      assert_equal 4, simplify(command(%Q|; return length(encode_base64("aaa"));|))
    end
  end

  def test_that_url_safe_encode_base64_gets_the_length_correct
    run_test_as('programmer') do
      assert_equal 2, simplify(command(%Q|; return length(encode_base64("a", 1));|))
      assert_equal 3, simplify(command(%Q|; return length(encode_base64("aa", 1));|))
      assert_equal 4, simplify(command(%Q|; return length(encode_base64("aaa", 1));|))
    end
  end

  def test_that_decode_base64_works
    run_test_as('programmer') do
      assert_equal 'a', decode_base64('YQ==')
      assert_equal 'aa', decode_base64('YWE=')
      assert_equal 'aaa', decode_base64('YWFh')
      assert_equal '~FF', decode_base64('/w==')
      assert_equal '~FB', decode_base64('+w==')
      assert_equal '123456789~00123456789~00123456789~00', decode_base64('MTIzNDU2Nzg5ADEyMzQ1Njc4OQAxMjM0NTY3ODkA')
      assert_equal "~A7~CED~8E~D2L~16a~F6~F2~01UZ2~BC~B0)~EC~02~86v~CD~9B~05~E66~F3.vx<~F0~D1E@~C7~DA~F3~C7~C0~AAC~1E~D2~D0~03]!~F7~0C~C9~19~F0~82gv~E4~19:~02~F0~7E~F8~BE~CCj'F~C2~C3~B8~1C~91~1F~7E~F7~F5P~D9~0E~D8~D6~89~DA~86~B5~95~E0f~0CJ~92~01+~8A~AE.~7F;~88", decode_base64("p85EjtJMFmH28gFVWjK8sCnsAoZ2zZsF5jbzLnZ4PPDRRUDH2vPHwKpDHtLQA10h9wzJGfCCZ3bkGToC8H74vsxqJ0bCw7gckR9+9/VQ2Q7Y1onahrWV4GYMSpIBK4quLn87iA==")
    end
  end

  def test_that_url_safe_decode_base64_works
    run_test_as('programmer') do
      assert_equal 'a', decode_base64('YQ', 1)
      assert_equal 'aa', decode_base64('YWE', 1)
      assert_equal 'aaa', decode_base64('YWFh', 1)
      assert_equal '~FF', decode_base64('_w', 1)
      assert_equal '~FB', decode_base64('-w', 1)
      assert_equal '123456789~00123456789~00123456789~00', decode_base64('MTIzNDU2Nzg5ADEyMzQ1Njc4OQAxMjM0NTY3ODkA', 1)
      assert_equal "~A7~CED~8E~D2L~16a~F6~F2~01UZ2~BC~B0)~EC~02~86v~CD~9B~05~E66~F3.vx<~F0~D1E@~C7~DA~F3~C7~C0~AAC~1E~D2~D0~03]!~F7~0C~C9~19~F0~82gv~E4~19:~02~F0~7E~F8~BE~CCj'F~C2~C3~B8~1C~91~1F~7E~F7~F5P~D9~0E~D8~D6~89~DA~86~B5~95~E0f~0CJ~92~01+~8A~AE.~7F;~88", decode_base64("p85EjtJMFmH28gFVWjK8sCnsAoZ2zZsF5jbzLnZ4PPDRRUDH2vPHwKpDHtLQA10h9wzJGfCCZ3bkGToC8H74vsxqJ0bCw7gckR9-9_VQ2Q7Y1onahrWV4GYMSpIBK4quLn87iA", 1)
    end
  end

  def test_that_bad_base64_strings_fail
    run_test_as('programmer') do
      assert_equal E_INVARG, decode_base64('@@@@')
      assert_equal E_INVARG, decode_base64('@@@@', 1)
    end
  end

  def test_that_decode_base64_does_not_expect_moo_binary_string_input
    run_test_as('programmer') do
      assert_equal E_INVARG, decode_base64('~5A~6D~39~76~59~6D~46~79')
      assert_equal E_INVARG, decode_base64('~5A~6D~39~76~59~6D~46~79', 1)
    end
  end

  def test_that_decode_base64_padding_is_required_in_standard_mode
    run_test_as('programmer') do
      assert_equal 'test', decode_base64('dGVzdA==')
      assert_equal E_INVARG, decode_base64('dGVzdA=')
      assert_equal E_INVARG, decode_base64('dGVzdA')
    end
  end

  def test_that_decode_base64_padding_is_optional_in_url_safe_mode
    run_test_as('programmer') do
      assert_equal 'test', decode_base64('dGVzdA==', 1)
      assert_equal 'test', decode_base64('dGVzdA=', 1)
      assert_equal 'test', decode_base64('dGVzdA', 1)
    end
  end

  def test_that_decode_base64_does_not_allow_embedded_padding
    run_test_as('programmer') do
      assert_equal E_INVARG, decode_base64('dGV=zdA=')
      assert_equal E_INVARG, decode_base64('dGV=zdA=', 1)
    end
  end

  def test_that_decode_base64_does_not_allow_more_than_two_characters_of_padding
    run_test_as('programmer') do
      assert_equal E_INVARG, decode_base64('dGVzd===')
      assert_equal E_INVARG, decode_base64('dGVzd===', 1)
    end
  end

  def test_that_decode_base64_requires_at_least_two_characters_of_input
    run_test_as('programmer') do
      assert_equal E_INVARG, decode_base64('A===')
      assert_equal E_INVARG, decode_base64('A', 1)
    end
  end

  def test_that_decode_base64_gets_the_length_correct
    run_test_as('programmer') do
      assert_equal 1, simplify(command(%Q|; return length(decode_base64("YQ=="));|))
      assert_equal 2, simplify(command(%Q|; return length(decode_base64("YWE="));|))
      assert_equal 3, simplify(command(%Q|; return length(decode_base64("YWFh"));|))
    end
  end

  def test_that_url_safe_decode_base64_gets_the_length_correct
    run_test_as('programmer') do
      assert_equal 1, simplify(command(%Q|; return length(decode_base64("YQ", 1));|))
      assert_equal 2, simplify(command(%Q|; return length(decode_base64("YWE", 1));|))
      assert_equal 3, simplify(command(%Q|; return length(decode_base64("YWFh", 1));|))
    end
  end

  def test_that_a_variety_of_fuzzy_inputs_base64_encode_decode_correctly
    run_test_as('wizard') do
      100.times do |i|
        s = (1..rand(20)).inject('') { |a| a + "~#{'%02X' % rand(256)}" }
        s = simplify command %Q|; return encode_binary(decode_binary("#{s}"));|
        server_log s
        u = encode_base64(s, i % 2)
        t = decode_base64(u, i % 2)
        assert_equal s, t
      end
    end
  end

  def test_that_hashing_works
    run_test_as('programmer') do
      assert_equal "900150983CD24FB0D6963F7D28E17F72", string_hash("abc", "md5")
      assert_equal "8EB208F7E05D987A9B044A8E98C6B087F15A0BFC", string_hash("abc", "ripemd160")
      assert_equal "A9993E364706816ABA3E25717850C26C9CD0D89D", string_hash("abc", "sha1")
      assert_equal "23097D223405D8228642A477BDA255B32AADBCE4BDA0B3F7E36C9DA7", string_hash("abc", "sha224")
      assert_equal "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", string_hash("abc", "sha256")
      assert_equal "CB00753F45A35E8BB5A03D699AC65007272C32AB0EDED1631A8B605A43FF5BED8086072BA1E7CC2358BAECA134C825A7", string_hash("abc", "sha384")
      assert_equal "DDAF35A193617ABACC417349AE20413112E6FA4E89A97EA20A9EEEE64B55D39A2192992A274FC1A836BA3C23A3FEEBBD454D4423643CE80E2A9AC94FA54CA49F", string_hash("abc", "sha512")
      assert_equal "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", string_hash("abc")
      assert_equal "8215EF0796A20BCAAAE116D3876C664A", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "md5")
      assert_equal "12A053384A9C0C88E405A06C27DCF49ADA62EB2B", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "ripemd160")
      assert_equal "84983E441C3BD26EBAAE4AA1F95129E5E54670F1", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha1")
      assert_equal "75388B16512776CC5DBA5DA1FD890150B0C6455CB4F58B1952522525", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha224")
      assert_equal "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha256")
      assert_equal "3391FDDDFC8DC7393707A65B1B4709397CF8B1D162AF05ABFE8F450DE5F36BC6B0455A8520BC4E6F5FE95B1FE3C8452B", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha384")
      assert_equal "204A8FC6DDA82F0A0CED7BEB8E08A41657C16EF468B228A8279BE331A703C33596FD15C13B1B07F9AA1D3BEA57789CA031AD85C7A71DD70354EC631238CA3445", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha512")
      assert_equal "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")

      assert_equal "900150983CD24FB0D6963F7D28E17F72", binary_hash("abc", "md5")
      assert_equal "8EB208F7E05D987A9B044A8E98C6B087F15A0BFC", binary_hash("abc", "ripemd160")
      assert_equal "A9993E364706816ABA3E25717850C26C9CD0D89D", binary_hash("abc", "sha1")
      assert_equal "23097D223405D8228642A477BDA255B32AADBCE4BDA0B3F7E36C9DA7", binary_hash("abc", "sha224")
      assert_equal "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", binary_hash("abc", "sha256")
      assert_equal "CB00753F45A35E8BB5A03D699AC65007272C32AB0EDED1631A8B605A43FF5BED8086072BA1E7CC2358BAECA134C825A7", binary_hash("abc", "sha384")
      assert_equal "DDAF35A193617ABACC417349AE20413112E6FA4E89A97EA20A9EEEE64B55D39A2192992A274FC1A836BA3C23A3FEEBBD454D4423643CE80E2A9AC94FA54CA49F", binary_hash("abc", "sha512")
      assert_equal "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", binary_hash("abc")
      assert_equal "8215EF0796A20BCAAAE116D3876C664A", binary_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "md5")
      assert_equal "12A053384A9C0C88E405A06C27DCF49ADA62EB2B", binary_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "ripemd160")
      assert_equal "84983E441C3BD26EBAAE4AA1F95129E5E54670F1", binary_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha1")
      assert_equal "75388B16512776CC5DBA5DA1FD890150B0C6455CB4F58B1952522525", binary_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha224")
      assert_equal "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1", binary_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha256")
      assert_equal "3391FDDDFC8DC7393707A65B1B4709397CF8B1D162AF05ABFE8F450DE5F36BC6B0455A8520BC4E6F5FE95B1FE3C8452B", binary_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha384")
      assert_equal "204A8FC6DDA82F0A0CED7BEB8E08A41657C16EF468B228A8279BE331A703C33596FD15C13B1B07F9AA1D3BEA57789CA031AD85C7A71DD70354EC631238CA3445", binary_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha512")
      assert_equal "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1", binary_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")

      assert_equal "CA9C491AC66B2C62500882E93F3719A8", binary_hash("~00~00~00~00~00", "md5")
      assert_equal "585BD7ED566208944B1F2E13170CD4B66325B8EE", binary_hash("~00~00~00~00~00", "ripemd160")
      assert_equal "A10909C2CDCAF5ADB7E6B092A4FABA558B62BD96", binary_hash("~00~00~00~00~00", "sha1")
      assert_equal "0E61C65E55810439F418C959DD030C194B2FDAA2AE46B13B3732FF17", binary_hash("~00~00~00~00~00", "sha224")
      assert_equal "8855508AADE16EC573D21E6A485DFD0A7624085C1A14B5ECDD6485DE0C6839A4", binary_hash("~00~00~00~00~00", "sha256")
      assert_equal "8E8D9847B6BD198BB1980DB334659E96A1BF3DBB5C56368C6FABE6F6B561232790E3B40C1D4FB50A19C349B10BDC6950", binary_hash("~00~00~00~00~00", "sha384")
      assert_equal "65FAA9D920E0E9CFF43FC3F30AB02BA2E8CF6F4643B58F7C1E64583FBEC8A268E677B0EC4D54406E748BECB53FDA210F5D4F39CF2A5014B1CA496B0805182649", binary_hash("~00~00~00~00~00", "sha512")
      assert_equal "8855508AADE16EC573D21E6A485DFD0A7624085C1A14B5ECDD6485DE0C6839A4", binary_hash("~00~00~00~00~00")

      assert_equal "0236065F9B0E6AB6C6C860791121DB4E", binary_hash("~A7~CE~44~8E~D2~4C~16~61~F6~F2~01~55~5A~32~BC~B0~29~EC~02~86~76~CD~9B~05~E6~36~F3~2E~76~78~3C~F0~D1~45~40~C7~DA~F3~C7~C0~AA~43~1E~D2~D0~03~5D~21~F7~0C~C9~19~F0~82~67~76~E4~19~3A~02~F0~7E~F8~BE~CC~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88", "md5")
      assert_equal "95AE63897ABC55C7EA0FBF4CCCBB17C5A2606B1D", binary_hash("~A7~CE~44~8E~D2~4C~16~61~F6~F2~01~55~5A~32~BC~B0~29~EC~02~86~76~CD~9B~05~E6~36~F3~2E~76~78~3C~F0~D1~45~40~C7~DA~F3~C7~C0~AA~43~1E~D2~D0~03~5D~21~F7~0C~C9~19~F0~82~67~76~E4~19~3A~02~F0~7E~F8~BE~CC~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88", "ripemd160")
      assert_equal "9A2766D4D1740DCACD13D150B89626C63E2F83EE", binary_hash("~A7~CE~44~8E~D2~4C~16~61~F6~F2~01~55~5A~32~BC~B0~29~EC~02~86~76~CD~9B~05~E6~36~F3~2E~76~78~3C~F0~D1~45~40~C7~DA~F3~C7~C0~AA~43~1E~D2~D0~03~5D~21~F7~0C~C9~19~F0~82~67~76~E4~19~3A~02~F0~7E~F8~BE~CC~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88", "sha1")
      assert_equal "63FBAEFB5BC9D409768C416BBF94C07131B7C9D7047A02AFE8A96073", binary_hash("~A7~CE~44~8E~D2~4C~16~61~F6~F2~01~55~5A~32~BC~B0~29~EC~02~86~76~CD~9B~05~E6~36~F3~2E~76~78~3C~F0~D1~45~40~C7~DA~F3~C7~C0~AA~43~1E~D2~D0~03~5D~21~F7~0C~C9~19~F0~82~67~76~E4~19~3A~02~F0~7E~F8~BE~CC~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88", "sha224")
      assert_equal "A088FB0606E595E8A248393A296BE79C5A0FE7417FE267334E2113D6613B8D70", binary_hash("~A7~CE~44~8E~D2~4C~16~61~F6~F2~01~55~5A~32~BC~B0~29~EC~02~86~76~CD~9B~05~E6~36~F3~2E~76~78~3C~F0~D1~45~40~C7~DA~F3~C7~C0~AA~43~1E~D2~D0~03~5D~21~F7~0C~C9~19~F0~82~67~76~E4~19~3A~02~F0~7E~F8~BE~CC~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88", "sha256")
      assert_equal "6E8213F2BA1E3001CFF6F091690C88931A9FEC0FED9E60DEBE0FD9A4DAECF26BE34020009D740316289E01FA97117696", binary_hash("~A7~CE~44~8E~D2~4C~16~61~F6~F2~01~55~5A~32~BC~B0~29~EC~02~86~76~CD~9B~05~E6~36~F3~2E~76~78~3C~F0~D1~45~40~C7~DA~F3~C7~C0~AA~43~1E~D2~D0~03~5D~21~F7~0C~C9~19~F0~82~67~76~E4~19~3A~02~F0~7E~F8~BE~CC~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88", "sha384")
      assert_equal "0F633D2DA75405469B0A8A04E8F5A789CC4708DBA5F2785C4BB7F8A83462675A4593259BA627F84CC42AF61C0A3D49E3876759CB58D2EA24C84B8C2305CB56EA", binary_hash("~A7~CE~44~8E~D2~4C~16~61~F6~F2~01~55~5A~32~BC~B0~29~EC~02~86~76~CD~9B~05~E6~36~F3~2E~76~78~3C~F0~D1~45~40~C7~DA~F3~C7~C0~AA~43~1E~D2~D0~03~5D~21~F7~0C~C9~19~F0~82~67~76~E4~19~3A~02~F0~7E~F8~BE~CC~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88", "sha512")
      assert_equal "A088FB0606E595E8A248393A296BE79C5A0FE7417FE267334E2113D6613B8D70", binary_hash("~A7~CE~44~8E~D2~4C~16~61~F6~F2~01~55~5A~32~BC~B0~29~EC~02~86~76~CD~9B~05~E6~36~F3~2E~76~78~3C~F0~D1~45~40~C7~DA~F3~C7~C0~AA~43~1E~D2~D0~03~5D~21~F7~0C~C9~19~F0~82~67~76~E4~19~3A~02~F0~7E~F8~BE~CC~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88")

      assert_equal "99914B932BD37A50B983C5E7C90AE93B", value_hash([], "md5")
      assert_equal "85E318A2B32953EDE3405FF0B2914E96C6292464", value_hash([], "ripemd160")
      assert_equal "BF21A9E8FBC5A3846FB05B4FA0859E0917B2202F", value_hash([], "sha1")
      assert_equal "5CDD15A873608087BE07A41B7F1A04E96D3A66FE7A9B0FAAC71F8D05", value_hash([], "sha224")
      assert_equal "44136FA355B3678A1146AD16F7E8649E94FB4FC21FE77E8310C060F61CAAFF8A", value_hash([], "sha256")
      assert_equal "D2A23BC783E3AA38F401E13C7488505137C4954A7FD88331F1597C5FF71111DC807C7370A5B282C6DA541C56EDE69F30", value_hash([], "sha384")
      assert_equal "27C74670ADB75075FAD058D5CEAF7B20C4E7786C83BAE8A32F626F9782AF34C9A33C2046EF60FD2A7878D378E29FEC851806BBD9A67878F3A9F1CDA4830763FD", value_hash([], "sha512")
      assert_equal "44136FA355B3678A1146AD16F7E8649E94FB4FC21FE77E8310C060F61CAAFF8A", value_hash([])

      assert_equal "D751713988987E9331980363E24189CE", value_hash({}, "md5")
      assert_equal "2BDF48D04094F739353B0C92F25B8BDCF7BBB679", value_hash({}, "ripemd160")
      assert_equal "97D170E1550EEE4AFC0AF065B78CDA302A97674C", value_hash({}, "sha1")
      assert_equal "23F497F643E37257C3F7E54F049D8829C41103BB29BF8C0BA0D1DF0A", value_hash({}, "sha224")
      assert_equal "4F53CDA18C2BAA0C0354BB5F9A3ECBE5ED12AB4D8E11BA873C2F11161202B945", value_hash({}, "sha256")
      assert_equal "562109B054CB4428FC53607B107C0ACDF91DE434B990A0295F516EF2AAD79EFC902238944E76D42F21BCF710BFA4C554", value_hash({}, "sha384")
      assert_equal "B25B294CB4DEB69EA00A4C3CF3113904801B6015E5956BD019A8570B1FE1D6040E944EF3CDEE16D0A46503CA6E659A25F21CF9CEDDC13F352A3C98138C15D6AF", value_hash({}, "sha512")
      assert_equal "4F53CDA18C2BAA0C0354BB5F9A3ECBE5ED12AB4D8E11BA873C2F11161202B945", value_hash({})

      value = [1, 2, 3, {:nothing => ["fee", "fi", "fo", "fum"]}]
      string = toliteral(value)
      assert_equal "CF4BC5C55A11D6ECF1913148CCC98FFE", string_hash(string, "md5")
      assert_equal "CF4BC5C55A11D6ECF1913148CCC98FFE", value_hash(value, "md5")
      assert_equal "063DC736372D210EA6C432333AEF6E97C1A09211", string_hash(string, "ripemd160")
      assert_equal "063DC736372D210EA6C432333AEF6E97C1A09211", value_hash(value, "ripemd160")
      assert_equal "965D05B1BA7E8EF2F3D0F8A9335B86661344794C", string_hash(string, "sha1")
      assert_equal "965D05B1BA7E8EF2F3D0F8A9335B86661344794C", value_hash(value, "sha1")
      assert_equal "A88B8CB33AF93B2BCEB03CCB1F7B7538BEBBA84B1D46B1E425586CB5", string_hash(string, "sha224")
      assert_equal "A88B8CB33AF93B2BCEB03CCB1F7B7538BEBBA84B1D46B1E425586CB5", value_hash(value, "sha224")
      assert_equal "DC8CE6D88FA5A949979DF79FD745F859D9250FA0A20766FFA3DF88B81EFCEA68", string_hash(string, "sha256")
      assert_equal "DC8CE6D88FA5A949979DF79FD745F859D9250FA0A20766FFA3DF88B81EFCEA68", value_hash(value, "sha256")
      assert_equal "89028D478FE3991ECCCC7A35423E1103B30AFD6367D844D4A5D9634F9AE4A46BEBF05FD6B7257BB0C84565C7A3AD37FD", string_hash(string, "sha384")
      assert_equal "89028D478FE3991ECCCC7A35423E1103B30AFD6367D844D4A5D9634F9AE4A46BEBF05FD6B7257BB0C84565C7A3AD37FD", value_hash(value, "sha384")
      assert_equal "5D3BD15B97DE42BA336B9BFD6B38329CF0F62191026EE26029223E7AECC0BD7AA54195F6D8CD4E47988FB9B8AFC85027C098445C7F9160726F460AFC96D8F456", string_hash(string, "sha512")
      assert_equal "5D3BD15B97DE42BA336B9BFD6B38329CF0F62191026EE26029223E7AECC0BD7AA54195F6D8CD4E47988FB9B8AFC85027C098445C7F9160726F460AFC96D8F456", value_hash(value, "sha512")
      assert_equal "DC8CE6D88FA5A949979DF79FD745F859D9250FA0A20766FFA3DF88B81EFCEA68", string_hash(string)
      assert_equal "DC8CE6D88FA5A949979DF79FD745F859D9250FA0A20766FFA3DF88B81EFCEA68", value_hash(value)
    end
  end

  M0 = "~d1~31~dd~02~c5~e6~ee~c4~69~3d~9a~06~98~af~f9~5c~2f~ca~b5~87~12~46~7e~ab~40~04~58~3e~b8~fb~7f~89" +
    "~55~ad~34~06~09~f4~b3~02~83~e4~88~83~25~71~41~5a~08~51~25~e8~f7~cd~c9~9f~d9~1d~bd~f2~80~37~3c~5b"
  M1 = "~d1~31~dd~02~c5~e6~ee~c4~69~3d~9a~06~98~af~f9~5c~2f~ca~b5~07~12~46~7e~ab~40~04~58~3e~b8~fb~7f~89" +
    "~55~ad~34~06~09~f4~b3~02~83~e4~88~83~25~f1~41~5a~08~51~25~e8~f7~cd~c9~9f~d9~1d~bd~72~80~37~3c~5b"
  N0 = "~96~0b~1d~d1~dc~41~7b~9c~e4~d8~97~f4~5a~65~55~d5~35~73~9a~c7~f0~eb~fd~0c~30~29~f1~66~d1~09~b1~8f" +
    "~75~27~7f~79~30~d5~5c~eb~22~e8~ad~ba~79~cc~15~5c~ed~74~cb~dd~5f~c5~d3~6d~b1~9b~0a~d8~35~cc~a7~e3"
  N1 = "~96~0b~1d~d1~dc~41~7b~9c~e4~d8~97~f4~5a~65~55~d5~35~73~9a~47~f0~eb~fd~0c~30~29~f1~66~d1~09~b1~8f" +
    "~75~27~7f~79~30~d5~5c~eb~22~e8~ad~ba~79~4c~15~5c~ed~74~cb~dd~5f~c5~d3~6d~b1~9b~0a~58~35~cc~a7~e3"
  N2 = "~d8~82~3e~31~56~34~8f~5b~ae~6d~ac~d4~36~c9~19~c6~dd~53~e2~b4~87~da~03~fd~02~39~63~06~d2~48~cd~a0" +
    "~e9~9f~33~42~0f~57~7e~e8~ce~54~b6~70~80~a8~0d~1e~c6~98~21~bc~b6~a8~83~93~96~f9~65~2b~6f~f7~2a~70"
  N3 = "~d8~82~3e~31~56~34~8f~5b~ae~6d~ac~d4~36~c9~19~c6~dd~53~e2~34~87~da~03~fd~02~39~63~06~d2~48~cd~a0" +
    "~e9~9f~33~42~0f~57~7e~e8~ce~54~b6~70~80~28~0d~1e~c6~98~21~bc~b6~a8~83~93~96~f9~65~ab~6f~f7~2a~70"

  H0 = "A4C0D35C95A63A805915367DCFE6B751"
  H1 = "79054025255FB1A26E4BC422AEF54EB4"

  def test_md5_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "D41D8CD98F00B204E9800998ECF8427E", string_hash("", "md5")
      assert_equal "0CC175B9C0F1B6A831C399E269772661", string_hash("a", "md5")
      assert_equal "900150983CD24FB0D6963F7D28E17F72", string_hash("abc", "md5")
      assert_equal "F96B697D7CB7938D525A2F31AAF161D0", string_hash("message digest", "md5")
      assert_equal "C3FCD3D76192E4007DFB496CCA67E13B", string_hash("abcdefghijklmnopqrstuvwxyz", "md5")
      assert_equal "D174AB98D277D9F5A5611C2C9F419D9F", string_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "md5")
      assert_equal "57EDF4A22BE3C955AC49DA2E2107B67A", string_hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "md5")
      assert_equal "A5771BCE93E200C36F7CD9DFD0E5DEAA", string_hash("38", "md5")
      assert_equal H0, binary_hash(M0 + N0, "md5")
      assert_equal H0, binary_hash(M1 + N1, "md5")
      assert_equal H1, binary_hash(M0 + N2, "md5")
      assert_equal H1, binary_hash(M1 + N3, "md5")
    end
  end

  def test_sha1_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709", string_hash("", "sha1")
      assert_equal "86F7E437FAA5A7FCE15D1DDCB9EAEAEA377667B8", string_hash("a", "sha1")
      assert_equal "A9993E364706816ABA3E25717850C26C9CD0D89D", string_hash("abc", "sha1")
      assert_equal "C12252CEDA8BE8994D5FA0290A47231C1D16AAE3", string_hash("message digest", "sha1")
      assert_equal "32D10C7B8CF96570CA04CE37F2A19D84240D3A89", string_hash("abcdefghijklmnopqrstuvwxyz", "sha1")
      assert_equal "761C457BF73B14D27E9E9265C46F4B4DDA11F940", string_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "sha1")
      assert_equal "50ABF5706A150990A08B2C5EA40FA0E585554732", string_hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "sha1")
      assert_equal "5B384CE32D8CDEF02BC3A139D4CAC0A22BB029E8", string_hash("38", "sha1")
    end
  end

  def test_sha224_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "D14A028C2A3A2BC9476102BB288234C415A2B01F828EA62AC5B3E42F", string_hash("", "sha224")
      assert_equal "ABD37534C7D9A2EFB9465DE931CD7055FFDB8879563AE98078D6D6D5", string_hash("a", "sha224")
      assert_equal "23097D223405D8228642A477BDA255B32AADBCE4BDA0B3F7E36C9DA7", string_hash("abc", "sha224")
      assert_equal "2CB21C83AE2F004DE7E81C3C7019CBCB65B71AB656B22D6D0C39B8EB", string_hash("message digest", "sha224")
      assert_equal "45A5F72C39C5CFF2522EB3429799E49E5F44B356EF926BCF390DCCC2", string_hash("abcdefghijklmnopqrstuvwxyz", "sha224")
      assert_equal "BFF72B4FCB7D75E5632900AC5F90D219E05E97A7BDE72E740DB393D9", string_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "sha224")
      assert_equal "B50AECBE4E9BB0B57BC5F3AE760A8E01DB24F203FB3CDCD13148046E", string_hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "sha224")
      assert_equal "4CFCA6DA32DA647198225460722B7EA1284F98C4B179E8DBAE3F93D5", string_hash("38", "sha224")
      assert_equal "75388B16512776CC5DBA5DA1FD890150B0C6455CB4F58B1952522525", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha224")
    end
  end

  def test_sha256_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", string_hash("", "sha256")
      assert_equal "CA978112CA1BBDCAFAC231B39A23DC4DA786EFF8147C4E72B9807785AFEE48BB", string_hash("a", "sha256")
      assert_equal "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", string_hash("abc", "sha256")
      assert_equal "F7846F55CF23E14EEBEAB5B4E1550CAD5B509E3348FBC4EFA3A1413D393CB650", string_hash("message digest", "sha256")
      assert_equal "71C480DF93D6AE2F1EFAD1447C66C9525E316218CF51FC8D9ED832F2DAF18B73", string_hash("abcdefghijklmnopqrstuvwxyz", "sha256")
      assert_equal "DB4BFCBD4DA0CD85A60C3C37D3FBD8805C77F15FC6B1FDFE614EE0A7C8FDB4C0", string_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "sha256")
      assert_equal "F371BC4A311F2B009EEF952DD83CA80E2B60026C8E935592D0F9C308453C813E", string_hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "sha256")
      assert_equal "AEA92132C4CBEB263E6AC2BF6C183B5D81737F179F21EFDC5863739672F0F470", string_hash("38", "sha256")
      assert_equal "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1", string_hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "sha256")
      assert_equal "CF5B16A778AF8380036CE59E7B0492370B249B11E8F07A51AFAC45037AFEE9D1", string_hash("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", "sha256")
    end
  end

  def test_sha384_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "38B060A751AC96384CD9327EB1B1E36A21FDB71114BE07434C0CC7BF63F6E1DA274EDEBFE76F65FBD51AD2F14898B95B", string_hash("", "sha384")
      assert_equal "54A59B9F22B0B80880D8427E548B7C23ABD873486E1F035DCE9CD697E85175033CAA88E6D57BC35EFAE0B5AFD3145F31", string_hash("a", "sha384")
      assert_equal "CB00753F45A35E8BB5A03D699AC65007272C32AB0EDED1631A8B605A43FF5BED8086072BA1E7CC2358BAECA134C825A7", string_hash("abc", "sha384")
      assert_equal "473ED35167EC1F5D8E550368A3DB39BE54639F828868E9454C239FC8B52E3C61DBD0D8B4DE1390C256DCBB5D5FD99CD5", string_hash("message digest", "sha384")
      assert_equal "FEB67349DF3DB6F5924815D6C3DC133F091809213731FE5C7B5F4999E463479FF2877F5F2936FA63BB43784B12F3EBB4", string_hash("abcdefghijklmnopqrstuvwxyz", "sha384")
      assert_equal "1761336E3F7CBFE51DEB137F026F89E01A448E3B1FAFA64039C1464EE8732F11A5341A6F41E0C202294736ED64DB1A84", string_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "sha384")
      assert_equal "B12932B0627D1C060942F5447764155655BD4DA0C9AFA6DD9B9EF53129AF1B8FB0195996D2DE9CA0DF9D821FFEE67026", string_hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "sha384")
      assert_equal "C071D202AD950B6A04A5F15C24596A993AF8B212467958D570A3FFD4780060638E3A3D06637691D3012BD31122071B2C", string_hash("38", "sha384")
      assert_equal "09330C33F71147E83D192FC782CD1B4753111B173B3B05D22FA08086E3B0F712FCC7C71A557E2DB966C3E9FA91746039", string_hash("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", "sha384")
    end
  end

  def test_sha512_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "CF83E1357EEFB8BDF1542850D66D8007D620E4050B5715DC83F4A921D36CE9CE47D0D13C5D85F2B0FF8318D2877EEC2F63B931BD47417A81A538327AF927DA3E", string_hash("", "sha512")
      assert_equal "1F40FC92DA241694750979EE6CF582F2D5D7D28E18335DE05ABC54D0560E0F5302860C652BF08D560252AA5E74210546F369FBBBCE8C12CFC7957B2652FE9A75", string_hash("a", "sha512")
      assert_equal "DDAF35A193617ABACC417349AE20413112E6FA4E89A97EA20A9EEEE64B55D39A2192992A274FC1A836BA3C23A3FEEBBD454D4423643CE80E2A9AC94FA54CA49F", string_hash("abc", "sha512")
      assert_equal "107DBF389D9E9F71A3A95F6C055B9251BC5268C2BE16D6C13492EA45B0199F3309E16455AB1E96118E8A905D5597B72038DDB372A89826046DE66687BB420E7C", string_hash("message digest", "sha512")
      assert_equal "4DBFF86CC2CA1BAE1E16468A05CB9881C97F1753BCE3619034898FAA1AABE429955A1BF8EC483D7421FE3C1646613A59ED5441FB0F321389F77F48A879C7B1F1", string_hash("abcdefghijklmnopqrstuvwxyz", "sha512")
      assert_equal "1E07BE23C26A86EA37EA810C8EC7809352515A970E9253C26F536CFC7A9996C45C8370583E0A78FA4A90041D71A4CEAB7423F19C71B9D5A3E01249F0BEBD5894", string_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "sha512")
      assert_equal "72EC1EF1124A45B047E8B7C75A932195135BB61DE24EC0D1914042246E0AEC3A2354E093D76F3048B456764346900CB130D2A4FD5DD16ABB5E30BCB850DEE843", string_hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "sha512")
      assert_equal "CAAE34A5E81031268BCDAF6F1D8C04D37B7F2C349AFB705B575966F63E2EBF0FD910C3B05160BA087AB7AF35D40B7C719C53CD8B947C96111F64105FD45CC1B2", string_hash("38", "sha512")
      assert_equal "8E959B75DAE313DA8CF4F72814FC143F8F7779C6EB9F7FA17299AEADB6889018501D289E4900F7E4331B99DEC4B5433AC7D329EEB6DD26545E96E55B874BE909", string_hash("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", "sha512")
      assert_equal "5338370F5655F4DA14572D4FB471539B201485ECFB3D3204048DC6B83E61FAB505BCBBD73E644A1A5D159A32A0889CF3C9591B69B26D31BE56C68838CE3CD63D", string_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "sha512")
      assert_equal "33F8901B053E4CC677D3CB4122D96AD9B96B13BF76194CF962488BB4DE4998A71455CB31582DB527ADF77A485B81CF5B722A5E8638EB6BE487400F3AEC006E7C", string_hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890", "sha512")
    end
  end

  def test_ripemd160_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "9C1185A5C5E9FC54612808977EE8F548B2258D31", string_hash("", "ripemd160")
      assert_equal "0BDC9D2D256B3EE9DAAE347BE6F4DC835A467FFE", string_hash("a", "ripemd160")
      assert_equal "8EB208F7E05D987A9B044A8E98C6B087F15A0BFC", string_hash("abc", "ripemd160")
      assert_equal "F71C27109C692C1B56BBDCEB5B9D2865B3708DBC", string_hash("abcdefghijklmnopqrstuvwxyz", "ripemd160")
      assert_equal "5D0689EF49D2FAE572B881B123A85FFA21595F36", string_hash("message digest", "ripemd160")
      assert_equal "B0E20B6E3116640286ED3A87A5713079B21F5189", string_hash("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789", "ripemd160")
      assert_equal "9B752E45573D4B39F4DBD3323CAB82BF63326BFB", string_hash("12345678901234567890123456789012345678901234567890123456789012345678901234567890", "ripemd160")
      assert_equal "6B2D075B1CD34CD1C3E43A995F110C55649DAD0E", string_hash("38", "ripemd160")
    end
  end

  def test_that_hmac_builtins_take_a_third_parameter
    run_test_as('programmer') do
      assert_equal "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD", string_hmac("", "", "sha256")
      assert_equal "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD", binary_hmac("", "", "sha256")
      assert_equal "140B47411F10521CC48851F43C9ABE73016E1D45627C8523D25CBF7FE64F8B03", value_hmac("", "", "sha256")
    end
  end

  def test_that_hmacing_works
    run_test_as('programmer') do
      assert_equal "EFFCDF6AE5EB2FA2D27416D5F184DF9C259A7C79", string_hmac("what do ya want for nothing?", "Jefe", "sha1")
      assert_equal "B617318655057264E28BC0B6FB378C8EF146BE00", string_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b", "sha1")
      assert_equal "217E44BB08B6E06A2D6C30F3CB9F537F97C63356", string_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "90D0DACE1C1BDC957339307803160335BDE6DF2B", string_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "999D63DE9594FB320FA1B1D7CC6D3662FEC60AB1", string_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00", "sha1")
      assert_equal "FBDB1D1B18AA6C08324B7D64B71FB76370690E1D", string_hmac("", "", "sha1")
      assert_equal "5BDCC146BF60754E6A042426089575C75A003F089D2739839DEC58B964EC3843", string_hmac("what do ya want for nothing?", "Jefe", "sha256")
      assert_equal "B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7", string_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b", "sha256")
      assert_equal "9B09FFA71B942FCB27635FBCD5B0E944BFDC63644F0713938A7F51535C3A35E2", string_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "60E431591EE0B67F0D8A26AACBF5B77F8E0BC6213728C5140546040F0EE37F54", string_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "44D1527E6295BF1055924DEBECC4499776903B9C37FF7D6519452230A7C30AF8", string_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00", "sha256")
      assert_equal "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD", string_hmac("", "", "sha256")
      assert_equal "5BDCC146BF60754E6A042426089575C75A003F089D2739839DEC58B964EC3843", string_hmac("what do ya want for nothing?", "Jefe")
      assert_equal "B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7", string_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b")
      assert_equal "9B09FFA71B942FCB27635FBCD5B0E944BFDC63644F0713938A7F51535C3A35E2", string_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa")
      assert_equal "60E431591EE0B67F0D8A26AACBF5B77F8E0BC6213728C5140546040F0EE37F54", string_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa")
      assert_equal "44D1527E6295BF1055924DEBECC4499776903B9C37FF7D6519452230A7C30AF8", string_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00")
      assert_equal "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD", string_hmac("", "")

      assert_equal "EFFCDF6AE5EB2FA2D27416D5F184DF9C259A7C79", binary_hmac("what do ya want for nothing?", "Jefe", "sha1")
      assert_equal "B617318655057264E28BC0B6FB378C8EF146BE00", binary_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b", "sha1")
      assert_equal "217E44BB08B6E06A2D6C30F3CB9F537F97C63356", binary_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "90D0DACE1C1BDC957339307803160335BDE6DF2B", binary_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "999D63DE9594FB320FA1B1D7CC6D3662FEC60AB1", binary_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00", "sha1")
      assert_equal "FBDB1D1B18AA6C08324B7D64B71FB76370690E1D", binary_hmac("", "", "sha1")
      assert_equal "5BDCC146BF60754E6A042426089575C75A003F089D2739839DEC58B964EC3843", binary_hmac("what do ya want for nothing?", "Jefe", "sha256")
      assert_equal "B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7", binary_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b", "sha256")
      assert_equal "9B09FFA71B942FCB27635FBCD5B0E944BFDC63644F0713938A7F51535C3A35E2", binary_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "60E431591EE0B67F0D8A26AACBF5B77F8E0BC6213728C5140546040F0EE37F54", binary_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "44D1527E6295BF1055924DEBECC4499776903B9C37FF7D6519452230A7C30AF8", binary_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00", "sha256")
      assert_equal "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD", binary_hmac("", "", "sha256")
      assert_equal "5BDCC146BF60754E6A042426089575C75A003F089D2739839DEC58B964EC3843", binary_hmac("what do ya want for nothing?", "Jefe")
      assert_equal "B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7", binary_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b")
      assert_equal "9B09FFA71B942FCB27635FBCD5B0E944BFDC63644F0713938A7F51535C3A35E2", binary_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa")
      assert_equal "60E431591EE0B67F0D8A26AACBF5B77F8E0BC6213728C5140546040F0EE37F54", binary_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa")
      assert_equal "44D1527E6295BF1055924DEBECC4499776903B9C37FF7D6519452230A7C30AF8", binary_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00")
      assert_equal "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD", binary_hmac("", "")

      assert_equal "28809E3D8CEA48FABEBA399A4AF2942AEBD89DE4", value_hmac("what do ya want for nothing?", "Jefe", "sha1")
      assert_equal "7A9169174AC124FF114F53A03744AB22BF840C7D", value_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b", "sha1")
      assert_equal "2311301999BCC3D7A95344E44527E8EB2FDAE39B", value_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "171DBF033CFCA50A2DC1A0182D5E37D2453CED5D", value_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "EA1A09993CA7F0848D1085A67D84AA6348093004", value_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00", "sha1")
      assert_equal "1F871CCD9A7164051BF779C8483677424E9C77F5", value_hmac("", "", "sha1")
      assert_equal "6E5C2A18FEBCCACA669AC5F4415342714850E76BA86824A3371270E6DD69956F", value_hmac("what do ya want for nothing?", "Jefe", "sha256")
      assert_equal "69AB6C68D61A3B85453AB106B356640D2D3F8D4945C67781423B7A8D48565A02", value_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b", "sha256")
      assert_equal "951A56011AB193C6C74E68CFB855D25ADFF63C488FDE458039AE3F4108EE37F0", value_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "EC69C60BE95151FEF7E303AD7231C7165E4120683F7F218CE0661F7B8317534C", value_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "415BD3379F2E94D6F38ED2D54BEFB367912B7043FE033D3B6C66A78EA51A153C", value_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00", "sha256")
      assert_equal "140B47411F10521CC48851F43C9ABE73016E1D45627C8523D25CBF7FE64F8B03", value_hmac("", "", "sha256")
      assert_equal "6E5C2A18FEBCCACA669AC5F4415342714850E76BA86824A3371270E6DD69956F", value_hmac("what do ya want for nothing?", "Jefe")
      assert_equal "69AB6C68D61A3B85453AB106B356640D2D3F8D4945C67781423B7A8D48565A02", value_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b")
      assert_equal "951A56011AB193C6C74E68CFB855D25ADFF63C488FDE458039AE3F4108EE37F0", value_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa")
      assert_equal "EC69C60BE95151FEF7E303AD7231C7165E4120683F7F218CE0661F7B8317534C", value_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa")
      assert_equal "415BD3379F2E94D6F38ED2D54BEFB367912B7043FE033D3B6C66A78EA51A153C", value_hmac("Test Using Key with nulls", "123456789~00123456789~00123456789~00")
      assert_equal "140B47411F10521CC48851F43C9ABE73016E1D45627C8523D25CBF7FE64F8B03", value_hmac("", "")
    end
  end

  def test_hmac_sha1_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "EFFCDF6AE5EB2FA2D27416D5F184DF9C259A7C79", string_hmac("what do ya want for nothing?", "Jefe", "sha1")
      assert_equal "B617318655057264E28BC0B6FB378C8EF146BE00", string_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b", "sha1")
      assert_equal "4C1A03424B55E07FE7F27BE1D58BB9324A9A5A04", string_hmac("Test With Truncation", "~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c", "sha1")
      assert_equal "AA4AE5E15272D00E95705637CE8A3B55ED402112", string_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "E8E99D0F45237D786D6BBAA7965C7808BBFF1A91", string_hmac("Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "125D7342B9AC11CD91A39AF48AA17B4F63F175D3", binary_hmac("~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha1")
      assert_equal "4C9007F4026250C6BC8414F9BF50C86C2D7235DA", binary_hmac("~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd", "~01~02~03~04~05~06~07~08~09~0a~0b~0c~0d~0e~0f~10~11~12~13~14~15~16~17~18~19", "sha1")
    end
  end

  def test_hmac_sha256_test_vectors_from_nettle
    run_test_as('programmer') do
      assert_equal "5BDCC146BF60754E6A042426089575C75A003F089D2739839DEC58B964EC3843", string_hmac("what do ya want for nothing?", "Jefe", "sha256")
      assert_equal "B0344C61D8DB38535CA8AFCEAF0BF12B881DC200C9833DA726E9376C2E32CFF7", string_hmac("Hi There", "~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b~0b", "sha256")
      assert_equal "A3B6167473100EE06E0C796C2955552BFA6F7C0A6A8AEF8B93F860AAB0CD20C5", string_hmac("Test With Truncation", "~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c~0c", "sha256")
      assert_equal "60E431591EE0B67F0D8A26AACBF5B77F8E0BC6213728C5140546040F0EE37F54", string_hmac("Test Using Larger Than Block-Size Key - Hash Key First", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "9B09FFA71B942FCB27635FBCD5B0E944BFDC63644F0713938A7F51535C3A35E2", string_hmac("This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "773EA91E36800E46854DB8EBD09181A72959098B3EF8C122D9635514CED565FE", binary_hmac("~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd~dd", "~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa~aa", "sha256")
      assert_equal "82558A389A443C0EA4CC819899F2083A85F0FAA3E578F8077A2E3FF46729665B", binary_hmac("~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd~cd", "~01~02~03~04~05~06~07~08~09~0a~0b~0c~0d~0e~0f~10~11~12~13~14~15~16~17~18~19", "sha256")
    end
  end

  def test_that_specifying_fake_hash_algorithms_fails
    run_test_as('programmer') do
      assert_equal E_INVARG, string_hash("abc", "bozo")
      assert_equal E_INVARG, binary_hash("abc", "con")
      assert_equal E_INVARG, value_hash("abc", "carne")
    end
  end

  def test_that_specifying_fake_hmac_algorithms_fails
    run_test_as('programmer') do
      assert_equal E_INVARG, string_hmac("abc", "abc", "bozo")
      assert_equal E_INVARG, binary_hmac("abc", "abc", "con")
      assert_equal E_INVARG, value_hmac("abc", "abc", "carne")
    end
  end

  def test_that_hashing_bad_binary_strings_does_not_crash_the_server
    run_test_as('programmer') do
      assert_equal E_INVARG, binary_hash('~00~#!~00')
    end
  end

  def test_that_hmacing_bad_binary_strings_does_not_crash_the_server
    run_test_as('programmer') do
      assert_equal E_INVARG, binary_hmac('~00~#!~00', 'key')
    end
  end

  def test_that_hmacing_bad_binary_string_keys_does_not_crash_the_server
    run_test_as('programmer') do
      assert_equal E_INVARG, string_hmac('', '~00~#!~00')
      assert_equal E_INVARG, binary_hmac('', '~00~#!~00')
      assert_equal E_INVARG, value_hmac('','~00~#!~00')
    end
  end

  def test_that_a_variety_of_fuzzy_inputs_do_not_break_binary_hash
    run_test_as('wizard') do
      with_mutating_binary_string("~A7~CED~8E~D2L~16a~F6~F2~01UZ2~BC~B0)~EC~02~86v~CD~9B~05~E66~F3.vx<~F0~D1E@~C7~DA~F3~C7~C0~AAC~1E~D2~D0~03]!~F7~0C~C9~19~F0~82gv~E4~19:~02~F0~7E~F8~BE") do |g|
        100.times do
          s = g.next
          server_log s
          v = binary_hash(s)
          assert v == E_INVARG || v.class == String
          v = binary_hash(s, 'md5')
          assert v == E_INVARG || v.class == String
          v = binary_hash(s, 'sha1')
          assert v == E_INVARG || v.class == String
          v = binary_hash(s, 'sha256')
          assert v == E_INVARG || v.class == String
        end
      end
    end
  end

  def test_that_a_variety_of_fuzzy_inputs_do_not_break_binary_hmac
    run_test_as('wizard') do
      x = "~6A~27~46~C2~C3~B8~1C~91~1F~7E~F7~F5~50~D9~0E~D8~D6~89~DA~86~B5~95~E0~66~0C~4A~92~01~2B~8A~AE~2E~7F~3B~88"
      with_mutating_binary_string("~A7~CED~8E~D2L~16a~F6~F2~01UZ2~BC~B0)~EC~02~86v~CD~9B~05~E66~F3.vx<~F0~D1E@~C7~DA~F3~C7~C0~AAC~1E~D2~D0~03]!~F7~0C~C9~19~F0~82gv~E4~19:~02~F0~7E~F8~BE") do |g|
        100.times do
          s = g.next
          server_log s
          v = binary_hmac(x, s)
          assert v == E_INVARG || v.class == String
          v = binary_hmac(s, x)
          assert v == E_INVARG || v.class == String
        end
      end
    end
  end

  def test_that_value_hash_is_not_case_sensitive
    run_test_as('programmer') do
      assert_equal "99914B932BD37A50B983C5E7C90AE93B", value_hash([], "MD5")
      assert_equal "BF21A9E8FBC5A3846FB05B4FA0859E0917B2202F", value_hash([], "SHA1")
      assert_equal "44136FA355B3678A1146AD16F7E8649E94FB4FC21FE77E8310C060F61CAAFF8A", value_hash([], "SHA256")
    end
  end

  def test_that_string_hash_is_not_case_sensitive
    run_test_as('programmer') do
      assert_equal "900150983CD24FB0D6963F7D28E17F72", string_hash("abc", "MD5")
      assert_equal "A9993E364706816ABA3E25717850C26C9CD0D89D", string_hash("abc", "SHA1")
      assert_equal "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", string_hash("abc", "SHA256")
    end
  end

  def test_that_binary_hash_is_not_case_sensitive
    run_test_as('programmer') do
      assert_equal "900150983CD24FB0D6963F7D28E17F72", binary_hash("abc", "MD5")
      assert_equal "A9993E364706816ABA3E25717850C26C9CD0D89D", binary_hash("abc", "SHA1")
      assert_equal "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", binary_hash("abc", "SHA256")
    end
  end

  def test_that_value_hash_can_return_a_binary_string
    run_test_as('programmer') do
      assert_equal "99914B932BD37A50B983C5E7C90AE93B", value_hash([], "MD5", 0)
      assert_equal "~99~91~4B~93~2B~D3~7A~50~B9~83~C5~E7~C9~0A~E9~3B", value_hash([], "MD5", 1)
    end
  end

  def test_that_string_hash_can_return_a_binary_string
    run_test_as('programmer') do
      assert_equal "D41D8CD98F00B204E9800998ECF8427E", string_hash("", "MD5", 0)
      assert_equal "~D4~1D~8C~D9~8F~00~B2~04~E9~80~09~98~EC~F8~42~7E", string_hash("", "MD5", 1)
    end
  end

  def test_that_binary_hash_can_return_a_binary_string
    run_test_as('programmer') do
      assert_equal "CA9C491AC66B2C62500882E93F3719A8", binary_hash("~00~00~00~00~00", "MD5", 0)
      assert_equal "~CA~9C~49~1A~C6~6B~2C~62~50~08~82~E9~3F~37~19~A8", binary_hash("~00~00~00~00~00", "MD5", 1)
    end
  end

  def test_that_value_hmac_can_return_a_binary_string
    run_test_as('programmer') do
      assert_equal "140B47411F10521CC48851F43C9ABE73016E1D45627C8523D25CBF7FE64F8B03", value_hmac("", "", "sha256", 0)
      assert_equal "~14~0B~47~41~1F~10~52~1C~C4~88~51~F4~3C~9A~BE~73~01~6E~1D~45~62~7C~85~23~D2~5C~BF~7F~E6~4F~8B~03", value_hmac("", "", "sha256", 1)
    end
  end

  def test_that_string_hmac_can_return_a_binary_string
    run_test_as('programmer') do
      assert_equal "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD", string_hmac("", "", "sha256", 0)
      assert_equal "~B6~13~67~9A~08~14~D9~EC~77~2F~95~D7~78~C3~5F~C5~FF~16~97~C4~93~71~56~53~C6~C7~12~14~42~92~C5~AD", string_hmac("", "", "sha256", 1)
    end
  end

  def test_that_binary_hmac_can_return_a_binary_string
    run_test_as('programmer') do
      assert_equal "B613679A0814D9EC772F95D778C35FC5FF1697C493715653C6C712144292C5AD", binary_hmac("", "", "sha256", 0)
      assert_equal "~B6~13~67~9A~08~14~D9~EC~77~2F~95~D7~78~C3~5F~C5~FF~16~97~C4~93~71~56~53~C6~C7~12~14~42~92~C5~AD", binary_hmac("", "", "sha256", 1)
    end
  end

  def test_that_hash_lengths_are_correct
    run_test_as('programmer') do
      assert_equal 96, simplify(command(%Q|; return length(string_hash("", "sha256", 1));|))
      assert_equal 64, simplify(command(%Q|; return length(string_hash("", "sha256", 0));|))
    end
  end

  def test_that_hmac_lengths_are_correct
    run_test_as('programmer') do
      assert_equal 96, simplify(command(%Q|; return length(string_hmac("", "", "sha256", 1));|))
      assert_equal 64, simplify(command(%Q|; return length(string_hmac("", "", "sha256", 0));|))
    end
  end

  class << self
    def supports_md5
      ''.crypt('$1$')[0..2] == '$1$'
    end
    def supports_sha256
      ''.crypt('$5$')[0..2] == '$5$'
    end
    def supports_sha512
      ''.crypt('$6$')[0..2] == '$6$'
    end
  end

  if !supports_md5
    puts 'WARNING: MD5 crypt() is not supported on this system'
  end
  if !supports_sha256
    puts 'WARNING: SHA256 crypt() is not supported on this system'
  end
  if !supports_sha512
    puts 'WARNING: SHA512 crypt() is not supported on this system'
  end

  def test_that_salt_generates_standard_two_character_salt
    run_test_as('programmer') do
      assert_equal "..", salt("", "~00~00")
    end
  end

  def test_that_standard_two_character_salt_requires_two_characters_of_random_input
    run_test_as('programmer') do
      assert_equal E_INVARG, salt("", "")
    end
  end

  def test_that_salt_generates_md5_salts
    run_test_as('programmer') do
      if self.class.supports_md5
        assert_equal "$1$........", salt("$1$", "~00~00~00~00~00~00")
      end
    end
  end

  def test_that_md5_salt_requires_at_least_three_characters_of_random_input
    run_test_as('programmer') do
      if self.class.supports_md5
        assert_equal E_INVARG, salt("$1$", "12")
      end
    end
  end

  def test_that_salt_generates_sha256_salts
    run_test_as('programmer') do
      if self.class.supports_sha256
        assert_equal "$5$................", salt("$5$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_sha256_salt_requires_at_least_three_characters_of_random_input
    run_test_as('programmer') do
      if self.class.supports_sha256
        assert_equal E_INVARG, salt("$5$", "~00~00")
      end
    end
  end

  def test_that_sha256_salt_prefix_may_specify_rounds
    run_test_as('programmer') do
      if self.class.supports_sha256
        assert_equal "$5$rounds=1000$................", salt("$5$rounds=1000$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal "$5$rounds=10000000$................", salt("$5$rounds=10000000$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_sha256_salt_prefix_rounds_may_not_be_below_1000
    run_test_as('programmer') do
      if self.class.supports_sha256
        assert_equal E_INVARG, salt("$5$rounds=999$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$rounds=1$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_sha256_salt_prefix_rounds_may_not_be_above_999999999
    run_test_as('programmer') do
      if self.class.supports_sha256
        assert_equal E_INVARG, salt("$5$rounds=1000000000$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$rounds=1231231230$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_sha256_salt_prefix_rounds_may_not_be_garbage
    run_test_as('programmer') do
      if self.class.supports_sha256
        assert_equal E_INVARG, salt("$5$rounds=$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$rounds=foobar$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$rounds=123foobar$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$foobar=$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_salt_generates_sha512_salts
    run_test_as('programmer') do
      if self.class.supports_sha512
        assert_equal "$5$................", salt("$5$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_sha512_salt_requires_at_least_three_characters_of_random_input
    run_test_as('programmer') do
      if self.class.supports_sha512
        assert_equal E_INVARG, salt("$5$", "~00~00")
      end
    end
  end

  def test_that_sha512_salt_prefix_may_specify_rounds
    run_test_as('programmer') do
      if self.class.supports_sha512
        assert_equal "$5$rounds=1000$................", salt("$5$rounds=1000$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal "$5$rounds=10000000$................", salt("$5$rounds=10000000$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_sha512_salt_prefix_rounds_may_not_be_below_1000
    run_test_as('programmer') do
      if self.class.supports_sha512
        assert_equal E_INVARG, salt("$5$rounds=999$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$rounds=1$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_sha512_salt_prefix_rounds_may_not_be_above_999999999
    run_test_as('programmer') do
      if self.class.supports_sha512
        assert_equal E_INVARG, salt("$5$rounds=1000000000$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$rounds=1231231230$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_sha512_salt_prefix_rounds_may_not_be_garbage
    run_test_as('programmer') do
      if self.class.supports_sha512
        assert_equal E_INVARG, salt("$5$rounds=$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$rounds=foobar$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$rounds=123foobar$", "~00~00~00~00~00~00~00~00~00~00~00~00")
        assert_equal E_INVARG, salt("$5$foobar=$", "~00~00~00~00~00~00~00~00~00~00~00~00")
      end
    end
  end

  def test_that_salt_generates_bcrypt_compatible_salt
    run_test_as('programmer') do
      assert_equal "$2a$05$......................", salt("$2a$", "~00~00~00~00~00~00~00~00~00~00~00~00~00~00~00~00")
    end
  end

  def test_that_bcrypt_compatible_salt_requires_sixteen_characters_of_random_input
    run_test_as('programmer') do
      assert_equal E_INVARG, salt("$2a$", "1234567890")
    end
  end

  def test_that_bcrypt_compatible_salt_prefix_may_specify_cost_factor
    run_test_as('programmer') do
      assert_equal "$2a$04$KRGxLBS0Lxe3KBCwKxOzLe", salt("$2a$04$", "1234567890123456")
      assert_equal "$2a$16$KRGxLBS0Lxe3KBCwKxOzLe", salt("$2a$16$", "1234567890123456")
    end
  end

  def test_that_bcrypt_prefix_cost_factor_may_not_be_below_four
    run_test_as('programmer') do
      assert_equal E_INVARG, salt("$2a$03$", "1234567890123456")
      assert_equal E_INVARG, salt("$2a$01$", "1234567890123456")
    end
  end

  def test_that_bcrypt_prefix_cost_factor_may_not_be_above_thirty_one
    run_test_as('programmer') do
      assert_equal E_INVARG, salt("$2a$32$", "1234567890123456")
      assert_equal E_INVARG, salt("$2a$41$", "1234567890123456")
    end
  end

  def test_that_bcrypt_prefix_cost_factor_may_not_be_garbage
    run_test_as('programmer') do
      assert_equal E_INVARG, salt("$2a$$", "1234567890123456")
      assert_equal E_INVARG, salt("$2a$mm$", "1234567890123456")
    end
  end

  def test_that_salt_may_be_a_binary_string
    run_test_as('programmer') do
      assert_equal E_INVARG, salt("", "~00")
      assert_equal E_INVARG, salt("$2a$", "~00~01~02~03~04~05~06~07~08~09")
      assert_equal "..", salt("", "~00~00")
      assert_equal "$2a$05$......................", salt("$2a$", "~00~00~00~00~00~00~00~00~00~00~00~00~00~00~00~00")
    end
  end

  def test_that_salting_with_bad_binary_strings_does_not_crash_the_server
    run_test_as('programmer') do
      assert_equal E_INVARG, salt("", "~00~#!")
      assert_equal E_INVARG, salt("$2a$", "~00~00~00~00~00~00~00~00~00~00~00~00~00~00~00~zz")
    end
  end

  def test_that_invalid_prefixes_are_rejected
    run_test_as('programmer') do
      assert_equal E_INVARG, salt("foo", "")
    end
  end

  def test_that_crypt_with_one_argument_defaults_to_unix_crypt
    run_test_as('programmer') do
      hash = simplify(command %Q|; return crypt("foobar");|)
      assert_equal hash, 'foobar'.crypt(hash[0..1])
    end
  end

  def test_that_crypt_falls_through_to_unix_crypt
    run_test_as('programmer') do
      assert_equal "12Ce3aDvyIkJ2", crypt("foobar", "12")
      assert_equal "foobar".crypt("$1$12"), crypt("foobar", "$1$12") if self.class.supports_md5
      assert_equal "foobar".crypt("$5$12"), crypt("foobar", "$5$12") if self.class.supports_sha256
      assert_equal "foobar".crypt("$6$12"), crypt("foobar", "$6$12") if self.class.supports_sha512
    end
  end

  def test_that_crypt_with_sha256_salt_accepts_rounds
    run_test_as('wizard') do
      if self.class.supports_sha256
        assert_equal "foobar".crypt("$5$rounds=100000$12"), crypt("foobar", "$5$rounds=100000$12")
      end
    end
  end

  def test_that_only_wizards_can_specify_rounds_for_crypt_with_sha256_salt
    run_test_as('programmer') do
      if self.class.supports_sha256
        assert_equal E_PERM, crypt("foobar", "$5$rounds=100000$12")
      end
    end
  end

  def test_that_crypt_with_sha512_salt_accepts_rounds
    run_test_as('wizard') do
      if self.class.supports_sha512
        assert_equal "foobar".crypt("$6$rounds=100000$12"), crypt("foobar", "$6$rounds=100000$12")
      end
    end
  end

  def test_that_only_wizards_can_specify_rounds_for_crypt_with_sha512_salt
    run_test_as('programmer') do
      if self.class.supports_sha512
        assert_equal E_PERM, crypt("foobar", "$6$rounds=100000$12")
      end
    end
  end

  def test_that_crypt_supports_bcrypt_compatible_salt_and_uses_bcrypt
    run_test_as('programmer') do
      assert_equal "$2a$05$KRGxLBS0Lxe3KBCwKxOzLeMe5OhXhsCBVMLq7IYo9z2kiiCZMSmz6", crypt("foobar", "$2a$05$KRGxLBS0Lxe3KBCwKxOzLe")
    end
  end

  def test_that_bcrypt_compatible_salt_requires_sixteen_characters_of_seed_input
    run_test_as('programmer') do
      assert_equal E_INVARG, crypt("foobar", "$2a$05$KRGxLBS0Lxe3KBCwKxOzL")
    end
  end

  def test_that_bcrypt_compatible_salt_may_specify_cost_factor
    run_test_as('programmer') do
      assert_equal "$2a$05$KRGxLBS0Lxe3KBCwKxOzLeMe5OhXhsCBVMLq7IYo9z2kiiCZMSmz6", crypt("foobar", "$2a$05$KRGxLBS0Lxe3KBCwKxOzLe")
    end
    run_test_as('wizard') do
      assert_equal "$2a$14$KRGxLBS0Lxe3KBCwKxOzLe7p.ddns6iDfgZCpWHCQ4hN.RUaeF9Rq", crypt("foobar", "$2a$14$KRGxLBS0Lxe3KBCwKxOzLe")
    end
  end

  def test_that_bcrypt_cost_factor_may_not_be_below_four
    run_test_as('wizard') do
      assert_equal E_INVARG, crypt("foobar", "$2a$03$KRGxLBS0Lxe3KBCwKxOzLe")
      assert_equal E_INVARG, crypt("foobar", "$2a$01$KRGxLBS0Lxe3KBCwKxOzLe")
    end
  end

  def test_that_bcrypt_cost_factor_may_not_be_above_thirty_one
    run_test_as('wizard') do
      assert_equal E_INVARG, crypt("foobar", "$2a$32$KRGxLBS0Lxe3KBCwKxOzLe")
      assert_equal E_INVARG, crypt("foobar", "$2a$41$KRGxLBS0Lxe3KBCwKxOzLe")
    end
  end

  def test_that_bcrypt_cost_factor_other_than_five_requires_wiz_perms
    run_test_as('programmer') do
      assert_equal E_PERM, crypt("foobar", "$2a$10$KRGxLBS0Lxe3KBCwKxOzLe")
    end
    run_test_as('wizard') do
      assert_equal "$2a$10$KRGxLBS0Lxe3KBCwKxOzLeAgxB7LpTJ36c2o2iVIDdBSv3rB.GHhW", crypt("foobar", "$2a$10$KRGxLBS0Lxe3KBCwKxOzLe")
    end
  end

  def test_that_invalid_salts_are_rejected
    run_test_as('wizard') do
      assert_equal E_INVARG, crypt("foobar", "$2a$ab$KRGxLBS0Lxe3KBCwKxOzLe")
      assert_equal E_INVARG, crypt("foobar", "$2a$!!$KRGxLBS0Lxe3KBCwKxOzLe")
      assert_equal E_INVARG, crypt("foobar", "$5$roundz=10001$..")
      assert_equal E_INVARG, crypt("foobar", "$5$rounds=abacab$..")
      assert_equal E_INVARG, crypt("foobar", "$6$roundz=10001$..")
      assert_equal E_INVARG, crypt("foobar", "$6$rounds=abacab$..")
    end
  end

end
