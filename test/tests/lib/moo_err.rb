class MooErr < Struct.new(:err)

  alias inspect err
  alias to_s err

end
