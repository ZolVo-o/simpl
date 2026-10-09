class Simpl < Formula
  desc "Small programming language with Russian keywords"
  homepage "https://github.com/ZolVo-o/simpl"
  url "https://github.com/ZolVo-o/simpl/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "f925c4ab1b6db8a0d378ae820a5c3dd88d5e3ffd0d6071feb76542b9c420ad09"
  license "MIT"

  depends_on "python@3.12"

  def install
    ENV.prepend_path "PATH", Formula["python@3.12"].opt_bin
    system "make"
    bin.install "simpl"
    (share/"simpl").install "docs", "examples"
  end

  test do
    (testpath/"hello.simpl").write <<~EOS
      сказать "Привет, мир"
    EOS

    assert_match "Привет, мир", shell_output("#{bin}/simpl run #{testpath}/hello.simpl")
  end
end
