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

echo "Тесты CLI пройдены."
