#include "vm.h"

#include "lexer.h"
#include "opcodes.h"

#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#define VM_STACK_MAX 256
#define VM_FRAME_MAX 256
#define VM_HANDLER_MAX 256

static volatile sig_atomic_t vm_interrupted;

static void handle_interrupt(int signal_number)
{
    (void)signal_number;
    vm_interrupted = 1;
}

int vm_was_interrupted(void)
{
    return vm_interrupted != 0;
}

typedef struct {
    size_t return_ip;
    size_t stack_base;
    PyObject **locals;
} CallFrame;

typedef struct {
    size_t target_ip;
    size_t stack_count;
    size_t frame_count;
} ExceptionHandler;

static int read_u32(const Chunk *chunk, size_t *ip, uint32_t *value)
{
    if (chunk->code_count - *ip < 4) return 0;
    *value = (uint32_t)chunk->code[*ip]
           | ((uint32_t)chunk->code[*ip + 1] << 8)
           | ((uint32_t)chunk->code[*ip + 2] << 16)
           | ((uint32_t)chunk->code[*ip + 3] << 24);
    *ip += 4;
    return 1;
}

static size_t name_distance(const char *left, const char *right)
{
    PyObject *a = PyUnicode_DecodeUTF8(left, (Py_ssize_t)strlen(left), "strict");
    PyObject *b = PyUnicode_DecodeUTF8(right, (Py_ssize_t)strlen(right), "strict");
    Py_ssize_t an, bn, i, j;
    size_t *previous, *current, distance = SIZE_MAX;
    if (a == NULL || b == NULL) {
        Py_XDECREF(a); Py_XDECREF(b); PyErr_Clear();
        return SIZE_MAX;
    }
    an = PyUnicode_GetLength(a); bn = PyUnicode_GetLength(b);
    if (an < 0 || bn < 0 || an - bn > 2 || bn - an > 2
        || (size_t)(bn + 1) > SIZE_MAX / sizeof(*previous)) goto done;
    previous = malloc((size_t)(bn + 1) * sizeof(*previous));
    current = malloc((size_t)(bn + 1) * sizeof(*current));
    if (previous == NULL || current == NULL) {
        free(previous); free(current);
        goto done;
    }
    for (j = 0; j <= bn; ++j) previous[j] = (size_t)j;
    for (i = 1; i <= an; ++i) {
        current[0] = (size_t)i;
        for (j = 1; j <= bn; ++j) {
            size_t substitution = previous[j - 1]
                + (PyUnicode_ReadChar(a, i - 1) != PyUnicode_ReadChar(b, j - 1));
            size_t deletion = previous[j] + 1;
            size_t insertion = current[j - 1] + 1;
            current[j] = substitution < deletion ? substitution : deletion;
            if (insertion < current[j]) current[j] = insertion;
        }
        { size_t *swap = previous; previous = current; current = swap; }
    }
    distance = previous[bn];
    free(previous); free(current);
done:
    Py_DECREF(a); Py_DECREF(b);
    return distance;
}

static const char *suggest_name(const Chunk *chunk, uint32_t unknown)
{
    size_t i, best = 3;
    const char *suggestion = NULL;
    for (i = 0; i < chunk->name_count; ++i) {
        size_t distance;
        if (i == unknown) continue;
        distance = name_distance(chunk->names[unknown], chunk->names[i]);
        if (distance < best) { best = distance; suggestion = chunk->names[i]; }
    }
    return suggestion;
}

static void report_python_error(const Chunk *chunk, size_t instruction_ip,
                                const char *source_path)
{
    PyObject *type = NULL, *value = NULL, *traceback = NULL, *message = NULL;
    uint32_t line = instruction_ip < chunk->code_count ? chunk->lines[instruction_ip] : 0;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    message = value != NULL ? PyObject_Str(value) : NULL;
    if (message != NULL) {
        const char *text = PyUnicode_AsUTF8(message);
        if (source_path != NULL)
            fprintf(stderr, "%s:%u:1: error: %s\n", source_path, line,
                    text != NULL ? text : "ошибка выполнения");
        else
            fprintf(stderr, "Ошибка в строке %u: %s\n", line,
                    text != NULL ? text : "ошибка выполнения");
    } else {
        PyErr_Clear();
        if (source_path != NULL)
            fprintf(stderr, "%s:%u:1: error: ошибка выполнения\n", source_path, line);
        else
            fprintf(stderr, "Ошибка в строке %u: ошибка выполнения\n", line);
    }
    Py_XDECREF(message); Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(traceback);
    PyErr_Clear();
}

static PyObject *read_input_line(void)
{
    size_t length = 0, capacity = 64;
    char *buffer = malloc(capacity);
    int character;
    PyObject *result;
    if (buffer == NULL) return PyErr_NoMemory();
    while ((character = fgetc(stdin)) != EOF && character != '\n') {
        if (length == capacity) {
            char *grown;
            size_t next;
            if (capacity > SIZE_MAX / 2) {
                free(buffer);
                return PyErr_NoMemory();
            }
            next = capacity * 2;
            grown = realloc(buffer, next);
            if (grown == NULL) {
                free(buffer);
                return PyErr_NoMemory();
            }
            buffer = grown;
            capacity = next;
        }
        buffer[length++] = (char)character;
    }
    if (ferror(stdin)) {
        free(buffer);
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }
    if (length > 0 && buffer[length - 1] == '\r') --length;
    if (length > (size_t)PY_SSIZE_T_MAX) {
        free(buffer);
        return PyErr_NoMemory();
    }
    result = PyUnicode_DecodeUTF8(buffer, (Py_ssize_t)length, "strict");
    free(buffer);
    return result;
}

static PyObject *builtin_type(PyObject *value)
{
    const char *name;
    if (value == Py_None) name = "ничего";
    else if (PyBool_Check(value)) name = "логическое";
    else if (PyLong_Check(value) || PyFloat_Check(value)) name = "число";
    else if (PyUnicode_Check(value)) name = "строка";
    else if (PyList_Check(value)) name = "список";
    else if (PyDict_Check(value)) name = "словарь";
    else name = Py_TYPE(value)->tp_name;
    return PyUnicode_FromString(name);
}

static PyObject *call_builtin(uint32_t builtin, PyObject *const *args,
                              uint32_t argc, int program_argc, char **program_argv)
{
    switch (builtin) {
    case BUILTIN_LENGTH: {
        Py_ssize_t length;
        if (argc != 1) break;
        length = PyObject_Length(args[0]);
        return length < 0 ? NULL : PyLong_FromSsize_t(length);
    }
    case BUILTIN_TYPE:
        if (argc == 1) return builtin_type(args[0]);
        break;
    case BUILTIN_UPPER:
        if (argc == 1 && PyUnicode_Check(args[0]))
            return PyObject_CallMethod(args[0], "upper", NULL);
        if (argc == 1) PyErr_SetString(PyExc_TypeError, "верх ожидает строку");
        break;
    case BUILTIN_LOWER:
        if (argc == 1 && PyUnicode_Check(args[0]))
            return PyObject_CallMethod(args[0], "lower", NULL);
        if (argc == 1) PyErr_SetString(PyExc_TypeError, "низ ожидает строку");
        break;
    case BUILTIN_SPLIT:
        if (argc == 2 && PyUnicode_Check(args[0]) && PyUnicode_Check(args[1]))
            return PyUnicode_Split(args[0], args[1], -1);
        if (argc == 2) PyErr_SetString(PyExc_TypeError,
                                       "разделить ожидает строку и разделитель");
        break;
    case BUILTIN_JOIN:
        if (argc == 2 && PyUnicode_Check(args[1]))
            return PyUnicode_Join(args[1], args[0]);
        if (argc == 2) PyErr_SetString(PyExc_TypeError,
                                       "соединить ожидает список и строку-разделитель");
        break;
    case BUILTIN_ARGUMENTS:
        if (argc != 0) break;
        {
            PyObject *result = PyList_New(program_argc);
            int i;
            if (result == NULL) return NULL;
            for (i = 0; i < program_argc; ++i) {
                PyObject *value = PyUnicode_DecodeFSDefault(program_argv[i]);
                if (value == NULL) {
                    Py_DECREF(result);
                    return NULL;
                }
                PyList_SET_ITEM(result, i, value);
            }
            return result;
        }
    default:
        PyErr_SetString(PyExc_RuntimeError, "неизвестная встроенная функция");
        return NULL;
    }
    if (!PyErr_Occurred())
        PyErr_SetString(PyExc_TypeError, "неверное число аргументов встроенной функции");
    return NULL;
}

int vm_run(const Chunk *chunk)
{
    return vm_run_with_args(chunk, NULL, 0, NULL);
}

int vm_run_with_source(const Chunk *chunk, const char *source_path)
{
    return vm_run_with_args(chunk, source_path, 0, NULL);
}

int vm_run_with_args(const Chunk *chunk, const char *source_path,
                     int program_argc, char **program_argv)
{
    PyObject *stack[VM_STACK_MAX] = { NULL };
    PyObject **globals = calloc(chunk->name_count == 0 ? 1 : chunk->name_count,
                                sizeof(*globals));
    CallFrame frames[VM_FRAME_MAX];
    ExceptionHandler handlers[VM_HANDLER_MAX];
    size_t handler_count = 0;
    size_t frame_count = 0;
    size_t stack_count = 0;
    size_t ip = 0;
    size_t instruction_ip = 0;
    size_t i;
    int success = 0;
    void (*previous_sigint)(int);

    if (globals == NULL) return 0;
    vm_interrupted = 0;
    previous_sigint = signal(SIGINT, handle_interrupt);
dispatch:
    while (ip < chunk->code_count) {
        if (vm_interrupted) {
            fputs("Прервано пользователем\n", stderr);
            goto done;
        }
        instruction_ip = ip;
        uint8_t opcode = chunk->code[ip++];
        uint32_t index;

        switch (opcode) {
        case OP_CONST:
        case OP_LOAD:
        case OP_STORE:
            if (!read_u32(chunk, &ip, &index)) {
                fputs("Ошибка VM: повреждён операнд индекса\n", stderr);
                goto done;
            }
            if (opcode == OP_CONST) {
                if (index >= chunk->constant_count || stack_count == VM_STACK_MAX) {
                    fputs("Ошибка VM: индекс константы неверен или стек переполнен\n", stderr);
                    goto done;
                }
                Py_INCREF(chunk->constants[index]);
                stack[stack_count++] = chunk->constants[index];
            } else if (index >= chunk->name_count) {
                fputs("Ошибка VM: индекс имени вне диапазона\n", stderr);
                goto done;
            } else if (opcode == OP_LOAD) {
                PyObject *value = frame_count > 0
                    ? frames[frame_count - 1].locals[index] : NULL;
                if (value == NULL) value = globals[index];
                if (value == NULL || stack_count == VM_STACK_MAX) {
                    const char *suggestion = suggest_name(chunk, index);
                    if (source_path != NULL)
                        fprintf(stderr, "%s:%u:1: error: неизвестная переменная '%s'.",
                                source_path, chunk->lines[instruction_ip], chunk->names[index]);
                    else
                        fprintf(stderr, "Ошибка в строке %u: неизвестная переменная '%s'.",
                                chunk->lines[instruction_ip], chunk->names[index]);
                    if (suggestion != NULL)
                        fprintf(stderr, " Возможно, ты имел в виду '%s'?", suggestion);
                    fputc('\n', stderr);
                    goto done;
                }
                Py_INCREF(value);
                stack[stack_count++] = value;
            } else {
                if (stack_count == 0) {
                    fputs("Ошибка VM: стек пуст при сохранении\n", stderr);
                    goto done;
                }
                if (frame_count > 0) {
                    Py_XDECREF(frames[frame_count - 1].locals[index]);
                    frames[frame_count - 1].locals[index] = stack[--stack_count];
                } else {
                    Py_XDECREF(globals[index]);
                    globals[index] = stack[--stack_count];
                }
            }
            break;
        case OP_SAY:
            if (stack_count == 0) {
                fputs("Ошибка VM: стек пуст при выводе\n", stderr);
                goto done;
            }
            if (PyObject_Print(stack[--stack_count], stdout, Py_PRINT_RAW) < 0) {
                Py_DECREF(stack[stack_count]);
                goto python_error;
            }
            Py_DECREF(stack[stack_count]);
            if (putchar('\n') == EOF) goto done;
            break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_POW:
        case OP_EQ:
        case OP_NE:
        case OP_LT:
        case OP_LE:
        case OP_GT:
        case OP_GE:
        case OP_AND:
        case OP_OR: {
            PyObject *left;
            PyObject *right;
            PyObject *result;
            OpCode op = (OpCode)opcode;
            if (stack_count < 2) {
                fputs("Ошибка VM: недостаточно значений для операции\n", stderr);
                goto done;
            }
            right = stack[--stack_count];
            left = stack[--stack_count];
            switch (op) {
            case OP_ADD: result = PyNumber_Add(left, right); break;
            case OP_SUB: result = PyNumber_Subtract(left, right); break;
            case OP_MUL: result = PyNumber_Multiply(left, right); break;
            case OP_DIV: result = PyNumber_TrueDivide(left, right); break;
            case OP_MOD: result = PyNumber_Remainder(left, right); break;
            case OP_POW: result = PyNumber_Power(left, right, Py_None); break;
            case OP_AND:
            case OP_OR: {
                int left_truth = PyObject_IsTrue(left);
                int right_truth = PyObject_IsTrue(right);
                result = left_truth < 0 || right_truth < 0 ? NULL
                    : PyBool_FromLong(op == OP_AND
                        ? left_truth && right_truth : left_truth || right_truth);
                break;
            }
            default: {
                int compare = op == OP_EQ ? Py_EQ : op == OP_NE ? Py_NE
                    : op == OP_LT ? Py_LT : op == OP_LE ? Py_LE
                    : op == OP_GT ? Py_GT : Py_GE;
                result = PyObject_RichCompare(left, right, compare);
                break;
            }
            }
            Py_DECREF(left);
            Py_DECREF(right);
            if (result == NULL) {
                goto python_error;
            }
            stack[stack_count++] = result;
            break;
        }
        case OP_NEG:
        case OP_NOT: {
            PyObject *value;
            PyObject *result;
            int truth;
            if (stack_count == 0) {
                fputs("Ошибка VM: недостаточно значений для операции\n", stderr);
                goto done;
            }
            value = stack[--stack_count];
            if (opcode == OP_NEG) {
                result = PyNumber_Negative(value);
            } else {
                truth = PyObject_IsTrue(value);
                result = truth < 0 ? NULL : PyBool_FromLong(!truth);
            }
            Py_DECREF(value);
            if (result == NULL) {
                goto python_error;
            }
            stack[stack_count++] = result;
            break;
        }
        case OP_JUMP:
        case OP_JUMP_IF_FALSE: {
            uint32_t raw_offset;
            int32_t offset;
            if (!read_u32(chunk, &ip, &raw_offset)) {
                fputs("Ошибка VM: повреждён адрес перехода\n", stderr);
                goto done;
            }
            offset = (int32_t)raw_offset;
            if (opcode == OP_JUMP_IF_FALSE) {
                int truth;
                PyObject *condition;
                if (stack_count == 0) {
                    fputs("Ошибка VM: нет условия для перехода\n", stderr);
                    goto done;
                }
                condition = stack[--stack_count];
                truth = PyObject_IsTrue(condition);
                Py_DECREF(condition);
                if (truth < 0) {
                    goto python_error;
                }
                if (truth) break;
            }
            if ((offset < 0 && (size_t)(-(int64_t)offset) > ip)
                || (offset > 0 && (size_t)offset > chunk->code_count - ip)) {
                fputs("Ошибка VM: адрес перехода вне байткода\n", stderr);
                goto done;
            }
            ip = (size_t)((int64_t)ip + offset);
            break;
        }
        case OP_CALL: {
            uint32_t function_index;
            uint32_t argc;
            const ChunkFunction *function;
            CallFrame *frame;
            size_t base;
            size_t arg;
            if (!read_u32(chunk, &ip, &function_index)
                || !read_u32(chunk, &ip, &argc)
                || function_index >= chunk->function_count) {
                fputs("Ошибка VM: некорректный вызов функции\n", stderr);
                goto done;
            }
            function = &chunk->functions[function_index];
            if (argc != function->parameter_count || stack_count < argc
                || frame_count == VM_FRAME_MAX) {
                fputs("Ошибка VM: неверное число аргументов или переполнение стека вызовов\n", stderr);
                goto done;
            }
            frame = &frames[frame_count];
            frame->locals = calloc(chunk->name_count == 0 ? 1 : chunk->name_count,
                                   sizeof(*frame->locals));
            if (frame->locals == NULL) goto done;
            frame->return_ip = ip;
            base = stack_count - argc;
            frame->stack_base = base;
            for (arg = 0; arg < argc; ++arg) {
                frame->locals[function->parameters[arg]] = stack[base + arg];
                stack[base + arg] = NULL;
            }
            stack_count = base;
            ++frame_count;
            ip = function->address;
            break;
        }
        case OP_CALL_BUILTIN: {
            uint32_t builtin, argc, arg;
            size_t base;
            PyObject *result;
            if (!read_u32(chunk, &ip, &builtin) || !read_u32(chunk, &ip, &argc)
                || stack_count < argc) {
                fputs("Ошибка VM: некорректный вызов встроенной функции\n", stderr);
                goto done;
            }
            base = stack_count - argc;
            result = call_builtin(builtin, stack + base, argc,
                                  program_argc, program_argv);
            for (arg = 0; arg < argc; ++arg) {
                Py_DECREF(stack[base + arg]);
                stack[base + arg] = NULL;
            }
            stack_count = base;
            if (result == NULL) goto python_error;
            stack[stack_count++] = result;
            break;
        }
        case OP_TRY_BEGIN: {
            uint32_t raw_offset;
            int32_t offset;
            size_t target;
            if (!read_u32(chunk, &ip, &raw_offset) || handler_count == VM_HANDLER_MAX) {
                fputs("Ошибка VM: некорректный или слишком глубокий блок обработки ошибки\n",
                      stderr);
                goto done;
            }
            offset = (int32_t)raw_offset;
            if ((offset < 0 && (size_t)(-(int64_t)offset) > ip)
                || (offset > 0 && (size_t)offset > chunk->code_count - ip)) {
                fputs("Ошибка VM: адрес обработчика вне байткода\n", stderr);
                goto done;
            }
            target = (size_t)((int64_t)ip + offset);
            handlers[handler_count].target_ip = target;
            handlers[handler_count].stack_count = stack_count;
            handlers[handler_count].frame_count = frame_count;
            ++handler_count;
            break;
        }
        case OP_TRY_END:
            if (handler_count == 0) {
                fputs("Ошибка VM: завершение отсутствующего блока обработки\n", stderr);
                goto done;
            }
            --handler_count;
            break;
        case OP_RETURN: {
            PyObject *value;
            CallFrame *frame;
            if (frame_count == 0 || stack_count == 0) {
                fputs("Ошибка VM: return вне функции или без значения\n", stderr);
                goto done;
            }
            value = stack[--stack_count];
            frame = &frames[--frame_count];
            for (i = 0; i < chunk->name_count; ++i) Py_XDECREF(frame->locals[i]);
            free(frame->locals);
            stack_count = frame->stack_base;
            if (stack_count == VM_STACK_MAX) {
                Py_DECREF(value);
                fputs("Ошибка VM: стек переполнен после возврата\n", stderr);
                goto done;
            }
            stack[stack_count++] = value;
            ip = frame->return_ip;
            break;
        }
        case OP_MAKE_LIST:
        case OP_MAKE_DICT: {
            uint32_t count;
            PyObject *container;
            uint32_t item;
            if (!read_u32(chunk, &ip, &count) || stack_count < (size_t)count
                * (opcode == OP_MAKE_DICT ? 2u : 1u)) {
                fputs("Ошибка VM: недостаточно элементов для контейнера\n", stderr);
                goto done;
            }
            if (opcode == OP_MAKE_LIST) {
                container = PyList_New((Py_ssize_t)count);
                if (container == NULL) goto python_error;
                for (item = count; item > 0; --item) {
                    PyObject *value = stack[--stack_count];
                    stack[stack_count] = NULL;
                    PyList_SET_ITEM(container, (Py_ssize_t)(item - 1), value);
                }
            } else {
                size_t base = stack_count - (size_t)count * 2;
                container = PyDict_New();
                if (container == NULL) goto python_error;
                for (item = 0; item < count; ++item) {
                    PyObject *key = stack[base + (size_t)item * 2];
                    PyObject *value = stack[base + (size_t)item * 2 + 1];
                    if (PyDict_SetItem(container, key, value) < 0) {
                        uint32_t j;
                        for (j = item; j < count; ++j) {
                            Py_DECREF(stack[base + (size_t)j * 2]);
                            Py_DECREF(stack[base + (size_t)j * 2 + 1]);
                            stack[base + (size_t)j * 2] = NULL;
                            stack[base + (size_t)j * 2 + 1] = NULL;
                        }
                        stack_count = base;
                        Py_DECREF(container);
                        goto python_error;
                    }
                    Py_DECREF(key); Py_DECREF(value);
                    stack[base + (size_t)item * 2] = NULL;
                    stack[base + (size_t)item * 2 + 1] = NULL;
                }
                stack_count = base;
            }
            if (stack_count == VM_STACK_MAX) { Py_DECREF(container); goto done; }
            stack[stack_count++] = container;
            break;
        }
        case OP_INDEX_GET: {
            PyObject *target;
            PyObject *key;
            PyObject *value;
            if (stack_count < 2) goto stack_error;
            key = stack[--stack_count];
            target = stack[--stack_count];
            value = PyObject_GetItem(target, key);
            Py_DECREF(target); Py_DECREF(key);
            if (value == NULL) goto python_error;
            stack[stack_count++] = value;
            break;
        }
        case OP_INDEX_SET: {
            PyObject *value;
            PyObject *key;
            PyObject *target;
            int status;
            if (stack_count < 3) goto stack_error;
            value = stack[--stack_count];
            key = stack[--stack_count];
            target = stack[--stack_count];
            status = PyObject_SetItem(target, key, value);
            Py_DECREF(target); Py_DECREF(key); Py_DECREF(value);
            if (status < 0) goto python_error;
            break;
        }
        case OP_GET_ITER:
            if (stack_count == 0) goto stack_error;
            {
                PyObject *iterable = stack[--stack_count];
                PyObject *iterator = PyObject_GetIter(iterable);
                Py_DECREF(iterable);
                if (iterator == NULL) goto python_error;
                stack[stack_count++] = iterator;
            }
            break;
        case OP_ITER_NEXT: {
            uint32_t raw_offset;
            int32_t offset;
            PyObject *value;
            if (stack_count == 0 || !read_u32(chunk, &ip, &raw_offset)) goto stack_error;
            offset = (int32_t)raw_offset;
            value = PyIter_Next(stack[stack_count - 1]);
            if (value != NULL) {
                if (stack_count == VM_STACK_MAX) { Py_DECREF(value); goto done; }
                stack[stack_count++] = value;
                break;
            }
            if (PyErr_Occurred()) goto python_error;
            Py_DECREF(stack[--stack_count]);
            if ((offset < 0 && (size_t)(-(int64_t)offset) > ip)
                || (offset > 0 && (size_t)offset > chunk->code_count - ip)) goto done;
            ip = (size_t)((int64_t)ip + offset);
            break;
        }
        case OP_POP:
            if (stack_count == 0) goto stack_error;
            Py_DECREF(stack[--stack_count]);
            break;
        case OP_ASK: {
            PyObject *input;
            if (stack_count == VM_STACK_MAX) goto stack_error;
            input = read_input_line();
            if (input == NULL) goto python_error;
            stack[stack_count++] = input;
            break;
        }
        case OP_HALT:
            success = 1;
            goto done;
        default:
            fprintf(stderr, "Ошибка VM: неизвестная инструкция %u\n", opcode);
            goto done;
        }
    }
    fputs("Ошибка VM: отсутствует HALT\n", stderr);
    goto done;

stack_error:
    fputs("Ошибка VM: недостаточно значений на стеке\n", stderr);
    goto done;
python_error:
    if (handler_count > 0) {
        ExceptionHandler handler = handlers[--handler_count];
        PyObject *type = NULL, *value = NULL, *traceback = NULL, *message;
        PyErr_Fetch(&type, &value, &traceback);
        PyErr_NormalizeException(&type, &value, &traceback);
        message = value != NULL ? PyObject_Str(value) : NULL;
        Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(traceback);
        if (message == NULL) {
            PyErr_Clear();
            message = PyUnicode_FromString("ошибка выполнения");
        }
        if (message == NULL || handler.stack_count >= VM_STACK_MAX) {
            Py_XDECREF(message);
            goto done;
        }
        while (stack_count > handler.stack_count) Py_DECREF(stack[--stack_count]);
        while (frame_count > handler.frame_count) {
            CallFrame *frame = &frames[--frame_count];
            for (i = 0; i < chunk->name_count; ++i) Py_XDECREF(frame->locals[i]);
            free(frame->locals);
        }
        stack[stack_count++] = message;
        ip = handler.target_ip;
        goto dispatch;
    }
    report_python_error(chunk, instruction_ip, source_path);
    goto done;

done:
    while (stack_count > 0) Py_DECREF(stack[--stack_count]);
    while (frame_count > 0) {
        CallFrame *frame = &frames[--frame_count];
        for (i = 0; i < chunk->name_count; ++i) Py_XDECREF(frame->locals[i]);
        free(frame->locals);
    }
    for (i = 0; i < chunk->name_count; ++i) Py_XDECREF(globals[i]);
    free(globals);
    if (previous_sigint != SIG_ERR) signal(SIGINT, previous_sigint);
    return success;
}
