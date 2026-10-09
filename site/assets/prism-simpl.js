(() => {
  if (!window.Prism) return;

  Prism.languages.simpl = {
    comment: /#.*/,
    string: {
      pattern: /("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')/,
      greedy: true
    },
    keyword: {
      pattern: /(?<![\p{L}\p{N}_])(?:пусть|сказать|спросить|если|иначе|пока|повторить|раз|как|функция|вернуть|и|или|не|для|каждого|в|попробовать|поймать)(?![\p{L}\p{N}_])/u
    },
    boolean: {
      pattern: /(?<![\p{L}\p{N}_])(?:да|нет)(?![\p{L}\p{N}_])/u
    },
    number: /\b\d+(?:\.\d+)?\b/,
    function: /[\p{L}_][\p{L}\p{N}_]*(?=\s*\()/u,
    operator: /\*\*|==|!=|<=|>=|[+*/%<>=-]/,
    punctuation: /[()[\]{},.:]/
  };

  Prism.highlightAll();
})();
