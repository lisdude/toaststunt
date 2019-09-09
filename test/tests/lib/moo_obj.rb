class MooObj < Struct.new(:obj)

  alias inspect obj
  alias to_s obj

end
