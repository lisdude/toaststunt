module Fuzz

  G = ' !#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`abcdefghijklmnopqrstuvwxyz{|}~'

  def with_mutating_binary_string(s)
    g = Enumerator.new do |g|
      while true
        t = s.dup
        rand(t.length / 5).times do
          t[rand(t.length)] = G[rand(G.length)]
        end
        g.yield t
      end
    end
    yield g
  end

end
