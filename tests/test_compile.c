#include "../src/compiler.h"
#include "../src/disasm.h"
#include "../src/parser.h"
#include "../src/serialize.h"
#include "../src/vm.h"

#include <Python.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *sources[] = {
        "если да\n    сказать \"если\"\nиначе\n    сказать \"иначе\"\n",
        "если нет\n    сказать \"неверно\"\nиначе\n    сказать \"иначе\"\n",
        "пусть i = 0\nпока i < 3\n    сказать i\n    i = i + 1\n",
        "повторить 3 раз как i\n    сказать i\n",
        "пусть числа = [1, 2, 3]\n"
        "числа[0] = 10\n"
        "сказать числа[0]\n",
        "пусть человек = {\"имя\": \"Иван\", \"возраст\": 30}\n"
        "сказать человек[\"имя\"]\n",
        "пусть числа = [1, 2, 3]\n"
        "пусть сумма = 0\n"
        "для каждого число в числа\n"
        "    сумма = сумма + число\n"
        "сказать сумма\n",
        "функция факториал(n)\n"
        "    если n <= 1\n"
        "        вернуть 1\n"
        "    иначе\n"
        "        вернуть n * факториал(n - 1)\n"
        "сказать факториал(5)\n"
    };
    size_t i;
    Py_Initialize();
    for (i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
        Parser parser;
        Node *program;
        Chunk chunk;
        int result;
        parser_init(&parser, sources[i], strlen(sources[i]));
        program = parser_parse(&parser);
        if (program == NULL) {
            fprintf(stderr, "Ошибка тестового парсинга, строка %lu: %s\n",
                    (unsigned long)parser.error_line,
                    parser.error != NULL ? parser.error : "неизвестная ошибка");
            Py_Finalize();
            return 1;
        }
        assert(compiler_compile(program, &chunk));
        result = vm_run(&chunk);
        chunk_free(&chunk);
        ast_free(program);
        assert(result);
    }
    {
        const char *source =
            "функция удвоить(x)\n"
            "    вернуть x * 2\n"
            "пусть данные = [20, 22]\n"
            "сказать удвоить(данные[0] + данные[1])\n"
            "сказать 1.5\n"
            "сказать да\n"
            "сказать \"текст\"\n";
        Parser parser;
        Node *program;
        Chunk original;
        Chunk loaded;
        parser_init(&parser, source, strlen(source));
        program = parser_parse(&parser);
        if (program == NULL) {
            fprintf(stderr, "Ошибка тестового парсинга сериализации, строка %lu: %s\n",
                    (unsigned long)parser.error_line,
                    parser.error != NULL ? parser.error : "неизвестная ошибка");
            Py_Finalize();
            return 1;
        }
        assert(compiler_compile(program, &original));
        chunk_init_empty(&loaded);
        assert(serialize_save(&original, "test.simc"));
        assert(serialize_load(&loaded, "test.simc"));
        assert(loaded.constant_count == original.constant_count);
        assert(loaded.name_count == original.name_count);
        assert(loaded.code_count == original.code_count);
        assert(loaded.function_count == original.function_count);
        assert(loaded.functions[0].name_index == original.functions[0].name_index);
        assert(loaded.functions[0].parameter_count == original.functions[0].parameter_count);
        assert(loaded.functions[0].address == original.functions[0].address);
        assert(memcmp(loaded.code, original.code, original.code_count) == 0);
        assert(memcmp(loaded.lines, original.lines,
                      original.code_count * sizeof(*original.lines)) == 0);
        assert(vm_run(&loaded));
        {
            FILE *file = fopen("test.simc", "r+b");
            assert(file != NULL);
            assert(fputc('X', file) != EOF);
            assert(fclose(file) == 0);
            assert(!serialize_load(&loaded, "test.simc"));
            assert(loaded.code_count == original.code_count);
        }
        chunk_free(&loaded);
        chunk_free(&original);
        ast_free(program);
        remove("test.simc");
    }
    {
        const char *source = "пусть значение = 1\nсказать значние\n";
        Parser parser;
        Node *program;
        Chunk chunk;
        parser_init(&parser, source, strlen(source));
        program = parser_parse(&parser);
        assert(program != NULL);
        assert(compiler_compile(program, &chunk));
        assert(chunk.code_count > 0 && chunk.lines[chunk.code_count - 1] == 2);
        assert(!vm_run(&chunk));
        chunk_free(&chunk);
        ast_free(program);
    }
    {
        const char *source = "пусть имя = спросить\nсказать имя\n";
        FILE *input_file = fopen("test-input.txt", "w");
        Parser parser;
        Node *program;
        Chunk chunk;
        assert(input_file != NULL);
        assert(fputs("Алиса\n", input_file) >= 0);
        assert(fclose(input_file) == 0);
        assert(freopen("test-input.txt", "r", stdin) != NULL);
        parser_init(&parser, source, strlen(source));
        program = parser_parse(&parser);
        assert(program != NULL);
        assert(compiler_compile(program, &chunk));
        assert(vm_run(&chunk));
        chunk_free(&chunk);
        ast_free(program);
        remove("test-input.txt");
    }
    {
        const char *source = "сказать 2 + 3\n";
        Parser parser;
        Node *program;
        Chunk chunk;
        FILE *listing;
        char text[256];
        size_t length;
        parser_init(&parser, source, strlen(source));
        program = parser_parse(&parser);
        assert(program != NULL);
        assert(compiler_compile(program, &chunk));
        assert(chunk.constant_count == 1);
        assert(PyLong_AsLong(chunk.constants[0]) == 5);
        listing = tmpfile();
        assert(listing != NULL);
        assert(disasm_dump(&chunk, listing));
        rewind(listing);
        length = fread(text, 1, sizeof(text) - 1, listing);
        text[length] = '\0';
        assert(strstr(text, "CONST") != NULL && strstr(text, "= 5") != NULL);
        assert(strstr(text, "ADD") == NULL);
        fclose(listing);
        chunk_free(&chunk);
        ast_free(program);
    }
    {
        const char *source = "сказать \"одинаково\"\nсказать \"одинаково\"\n";
        Parser parser;
        Node *program;
        Chunk chunk;
        parser_init(&parser, source, strlen(source));
        program = parser_parse(&parser);
        assert(program != NULL);
        assert(compiler_compile(program, &chunk));
        assert(chunk.constant_count == 1);
        chunk_free(&chunk);
        ast_free(program);
    }
    {
        const char *source =
            "пусть слова = разделить(\"красный,синий\", \",\")\n"
            "сказать длина(слова)\n"
            "сказать тип(слова)\n"
            "сказать верх(\"привет\")\n"
            "сказать низ(\"ПРИВЕТ\")\n"
            "сказать соединить(слова, \" / \")\n"
            "сказать тип(42)\n";
        Parser parser;
        Node *program;
        Chunk chunk;
        parser_init(&parser, source, strlen(source));
        program = parser_parse(&parser);
        assert(program != NULL);
        assert(compiler_compile(program, &chunk));
        assert(vm_run(&chunk));
        chunk_free(&chunk);
        ast_free(program);
    }
    {
        const char *source =
            "функция упасть()\n"
            "    вернуть 1 / 0\n"
            "попробовать\n"
            "    пусть результат = упасть()\n"
            "поймать ошибка\n"
            "    сказать ошибка\n"
            "попробовать\n"
            "    сказать \"успех\"\n"
            "поймать другая_ошибка\n"
            "    сказать другая_ошибка\n";
        Parser parser;
        Node *program;
        Chunk chunk;
        parser_init(&parser, source, strlen(source));
        program = parser_parse(&parser);
        if (program == NULL) {
            fprintf(stderr, "Ошибка тестового try/catch, строка %lu: %s\n",
                    (unsigned long)parser.error_line,
                    parser.error != NULL ? parser.error : "неизвестная ошибка");
            Py_Finalize();
            return 1;
        }
        assert(compiler_compile(program, &chunk));
        assert(vm_run(&chunk));
        chunk_free(&chunk);
        ast_free(program);
    }
    Py_Finalize();
    return 0;
}
