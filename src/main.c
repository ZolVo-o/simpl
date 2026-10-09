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

static int compile_source(const char *path, Chunk *chunk)
{
    size_t length;
    char *source = read_file(path, &length);
    Parser parser;
    Node *program;
    int success;
    if (source == NULL) return 0;
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
        free(source);
        return 0;
    }
    success = compiler_compile(program, chunk);
    if (!success)
        fprintf(stderr, "%s:1:1: error: не удалось скомпилировать программу\n", path);
    ast_free(program);
    free(source);
    return success;
}

static char *default_output_path(const char *source_path)
{
    size_t length = strlen(source_path);
    size_t stem = length >= 4 && strcmp(source_path + length - 4, ".sim") == 0
        ? length - 4 : length;
    char *output = malloc(stem + sizeof(".simc"));
    if (output == NULL) return NULL;
    memcpy(output, source_path, stem);
    memcpy(output + stem, ".simc", sizeof(".simc"));
    return output;
}

static int command_run(const char *source_path)
{
    Chunk chunk;
    int success;
    if (!compile_source(source_path, &chunk)) return 0;
    success = vm_run_with_source(&chunk, source_path);
    chunk_free(&chunk);
    return success;
}

static int command_compile(const char *source_path, const char *output_path)
{
    Chunk chunk;
    char *generated_path = NULL;
    int success;
    if (output_path == NULL) {
        generated_path = default_output_path(source_path);
        output_path = generated_path;
        if (output_path == NULL) {
            fputs("Недостаточно памяти для имени выходного файла\n", stderr);
            return 0;
        }
    }
    if (!compile_source(source_path, &chunk)) {
        free(generated_path);
        return 0;
    }
    success = serialize_save(&chunk, output_path);
    if (!success)
        fprintf(stderr, "Не удалось записать байткод в «%s»\n", output_path);
    else
        printf("Скомпилировано: %s\n", output_path);
    chunk_free(&chunk);
    free(generated_path);
    return success;
}

static int command_disasm(const char *path)
{
    Chunk chunk;
    int success;
    chunk_init_empty(&chunk);
    if (!serialize_load(&chunk, path)) {
        fprintf(stderr, "Не удалось загрузить байткод «%s»\n", path);
        return 0;
    }
    success = disasm_dump(&chunk, stdout);
    if (!success) fprintf(stderr, "Не удалось дизассемблировать «%s»\n", path);
    chunk_free(&chunk);
    return success;
}

static int command_demo(void)
{
    PyObject *number;
    PyObject *constants[1];
    const uint8_t code[] = {
        OP_CONST, 0, 0, 0, 0,
        OP_SAY,
        OP_HALT
    };
    Chunk chunk;
    int success;
    printf("Python runtime: %s\n", Py_GetVersion());

    number = PyLong_FromLong(42);
    if (number == NULL) {
        PyErr_Print();
        return 1;
    }
    constants[0] = number;

    if (!chunk_init(&chunk, constants, 1, code, sizeof(code))) {
        fprintf(stderr, "Не удалось создать байткод.\n");
        Py_DECREF(number);
        return 1;
    }
    Py_DECREF(number);

    success = vm_run(&chunk);
    chunk_free(&chunk);
    return success ? 0 : 1;
}

static void print_usage(FILE *output)
{
    fputs("Использование:\n"
          "  simpl run <файл.sim>\n"
          "  simpl compile <файл.sim> [-o <файл.simc>]\n"
          "  simpl disasm <файл.simc>\n", output);
}

int main(int argc, char **argv)
{
    int result;
    Py_Initialize();
    if (!Py_IsInitialized()) {
        fprintf(stderr, "Не удалось инициализировать Python runtime.\n");
        return 1;
    }
    if (argc == 1) result = command_demo();
    else if (argc == 3 && strcmp(argv[1], "run") == 0)
        result = command_run(argv[2]) ? 0 : 1;
    else if (argc == 3 && strcmp(argv[1], "compile") == 0)
        result = command_compile(argv[2], NULL) ? 0 : 1;
    else if (argc == 5 && strcmp(argv[1], "compile") == 0
             && strcmp(argv[3], "-o") == 0)
        result = command_compile(argv[2], argv[4]) ? 0 : 1;
    else if (argc == 3 && strcmp(argv[1], "disasm") == 0)
        result = command_disasm(argv[2]) ? 0 : 1;
    else {
        print_usage(stderr);
        result = 2;
    }
    Py_Finalize();
    return result;
}
