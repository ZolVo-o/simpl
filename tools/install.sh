#!/bin/sh
set -eu

REPOSITORY='ZolVo-o/simpl'
PYTHON_VERSION='3.12'
API_URL="https://api.github.com/repos/$REPOSITORY/releases/latest"

if [ "$#" -gt 0 ]; then
    case "$1" in
        -h|--help)
            printf 'Использование: %s [--help]\n' "$0"
            printf 'Установить последний релиз Simpl в ~/.local/bin.\n'
            exit 0
            ;;
        *)
            printf 'Использование: %s [--help]\n' "$0" >&2
            exit 2
            ;;
    esac
fi

die()
{
    printf 'Ошибка: %s\n' "$*" >&2
    exit 1
}

case "$(uname -s)" in
    Linux)
        [ "$(uname -m)" = x86_64 ] || die "пока поддерживается только Linux x86_64"
        command -v ldconfig >/dev/null 2>&1 || die "нужен glibc и ldconfig; Alpine/musl пока не поддерживается"
        ldconfig -p 2>/dev/null | grep -Fq "libpython${PYTHON_VERSION}.so.1.0" \
            || die "не найдена libpython${PYTHON_VERSION}. Установите runtime Python ${PYTHON_VERSION}"
        ASSET_SUFFIX='linux-x64.tar.gz'
        ;;
    Darwin)
        PYTHON_FRAMEWORK="/Library/Frameworks/Python.framework/Versions/${PYTHON_VERSION}/Python"
        [ -f "$PYTHON_FRAMEWORK" ] \
            || die "не найден Python ${PYTHON_VERSION} framework; установите Python ${PYTHON_VERSION} с python.org"
        ASSET_SUFFIX='macos.tar.gz'
        ;;
    *)
        die "установка поддерживается только на Linux x86_64 и macOS"
        ;;
esac

command -v curl >/dev/null 2>&1 || die "не найдена команда curl"
command -v tar >/dev/null 2>&1 || die "не найдена команда tar"

PYTHON=$(command -v python3 || command -v python3.12 || true)
[ -n "$PYTHON" ] || die "для чтения метаданных GitHub нужен Python 3"

INSTALL_DIR=${SIMPL_INSTALL_DIR:-"$HOME/.local/bin"}
TEMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/simpl-install.XXXXXX")
trap 'rm -rf "$TEMP_DIR"' EXIT HUP INT TERM

curl -fsSL -H 'Accept: application/vnd.github+json' "$API_URL" -o "$TEMP_DIR/release.json" \
    || die "не удалось получить последний релиз с GitHub"

DOWNLOAD_URL=$(
    "$PYTHON" -c '
import json, sys
with open(sys.argv[1], encoding="utf-8") as release_file:
    release = json.load(release_file)
suffix = sys.argv[2]
assets = [asset for asset in release.get("assets", []) if asset["name"].endswith(suffix)]
if len(assets) != 1:
    raise SystemExit("В последнем релизе не найден единственный архив " + suffix)
print(assets[0]["browser_download_url"])
' "$TEMP_DIR/release.json" "$ASSET_SUFFIX"
) || die "не удалось определить ссылку на архив Simpl"

case "$DOWNLOAD_URL" in
    "https://github.com/$REPOSITORY/releases/download/"*) ;;
    *) die "GitHub вернул неожиданный адрес загрузки" ;;
esac

curl -fL --retry 3 "$DOWNLOAD_URL" -o "$TEMP_DIR/simpl.tar.gz" \
    || die "не удалось скачать архив Simpl"
tar -tzf "$TEMP_DIR/simpl.tar.gz" | grep -Fxq 'simpl' \
    || die "в архиве релиза не найден исполняемый файл simpl"
tar -xzf "$TEMP_DIR/simpl.tar.gz" -C "$TEMP_DIR" simpl \
    || die "не удалось распаковать Simpl"

mkdir -p "$INSTALL_DIR"
install -m 0755 "$TEMP_DIR/simpl" "$INSTALL_DIR/simpl.tmp.$$"
mv -f "$INSTALL_DIR/simpl.tmp.$$" "$INSTALL_DIR/simpl"

printf 'Simpl установлен: %s/simpl\n' "$INSTALL_DIR"
case ":$PATH:" in
    *":$INSTALL_DIR:"*) ;;
    *) printf 'Добавьте каталог в PATH: export PATH="%s:$PATH"\n' "$INSTALL_DIR" ;;
esac
