#include "../src/compiler.h"
#include "../src/parser.h"
#include "../src/vm.h"
#include "test.h"

#include <Python.h>

static int execute(const char *source)
{
    Parser parser;
    Node *program;
    Chunk chunk;
    int result;

    parser_init(&parser, source, strlen(source));
    program = parser_parse(&parser);
    if (program == NULL) return 0;
    if (!compiler_compile(program, &chunk)) {
        ast_free(program);
        return 0;
    }
    result = vm_run(&chunk);
    chunk_free(&chunk);
    ast_free(program);
    return result;
}

int main(void)
{
    Py_Initialize();
    TEST_ASSERT(Py_IsInitialized());

    TEST_ASSERT(execute("сказать 1 + 2 * 3\n"));
    TEST_ASSERT(execute("сказать (1 + 2) * 3\n"));
    TEST_ASSERT(execute("сказать 2 ** 3\n"));
    TEST_ASSERT(execute("сказать 7 % 3\n"));
    TEST_ASSERT(execute("пусть результат = ничего\nсказать тип(результат)\n"));
    TEST_ASSERT(execute("если 2 < 3\n    сказать 1\n"));
    TEST_ASSERT(execute("если да\n    сказать 1\nиначе\n    сказать 2\n"));
    TEST_ASSERT(execute("пусть x = 0\nпока x < 3\n    x = x + 1\n"));
    TEST_ASSERT(execute("повторить 3 раз как i\n    сказать i\n"));
    TEST_ASSERT(execute("функция f(x)\n    вернуть x + 1\nсказать f(4)\n"));
    TEST_ASSERT(execute("функция f(n)\n    если n <= 1\n        вернуть 1\n    вернуть n * f(n - 1)\nсказать f(5)\n"));
    TEST_ASSERT(execute("пусть xs = [1, 2]\nxs[0] = 3\nсказать xs[0]\n"));
    TEST_ASSERT(execute("пусть d = {\"x\": 1}\nсказать d[\"x\"]\n"));
    TEST_ASSERT(execute("пусть xs = [1, 2, 3]\nдля каждого x в xs\n    сказать x\n"));
    TEST_ASSERT(execute("попробовать\n    сказать 1 / 0\nпоймать e\n    сказать e\n"));
    TEST_ASSERT(!execute("сказать missing\n"));

    TEST_REPORT("VM");
    Py_Finalize();
    return EXIT_SUCCESS;
}
