#!/bin/sh
set -eu

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

cat > "$tmpdir/hello.sim" <<'SIMPL'
пусть значение = 6
сказать значение * 7
SIMPL

output=$(./simpl run "$tmpdir/hello.sim")
test "$output" = "42"

./simpl compile "$tmpdir/hello.sim" >/dev/null
test -f "$tmpdir/hello.simc"
output=$(./simpl disasm "$tmpdir/hello.simc")
case "$output" in
    *MUL*) ;;
    *) echo "Дизассемблер не вывел инструкцию MUL" >&2; exit 1 ;;
esac

cat > "$tmpdir/parse-error.sim" <<'SIMPL'
если да
сказать 1
SIMPL
if output=$(./simpl run "$tmpdir/parse-error.sim" 2>&1); then
    echo "Ожидалась ошибка разбора" >&2
    exit 1
fi
case "$output" in
    *parse-error.sim:2:*) ;;
    *) echo "Ошибка разбора не содержит file:line:column: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/runtime-error.sim" <<'SIMPL'
сказать 1 / 0
SIMPL
if output=$(./simpl run "$tmpdir/runtime-error.sim" 2>&1); then
    echo "Ожидалась ошибка времени выполнения" >&2
    exit 1
fi
case "$output" in
    *"runtime-error.sim:1:1: error: division by zero"*) ;;
    *) echo "Ошибка VM не соответствует формату диагностики: $output" >&2; exit 1 ;;
esac

test "$(./simpl --version)" = "Simpl 0.1.0"
test "$(./simpl -v)" = "Simpl 0.1.0"
./simpl --help | grep -q 'repl'
./simpl -h | grep -q 'compile'
if ./simpl >/dev/null 2>&1; then
    echo "Запуск без аргументов должен завершаться ошибкой" >&2
    exit 1
else
    test "$?" -eq 1
fi

cat > "$tmpdir/args.sim" <<'SIMPL'
## Показать аргументы командной строки
сказать arguments()
SIMPL
output=$(./simpl run "$tmpdir/args.sim" first --second)
test "$output" = "['first', '--second']"
./simpl compile "$tmpdir/args.sim" >/dev/null
output=$(./simpl run "$tmpdir/args.simc" first -- second)
test "$output" = "['first', 'second']"

./simpl compile "$tmpdir/hello.sim" -o - > "$tmpdir/stdout.simc"
cmp "$tmpdir/hello.simc" "$tmpdir/stdout.simc"
output=$(./simpl disasm --no-names --stats "$tmpdir/hello.simc")
case "$output" in
    *"Статистика:"*"байткод"*"инструкций"*) ;;
    *) echo "Статистика дизассемблера не выведена: $output" >&2; exit 1 ;;
esac
case "$output" in
    *"(значение)"*) echo "--no-names не скрыл имена" >&2; exit 1 ;;
esac

./simpl check "$tmpdir/hello.sim"
if ./simpl check "$tmpdir/parse-error.sim" >/dev/null 2>&1; then
    echo "check должен обнаружить ошибку разбора" >&2
    exit 1
else
    test "$?" -eq 2
fi
if ./simpl run "$tmpdir/runtime-error.sim" >/dev/null 2>&1; then
    echo "Ошибка времени выполнения должна завершаться кодом 3" >&2
    exit 1
else
    test "$?" -eq 3
fi

printf 'Ada\n' | ./simpl run examples/input.simpl > "$tmpdir/input.out"
grep -q 'Привет, Ada' "$tmpdir/input.out"

printf '## Документация\n# комментарий\nсказать 1\n' > "$tmpdir/doc.sim"
test "$(./simpl doc "$tmpdir/doc.sim")" = "Документация"

printf 'если да\n\tпусть x=1+2   # вычисление\n\tсказать x   \n' > "$tmpdir/fmt.sim"
./simpl fmt "$tmpdir/fmt.sim"
test "$(cat "$tmpdir/fmt.sim")" = "$(printf 'если да\n    пусть x = 1 + 2 # вычисление\n    сказать x')"
cp "$tmpdir/fmt.sim" "$tmpdir/fmt.expected"
./simpl fmt "$tmpdir/fmt.sim"
cmp "$tmpdir/fmt.expected" "$tmpdir/fmt.sim"

./simpl new "$tmpdir/project" >/dev/null
test -f "$tmpdir/project/main.simpl"
test -f "$tmpdir/project/README.md"
test "$(./simpl run "$tmpdir/project/main.simpl")" = "Привет, мир!"

output=$(printf 'сказать 1\n\n' | ./simpl repl)
case "$output" in
    *"1"*) ;;
    *) echo "REPL не выполнил введённую программу: $output" >&2; exit 1 ;;
esac

if ./simpl frobnicate >"$tmpdir/unknown.out" 2>"$tmpdir/unknown.err"; then
    echo "Неизвестная команда должна завершаться ошибкой" >&2
    exit 1
else
    test "$?" -eq 1
fi
grep -q "Неизвестная команда 'frobnicate'" "$tmpdir/unknown.err"
test ! -s "$tmpdir/unknown.out"

if ./simpl compile "$tmpdir/hello.sim" --bogus >/dev/null 2>"$tmpdir/flag.err"; then
    echo "Неизвестный флаг должен завершаться ошибкой" >&2
    exit 1
else
    test "$?" -eq 1
fi
grep -q "Неизвестный флаг '--bogus'" "$tmpdir/flag.err"
if ./simpl disasm "$tmpdir/hello.simc" extra >/dev/null 2>"$tmpdir/extra.err"; then
    echo "Лишний аргумент disasm должен завершаться ошибкой" >&2
    exit 1
else
    test "$?" -eq 1
fi
grep -q 'Слишком много аргументов' "$tmpdir/extra.err"

cat > "$tmpdir/infinite.sim" <<'SIMPL'
пока да
    пусть значение = 1
SIMPL
./simpl run "$tmpdir/infinite.sim" >/dev/null 2>"$tmpdir/interrupt.err" &
pid=$!
sleep 1
kill -INT "$pid"
if wait "$pid"; then
    echo "Ctrl+C должен прервать бесконечный цикл" >&2
    exit 1
else
    test "$?" -eq 130
fi
grep -q 'Прервано пользователем' "$tmpdir/interrupt.err"

echo "Тесты CLI пройдены."
