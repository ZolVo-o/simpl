#!/bin/sh
set -eu

tmpdir=$(mktemp -d)
file_path=".simpl-test-file-$$.txt"
trap 'rm -rf "$tmpdir" "$file_path"' EXIT HUP INT TERM

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
if ./simpl run "$tmpdir/parse-error.sim" >/dev/null 2>&1; then
    echo "Ошибка разбора должна завершать run кодом 2" >&2
    exit 1
else
    test "$?" -eq 2
fi
if ./simpl compile "$tmpdir/parse-error.sim" >/dev/null 2>&1; then
    echo "Ошибка разбора должна завершать compile кодом 2" >&2
    exit 1
else
    test "$?" -eq 2
fi

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

cat > "$tmpdir/lexical-error.sim" <<'SIMPL'
сказать @
SIMPL
if output=$(./simpl run "$tmpdir/lexical-error.sim" 2>&1); then
    echo "Лексическая ошибка должна завершиться ошибкой" >&2
    exit 1
fi
case "$output" in
    *"lexical-error.sim:1:"*"error:"*) ;;
    *) echo "Лексическая ошибка не содержит имя файла и строку: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/semantic-error.sim" <<'SIMPL'
сказать 1

сказать неизвестная_функция()
SIMPL
if output=$(./simpl run "$tmpdir/semantic-error.sim" 2>&1); then
    echo "Семантическая ошибка должна завершиться ошибкой" >&2
    exit 1
fi
case "$output" in
    *"semantic-error.sim:3:1: error: не удалось скомпилировать программу"*) ;;
    *) echo "Семантическая ошибка не содержит исходную строку: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/type-error.sim" <<'SIMPL'
сказать "ok"
сказать верх(3)
SIMPL
if output=$(./simpl run "$tmpdir/type-error.sim" 2>&1); then
    echo "Ошибка типа должна завершиться ошибкой" >&2
    exit 1
fi
case "$output" in
    *"type-error.sim:2:1: error:"*) ;;
    *) echo "Ошибка типа привязана не к строке 2: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/suggestion-error.sim" <<'SIMPL'
пусть значение = 1
сказать значние
SIMPL
if output=$(./simpl run "$tmpdir/suggestion-error.sim" 2>&1); then
    echo "Неизвестная переменная должна завершиться ошибкой" >&2
    exit 1
fi
case "$output" in
    *"suggestion-error.sim:2:1: error: неизвестная переменная 'значние'. Возможно, ты имел в виду 'значение'?"*) ;;
    *) echo "Подсказка похожего имени не выведена или неверная строка: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/stack-error.sim" <<'SIMPL'
функция внутренняя()
    вернуть 1 / 0
функция внешняя()
    вернуть внутренняя()
сказать внешняя()
SIMPL
if output=$(./simpl run "$tmpdir/stack-error.sim" 2>&1); then
    echo "Ошибка во вложенном вызове должна завершиться ошибкой" >&2
    exit 1
fi
case "$output" in
    *"stack-error.sim:2:1: error: division by zero"*"в функции 'внутренняя' (строка 2)"*"в функции 'внешняя' (строка 4)"*) ;;
    *) echo "Нет корректного стека вызовов или номера строки: $output" >&2; exit 1 ;;
esac
./simpl compile "$tmpdir/stack-error.sim" >/dev/null
if output=$(./simpl run "$tmpdir/stack-error.simc" 2>&1); then
    echo "Скомпилированная ошибка должна завершиться ошибкой" >&2
    exit 1
fi
case "$output" in
    *"stack-error.simc:2:1: error: division by zero"*"в функции 'внутренняя' (строка 2)"*"в функции 'внешняя' (строка 4)"*) ;;
    *) echo "Строки/стек вызовов не сохранены в байткоде: $output" >&2; exit 1 ;;
esac

test "$(./simpl --version)" = "Simpl $(cat VERSION)"
test "$(./simpl -v)" = "Simpl $(cat VERSION)"
./simpl --help | grep -q 'repl'
./simpl -h | grep -q 'compile'
if output=$(./simpl 2>&1); then
    echo "Запуск без аргументов должен завершаться ошибкой" >&2
    exit 1
else
    test "$?" -eq 1
fi
test "$output" = "Нет команды. Используй simpl --help."

cat > "$tmpdir/args.sim" <<'SIMPL'
## Показать аргументы командной строки
сказать arguments()
SIMPL
output=$(./simpl run "$tmpdir/args.sim" first --second)
test "$output" = "['first', '--second']"
./simpl compile "$tmpdir/args.sim" >/dev/null
output=$(./simpl run "$tmpdir/args.simc" first -- second)
test "$output" = "['first', 'second']"

cat > "$tmpdir/args.sim" <<'SIMPL'
сказать аргументы()
SIMPL
output=$(./simpl run "$tmpdir/args.sim" first second)
test "$output" = "['first', 'second']"
./simpl compile "$tmpdir/args.sim" >/dev/null
output=$(./simpl run "$tmpdir/args.simc" first second)
test "$output" = "['first', 'second']"

cat > "$tmpdir/sort.sim" <<'SIMPL'
пусть числа = [3, 1, 2]
сказать сортировать(числа)
сказать числа
сказать сортировать(["груша", "арбуз", "яблоко"])
SIMPL
output=$(./simpl run "$tmpdir/sort.sim")
test "$output" = "$(printf '[1, 2, 3]\n[3, 1, 2]\n[\047арбуз\047, \047груша\047, \047яблоко\047]')"
./simpl compile "$tmpdir/sort.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/sort.simc")
test "$compiled_output" = "$output"

cat > "$tmpdir/sum.sim" <<'SIMPL'
сказать сумма([1, 2.5, 3])
сказать сумма([])
SIMPL
output=$(./simpl run "$tmpdir/sum.sim")
test "$output" = "$(printf '6.5\n0')"
./simpl compile "$tmpdir/sum.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/sum.simc")
test "$compiled_output" = "$output"

printf 'сказать сумма([1, "два"])\n' > "$tmpdir/sum-error.sim"
if output=$(./simpl run "$tmpdir/sum-error.sim" 2>&1); then
    echo "сумма должна отклонять нечисловые элементы" >&2
    exit 1
else
    test "$?" -eq 3
fi
case "$output" in
    *"сумма ожидает список чисел"*) ;;
    *) echo "Неверная ошибка сумма: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/extremes.sim" <<'SIMPL'
сказать минимум([4, -2.5, 7])
сказать максимум([4, -2.5, 7])
сказать минимум(["груша", "арбуз", "яблоко"])
сказать максимум(["груша", "арбуз", "яблоко"])
SIMPL
output=$(./simpl run "$tmpdir/extremes.sim")
test "$output" = "$(printf '%s\n' '-2.5' '7' 'арбуз' 'яблоко')"
./simpl compile "$tmpdir/extremes.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/extremes.simc")
test "$compiled_output" = "$output"

printf 'сказать минимум([])\n' > "$tmpdir/empty-min.sim"
if output=$(./simpl run "$tmpdir/empty-min.sim" 2>&1); then
    echo "минимум пустого списка должен завершиться ошибкой" >&2
    exit 1
else
    test "$?" -eq 3
fi
case "$output" in
    *"минимум и максимум требуют непустой список"*) ;;
    *) echo "Неверная ошибка для пустого списка: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/files.sim" <<'SIMPL'
сказать записать_файл("$FILE_PATH", "Привет, мир!")
сказать читать_файл("$FILE_PATH")
SIMPL
sed "s|\$FILE_PATH|$file_path|g" "$tmpdir/files.sim" > "$tmpdir/files-ready.sim"
printf 'старое содержимое' > "$file_path"
output=$(./simpl run "$tmpdir/files-ready.sim")
test "$output" = "$(printf 'None\nПривет, мир!')"
test "$(cat "$file_path")" = "Привет, мир!"
./simpl compile "$tmpdir/files-ready.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/files-ready.simc")
test "$compiled_output" = "$output"

printf 'сказать читать_файл("%s.missing")\n' "$file_path" > "$tmpdir/file-error.sim"
if output=$(./simpl run "$tmpdir/file-error.sim" 2>&1); then
    echo "Чтение отсутствующего файла должно завершиться ошибкой" >&2
    exit 1
else
    test "$?" -eq 3
fi
case "$output" in
    *"file-error.sim:1:1: error:"*) ;;
    *) echo "Неверная ошибка чтения файла: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/time.sim" <<'SIMPL'
сказать тип(время())
сказать пауза(0)
SIMPL
output=$(./simpl run "$tmpdir/time.sim")
test "$output" = "$(printf 'число\nNone')"
./simpl compile "$tmpdir/time.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/time.simc")
test "$compiled_output" = "$output"

printf 'сказать пауза(-1)\n' > "$tmpdir/sleep-error.sim"
if output=$(./simpl run "$tmpdir/sleep-error.sim" 2>&1); then
    echo "пауза должна отклонять отрицательный интервал" >&2
    exit 1
else
    test "$?" -eq 3
fi
case "$output" in
    *"пауза ожидает неотрицательное число миллисекунд"*) ;;
    *) echo "Неверная ошибка пауза: $output" >&2; exit 1 ;;
esac

cat > "$tmpdir/conversions.sim" <<'SIMPL'
сказать число("42")
сказать число("3.5")
сказать строка(123)
сказать строка(да)
сказать случайное(5, 5)
SIMPL
output=$(./simpl run "$tmpdir/conversions.sim")
test "$output" = "$(printf '42\n3.5\n123\nTrue\n5')"
./simpl compile "$tmpdir/conversions.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/conversions.simc")
test "$compiled_output" = "$output"

printf 'сказать случайное(2, 1)\n' > "$tmpdir/random-error.sim"
if output=$(./simpl run "$tmpdir/random-error.sim" 2>&1); then
    echo "случайное должно отклонять обратный диапазон" >&2
    exit 1
else
    test "$?" -eq 3
fi

cat > "$tmpdir/collections.sim" <<'SIMPL'
пусть значения = ["а", "б"]
сказать добавить(значения, "в")
сказать значения
сказать удалить(значения, -1)
сказать значения
сказать содержит(значения, "а")
сказать содержит(значения, "в")
сказать найти("абракадабра", "ка")
сказать найти([4, 8, 12], 8)
сказать найти([4, 8, 12], 9)
сказать заменить("красный красный", "красный", "синий")
SIMPL
output=$(./simpl run "$tmpdir/collections.sim")
test "$output" = "$(printf 'None\n[\047а\047, \047б\047, \047в\047]\nв\n[\047а\047, \047б\047]\nTrue\nFalse\n4\n1\n-1\nсиний синий')"
./simpl compile "$tmpdir/collections.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/collections.simc")
test "$compiled_output" = "$output"

cat > "$tmpdir/math.sim" <<'SIMPL'
сказать модуль(-7)
сказать модуль(-2.5)
сказать круг(2.5)
сказать круг(3.5)
сказать круг(-2.5)
SIMPL
output=$(./simpl run "$tmpdir/math.sim")
test "$output" = "$(printf '7\n2.5\n2\n4\n-2')"
./simpl compile "$tmpdir/math.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/math.simc")
test "$compiled_output" = "$output"

cat > "$tmpdir/ascii-shuffle.sim" <<'SIMPL'
сказать символ(65)
сказать код("Z")
пусть значения = [1, 2, 3, 4]
сказать перемешать(значения)
сказать сортировать(значения)
SIMPL
output=$(./simpl run "$tmpdir/ascii-shuffle.sim")
test "$output" = "$(printf 'A\n90\nNone\n[1, 2, 3, 4]')"
./simpl compile "$tmpdir/ascii-shuffle.sim" >/dev/null
compiled_output=$(./simpl run "$tmpdir/ascii-shuffle.simc")
test "$compiled_output" = "$output"

printf 'сказать код("я")\n' > "$tmpdir/code-error.sim"
if output=$(./simpl run "$tmpdir/code-error.sim" 2>&1); then
    echo "код должен отклонять символы вне ASCII" >&2
    exit 1
else
    test "$?" -eq 3
fi
case "$output" in
    *"код ожидает один символ ASCII"*) ;;
    *) echo "Неверная ошибка код: $output" >&2; exit 1 ;;
esac

printf 'сказать выход(7)\n' > "$tmpdir/exit.sim"
if ./simpl run "$tmpdir/exit.sim" >/dev/null 2>&1; then
    echo "выход(7) должен завершить программу с кодом 7" >&2
    exit 1
else
    test "$?" -eq 7
fi

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

printf 'сказать "Привет, 🌍"\nпусть значение = 7\nсказать значение\n' > "$tmpdir/unicode.sim"
output=$(./simpl run "$tmpdir/unicode.sim")
test "$output" = "$(printf 'Привет, 🌍\n7')"

: > "$tmpdir/empty.sim"
test -z "$(./simpl run "$tmpdir/empty.sim")"
printf '# Только комментарий\n  # и ещё один\n' > "$tmpdir/comments.sim"
test -z "$(./simpl run "$tmpdir/comments.sim")"
printf '# Комментарий без перевода строки' > "$tmpdir/comments-no-newline.sim"
test -z "$(./simpl run "$tmpdir/comments-no-newline.sim")"

i=0
while test "$i" -lt 1001; do
    printf 'сказать 1\n'
    i=$((i + 1))
done > "$tmpdir/long.sim"
./simpl run "$tmpdir/long.sim" > "$tmpdir/long.out"
test "$(wc -l < "$tmpdir/long.out")" -eq 1001

cat > "$tmpdir/deep-recursion.sim" <<'SIMPL'
функция считать(n)
    если n <= 0
        вернуть 0
    вернуть считать(n - 1)
сказать считать(10000)
SIMPL
test "$(./simpl run "$tmpdir/deep-recursion.sim")" = "0"

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

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        echo "Проверка SIGINT пропущена в Windows/MSYS2."
        ;;
    *)
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
        ;;
esac

echo "Тесты CLI пройдены."
