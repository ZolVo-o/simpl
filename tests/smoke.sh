#!/usr/bin/env bash
set -euo pipefail

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

[[ "$(./simpl --version)" == "Simpl $(cat VERSION)" ]]

cat > "$tmpdir/arithmetic.sim" <<'SIMPL'
сказать 6 * 7
SIMPL
[[ "$(./simpl run "$tmpdir/arithmetic.sim")" == "42" ]]

cat > "$tmpdir/condition.sim" <<'SIMPL'
если 2 < 3
    сказать "условие"
иначе
    сказать "ошибка"
SIMPL
[[ "$(./simpl run "$tmpdir/condition.sim")" == "условие" ]]

cat > "$tmpdir/loop.sim" <<'SIMPL'
пусть i = 0
пока i < 3
    сказать i
    i = i + 1
SIMPL
[[ "$(./simpl run "$tmpdir/loop.sim")" == $'0\n1\n2' ]]

cat > "$tmpdir/function.sim" <<'SIMPL'
функция удвоить(x)
    вернуть x * 2
сказать удвоить(21)
SIMPL
[[ "$(./simpl run "$tmpdir/function.sim")" == "42" ]]

./simpl check "$tmpdir/arithmetic.sim"
./simpl compile "$tmpdir/arithmetic.sim" -o "$tmpdir/arithmetic.simc"
[[ "$(./simpl run "$tmpdir/arithmetic.simc")" == "42" ]]
if ./simpl unknown-command >"$tmpdir/unknown.out" 2>"$tmpdir/unknown.err"; then
    echo "Неизвестная команда должна завершаться ошибкой" >&2
    exit 1
fi
grep -q "Неизвестная команда 'unknown-command'" "$tmpdir/unknown.err"

echo "Smoke-тесты пройдены."
