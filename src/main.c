#include "chunk.h"
#include "compiler.h"
#include "disasm.h"
#include "opcodes.h"
#include "parser.h"
#include "serialize.h"
#include "vm.h"

#include <Python.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

static char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *data;
    if (file == NULL) {
        fprintf(stderr, "Не удалось открыть файл «%s»\n", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Не удалось прочитать файл «%s»\n", path);
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)size + 1);
    if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "Не удалось прочитать файл «%s»\n", path);
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    data[size] = '\0';
    *length = (size_t)size;
    return data;
}

static size_t source_column(const char *source, size_t length, size_t target,
                            size_t line)
{
    size_t i = 0, current_line = 1, column = 1;
    if (target > length) target = length;
    while (i < target && current_line < line) {
        if (source[i++] == '\n') ++current_line;
    }
    while (i < target && source[i] != '\n') {
        unsigned char c = (unsigned char)source[i];
        size_t width = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        if (i + width > target) width = 1;
        i += width;
        column += width == 4 ? 2 : 1;
    }
    return column;
}

static int compile_source_text(const char *path, const char *source, size_t length,
                              Chunk *chunk)
{
    Parser parser;
    Node *program;
    int success;
    parser_init(&parser, source, length);
    program = parser_parse(&parser);
    if (program == NULL) {
        size_t line = parser.error_line == 0 ? 1 : parser.error_line;
        size_t offset = parser.current.start >= source
            && parser.current.start <= source + length
            ? (size_t)(parser.current.start - source) : length;
        fprintf(stderr, "%s:%lu:%lu: error: %s\n", path,
                (unsigned long)line,
                (unsigned long)source_column(source, length, offset, line),
                parser.error != NULL ? parser.error : "ошибка разбора");
        return 2;
    }
    success = compiler_compile(program, chunk);
    if (!success)
        fprintf(stderr, "%s:1:1: error: не удалось скомпилировать программу\n", path);
    ast_free(program);
    return success ? 0 : 2;
}

static int compile_source(const char *path, Chunk *chunk)
{
    size_t length;
    char *source = read_file(path, &length);
    int status;
    if (source == NULL) return 1;
    status = compile_source_text(path, source, length, chunk);
    free(source);
    return status;
}

static int has_suffix(const char *path, const char *suffix)
{
    size_t path_length = strlen(path), suffix_length = strlen(suffix);
    return path_length >= suffix_length
        && strcmp(path + path_length - suffix_length, suffix) == 0;
}

static char *default_output_path(const char *source_path)
{
    size_t length = strlen(source_path);
    size_t stem = length >= 4 && strcmp(source_path + length - 4, ".sim") == 0
        ? length - 4 : length >= 6 && strcmp(source_path + length - 6, ".simpl") == 0
        ? length - 6 : length;
    char *output = malloc(stem + sizeof(".simc"));
    if (output == NULL) return NULL;
    memcpy(output, source_path, stem);
    memcpy(output + stem, ".simc", sizeof(".simc"));
    return output;
}

static int command_run(const char *source_path, int argc, char **argv)
{
    Chunk chunk;
    int success;
    int status;
    if (has_suffix(source_path, ".simc")) {
        chunk_init_empty(&chunk);
        if (!serialize_load(&chunk, source_path)) {
            fprintf(stderr, "Не удалось загрузить байткод «%s»\n", source_path);
            return 1;
        }
    } else {
        status = compile_source(source_path, &chunk);
        if (status != 0) return status;
    }
    success = vm_run_with_args(&chunk, source_path, argc, argv);
    chunk_free(&chunk);
    return success ? 0 : vm_was_interrupted() ? 130 : 3;
}

static int command_compile(const char *source_path, const char *output_path)
{
    Chunk chunk;
    char *generated_path = NULL;
    int success;
    int status;
    if (output_path == NULL) {
        generated_path = default_output_path(source_path);
        output_path = generated_path;
        if (output_path == NULL) {
            fputs("Недостаточно памяти для имени выходного файла\n", stderr);
            return 1;
        }
    }
    status = compile_source(source_path, &chunk);
    if (status != 0) {
        free(generated_path);
        return status;
    }
    success = strcmp(output_path, "-") == 0
        ? serialize_write(&chunk, stdout)
        : serialize_save(&chunk, output_path);
    if (!success)
        fprintf(stderr, "Не удалось записать байткод в «%s»\n", output_path);
    else if (strcmp(output_path, "-") != 0)
        printf("Скомпилировано: %s\n", output_path);
    chunk_free(&chunk);
    free(generated_path);
    return success ? 0 : 1;
}

static int command_check(const char *path)
{
    Chunk chunk;
    int status = compile_source(path, &chunk);
    if (status == 0) chunk_free(&chunk);
    return status;
}

static int command_disasm(const char *path, int no_names, int stats)
{
    Chunk chunk;
    int success;
    chunk_init_empty(&chunk);
    if (!serialize_load(&chunk, path)) {
        fprintf(stderr, "Не удалось загрузить байткод «%s»\n", path);
        return 1;
    }
    success = disasm_dump_options(&chunk, stdout, no_names, stats);
    if (!success) fprintf(stderr, "Не удалось дизассемблировать «%s»\n", path);
    chunk_free(&chunk);
    return success ? 0 : 1;
}

static void print_usage(FILE *output)
{
    fputs("Использование: simpl <команда> [аргументы]\n"
          "\nКоманды:\n"
          "  run <файл.sim|файл.simc> [--] [аргументы...]  запустить программу\n"
          "  compile <файл.sim> [-o <файл.simc|->]       скомпилировать\n"
          "  check <файл.sim>                             проверить без запуска\n"
          "  disasm <файл.simc> [--no-names] [--stats]   показать байткод\n"
          "  fmt <файл.sim>                                форматировать файл\n"
          "  new <каталог>                                создать проект\n"
          "  repl                                          интерактивная консоль\n"
          "  doc <файл.sim>                               извлечь ## комментарии\n"
          "\nФлаги: --help, -h — справка; --version, -v — версия\n", output);
}

static int fail_cli(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}

static int write_project_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "wb");
    size_t length = strlen(content);
    int ok;
    if (file == NULL) return 0;
    ok = fwrite(content, 1, length, file) == length;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int create_directory(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0777);
#endif
}

static int command_new(const char *directory)
{
    size_t length = strlen(directory);
    char *main_path = malloc(length + sizeof("/main.simpl"));
    char *readme_path = malloc(length + sizeof("/README.md"));
    int ok;
    if (main_path == NULL || readme_path == NULL) {
        free(main_path); free(readme_path);
        return fail_cli("Недостаточно памяти для пути проекта");
    }
    if (create_directory(directory) != 0) {
        fprintf(stderr, "Не удалось создать каталог «%s»: %s\n",
                directory, strerror(errno));
        free(main_path); free(readme_path);
        return 1;
    }
    snprintf(main_path, length + sizeof("/main.simpl"), "%s/main.simpl", directory);
    snprintf(readme_path, length + sizeof("/README.md"), "%s/README.md", directory);
    ok = write_project_file(main_path, "## Точка входа проекта\nсказать \"Привет, мир!\"\n")
        && write_project_file(readme_path,
                              "# Simpl project\n\nЗапуск: `simpl run main.simpl`\n");
    if (!ok) fprintf(stderr, "Не удалось записать файлы проекта «%s»\n", directory);
    else printf("Создан проект: %s\n", directory);
    free(main_path); free(readme_path);
    return ok ? 0 : 1;
}

static int command_doc(const char *path)
{
    size_t length, offset = 0;
    char *source = read_file(path, &length);
    if (source == NULL) return 1;
    while (offset < length) {
        size_t start = offset, end;
        while (offset < length && source[offset] != '\n') ++offset;
        end = offset;
        if (offset < length) ++offset;
        while (start < end && (source[start] == ' ' || source[start] == '\t')) ++start;
        if (end - start >= 2 && source[start] == '#' && source[start + 1] == '#') {
            start += 2;
            if (start < end && source[start] == ' ') ++start;
            fwrite(source + start, 1, end - start, stdout);
            putchar('\n');
        }
    }
    free(source);
    return ferror(stdout) ? 1 : 0;
}

static int format_line(FILE *output, const char *line, size_t length)
{
    size_t i = 0;
    int pending_space = 0, has_output = 0;
    char quote = 0;
    while (i < length) {
        char c = line[i];
        if (quote != 0) {
            fputc(c, output);
            if (c == '\\' && i + 1 < length) fputc(line[++i], output);
            else if (c == quote) quote = 0;
            ++i;
            has_output = 1;
            continue;
        }
        if (c == '"' || c == '\'') {
            if (pending_space && has_output) fputc(' ', output);
            pending_space = 0;
            quote = c;
            fputc(c, output);
            has_output = 1;
            ++i;
            continue;
        }
        if (c == '#') {
            if (pending_space && has_output) fputc(' ', output);
            fwrite(line + i, 1, length - i, output);
            return !ferror(output);
        }
        if (c == ' ' || c == '\t') {
            pending_space = 1;
            ++i;
            continue;
        }
        if (c == ',') {
            fputc(',', output);
            fputc(' ', output);
            pending_space = 0;
            has_output = 1;
            ++i;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            pending_space = 0;
            fputc(c, output);
            has_output = 1;
            ++i;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            char previous = has_output ? line[i == 0 ? 0 : i - 1] : '\0';
            if (pending_space && has_output
                && (previous == '=' || previous == '+' || previous == '-'
                    || previous == '*' || previous == '/' || previous == '%'
                    || previous == ',')) fputc(' ', output);
            pending_space = 0;
            fputc(c, output);
            has_output = 1;
            ++i;
            continue;
        }
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%'
            || c == '!'
            || c == '=' || c == '<' || c == '>') {
            size_t width = i + 1 < length
                && ((c == '=' && line[i + 1] == '=')
                    || (c == '!' && line[i + 1] == '=')
                    || ((c == '<' || c == '>') && line[i + 1] == '=')
                    || (c == '*' && line[i + 1] == '*')) ? 2 : 1;
            size_t previous_index = i;
            char previous = '\0';
            while (previous_index > 0 && line[previous_index - 1] == ' ') --previous_index;
            if (previous_index > 0) previous = line[previous_index - 1];
            int unary = c == '-' && (!has_output || previous == '=' || previous == '('
                         || previous == '[' || previous == ',' || previous == ':');
            if (has_output && (pending_space || !unary)) fputc(' ', output);
            fwrite(line + i, 1, width, output);
            pending_space = !unary;
            has_output = 1;
            i += width;
            continue;
        }
        if (pending_space && has_output) fputc(' ', output);
        pending_space = 0;
        fputc(c, output);
        has_output = 1;
        ++i;
    }
    return !ferror(output);
}

static int format_source(const char *path, const char *source, size_t length)
{
    size_t levels[128] = { 0 };
    size_t depth = 0, offset = 0;
    FILE *output;
    char *temporary = malloc(strlen(path) + sizeof(".tmp"));
    if (temporary == NULL) return fail_cli("Недостаточно памяти для временного файла");
    sprintf(temporary, "%s.tmp", path);
    output = fopen(temporary, "wb");
    if (output == NULL) {
        fprintf(stderr, "Не удалось открыть «%s»: %s\n", temporary, strerror(errno));
        free(temporary);
        return 1;
    }
    while (offset < length) {
        size_t start = offset, end, indent = 0, leading = 0, content, i;
        while (offset < length && source[offset] != '\n') ++offset;
        end = offset;
        if (offset < length) ++offset;
        if (end > start && source[end - 1] == '\r') --end;
        while (start + leading < end
               && (source[start + leading] == ' ' || source[start + leading] == '\t')) {
            indent += source[start + leading] == '\t' ? 4 : 1;
            ++leading;
        }
        content = start + leading;
        while (end > content && (source[end - 1] == ' ' || source[end - 1] == '\t')) --end;
        if (content == end) {
            fputc('\n', output);
            continue;
        }
        if (indent > levels[depth] && depth + 1 < sizeof(levels) / sizeof(levels[0]))
            levels[++depth] = indent;
        else while (depth > 0 && indent < levels[depth]) --depth;
        for (i = 0; i < depth * 4; ++i) fputc(' ', output);
        if (!format_line(output, source + content, end - content)) {
            fclose(output);
            remove(temporary);
            free(temporary);
            return 1;
        }
        fputc('\n', output);
    }
    if (fclose(output) != 0 || rename(temporary, path) != 0) {
        fprintf(stderr, "Не удалось сохранить отформатированный файл «%s»\n", path);
        remove(temporary);
        free(temporary);
        return 1;
    }
    free(temporary);
    return 0;
}

static int command_fmt(const char *path)
{
    size_t length;
    char *source = read_file(path, &length);
    Chunk chunk;
    int status;
    if (source == NULL) return 1;
    status = compile_source_text(path, source, length, &chunk);
    if (status == 0) {
        chunk_free(&chunk);
        status = format_source(path, source, length);
    }
    free(source);
    return status;
}

static int append_text(char **buffer, size_t *length, size_t *capacity,
                       const char *text, size_t count)
{
    char *grown;
    size_t needed = *length + count + 1;
    if (needed > *capacity) {
        size_t next = *capacity == 0 ? 128 : *capacity;
        while (next < needed) {
            if (next > SIZE_MAX / 2) return 0;
            next *= 2;
        }
        grown = realloc(*buffer, next);
        if (grown == NULL) return 0;
        *buffer = grown;
        *capacity = next;
    }
    memcpy(*buffer + *length, text, count);
    *length += count;
    (*buffer)[*length] = '\0';
    return 1;
}

static int repl_execute(const char *source, size_t length)
{
    Chunk chunk;
    int status = compile_source_text("<repl>", source, length, &chunk);
    if (status != 0) return status;
    status = vm_run_with_source(&chunk, "<repl>") ? 0 : 3;
    chunk_free(&chunk);
    return status;
}

static int command_repl(void)
{
    char *buffer = NULL;
    size_t length = 0, capacity = 0;
    char line[4096];
    int result = 0;
    fputs("Simpl REPL — пустая строка выполняет ввод, Ctrl+D завершает.\n", stdout);
    for (;;) {
        size_t line_length;
        fputs(length == 0 ? ">>> " : "... ", stdout);
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) break;
        line_length = strlen(line);
        if (line_length == 1 && line[0] == '\n' && length > 0) {
            int status = repl_execute(buffer, length);
            if (status != 0) result = status;
            length = 0;
            buffer[0] = '\0';
            continue;
        }
        if (!append_text(&buffer, &length, &capacity, line, line_length)) {
            free(buffer);
            return fail_cli("Недостаточно памяти для ввода REPL");
        }
    }
    if (length > 0) result = repl_execute(buffer, length);
    free(buffer);
    return result;
}

static int initialize_python(void)
{
    Py_Initialize();
    if (!Py_IsInitialized()) {
        fputs("Не удалось инициализировать Python runtime.\n", stderr);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    int result = 1;
    const char *command;
    if (argc == 1) {
        fputs("Нет команды. Используй simpl --help.\n", stderr);
        return 1;
    }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        print_usage(stdout);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
        puts("Simpl 0.1.0");
        return 0;
    }
    command = argv[1];
    if (!initialize_python()) return 1;
    if (strcmp(command, "run") == 0) {
        char **program_args;
        char **filtered_args = NULL;
        int program_argc, i, separator = -1;
        if (argc < 3) {
            result = fail_cli("Для команды run укажи файл программы");
        } else {
            program_args = argv + 3;
            program_argc = argc - 3;
            for (i = 3; i < argc; ++i) {
                if (strcmp(argv[i], "--") == 0) {
                    separator = i;
                    break;
                }
            }
            if (separator >= 0) {
                int source_index, target_index = 0;
                program_argc = argc - 4;
                if (program_argc > 0) {
                    filtered_args = malloc((size_t)program_argc * sizeof(*filtered_args));
                    if (filtered_args == NULL) {
                        result = fail_cli("Недостаточно памяти для аргументов программы");
                        goto run_done;
                    }
                    for (source_index = 3; source_index < argc; ++source_index) {
                        if (source_index != separator)
                            filtered_args[target_index++] = argv[source_index];
                    }
                    program_args = filtered_args;
                } else {
                    program_args = NULL;
                }
            }
            result = command_run(argv[2], program_argc, program_args);
        }
run_done:
        free(filtered_args);
    } else if (strcmp(command, "compile") == 0) {
        if (argc < 3) result = fail_cli("Для команды compile укажи исходный файл");
        else if (argc == 3) result = command_compile(argv[2], NULL);
        else if (argc == 5 && strcmp(argv[3], "-o") == 0)
            result = command_compile(argv[2], argv[4]);
        else if (argv[3][0] == '-') {
            fprintf(stderr, "Неизвестный флаг '%s'\n", argv[3]);
            result = 1;
        } else if (argc > 3) result = fail_cli("Слишком много аргументов для compile");
        else result = fail_cli("Неверные аргументы compile; используй -o <файл>");
    } else if (strcmp(command, "check") == 0) {
        if (argc != 3) result = fail_cli(argc < 3
            ? "Для команды check укажи исходный файл" : "Слишком много аргументов для check");
        else result = command_check(argv[2]);
    } else if (strcmp(command, "disasm") == 0) {
        int no_names = 0, stats = 0, i, bad = 0;
        const char *path = NULL;
        for (i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--no-names") == 0) no_names = 1;
            else if (strcmp(argv[i], "--stats") == 0) stats = 1;
            else if (argv[i][0] == '-') {
                fprintf(stderr, "Неизвестный флаг '%s'\n", argv[i]);
                result = 1; bad = 1; break;
            }
            else if (path != NULL) { result = fail_cli("Слишком много аргументов для disasm"); bad = 1; break; }
            else path = argv[i];
        }
        if (!bad && path == NULL) result = fail_cli("Для команды disasm укажи файл байткода");
        else if (!bad) result = command_disasm(path, no_names, stats);
    } else if (strcmp(command, "fmt") == 0) {
        if (argc != 3) result = fail_cli("Использование: simpl fmt <файл.sim>");
        else result = command_fmt(argv[2]);
    } else if (strcmp(command, "new") == 0) {
        if (argc != 3) result = fail_cli("Использование: simpl new <каталог>");
        else result = command_new(argv[2]);
    } else if (strcmp(command, "doc") == 0) {
        if (argc != 3) result = fail_cli("Использование: simpl doc <файл.sim>");
        else result = command_doc(argv[2]);
    } else if (strcmp(command, "repl") == 0) {
        if (argc != 2) result = fail_cli("Слишком много аргументов для repl");
        else result = command_repl();
    } else if (command[0] == '-') {
        fprintf(stderr, "Неизвестный флаг '%s'\n", command);
        result = 1;
    } else {
        fprintf(stderr, "Неизвестная команда '%s', попробуй --help\n", command);
        result = 1;
    }
    Py_Finalize();
    return result;
}
