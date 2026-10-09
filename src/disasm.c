#include "disasm.h"

#include "opcodes.h"

#include <stdint.h>

static const char *opcode_name(uint8_t opcode)
{
    static const char *const names[] = {
        "CONST", "LOAD", "STORE", "SAY", "ADD", "SUB", "MUL", "DIV",
        "MOD", "POW", "EQ", "NE", "LT", "LE", "GT", "GE", "AND", "OR",
        "NEG", "NOT", "JUMP", "JUMP_IF_FALSE", "CALL", "RETURN",
        "MAKE_LIST", "MAKE_DICT", "INDEX_GET", "INDEX_SET", "GET_ITER",
        "ITER_NEXT", "POP", "HALT", "ASK", "CALL_BUILTIN", "TRY_BEGIN", "TRY_END"
    };
    return opcode < sizeof(names) / sizeof(names[0]) ? names[opcode] : NULL;
}

static int read_u32(const Chunk *chunk, size_t *offset, uint32_t *value)
{
    if (chunk->code_count - *offset < 4) return 0;
    *value = (uint32_t)chunk->code[*offset]
        | ((uint32_t)chunk->code[*offset + 1] << 8)
        | ((uint32_t)chunk->code[*offset + 2] << 16)
        | ((uint32_t)chunk->code[*offset + 3] << 24);
    *offset += 4;
    return 1;
}

int disasm_dump(const Chunk *chunk, FILE *output)
{
    size_t offset = 0;
    while (offset < chunk->code_count) {
        size_t instruction = offset;
        uint8_t opcode = chunk->code[offset++];
        const char *name = opcode_name(opcode);
        uint32_t first, second;
        if (name == NULL) {
            fprintf(output, "%04lu <invalid %u>\n", (unsigned long)instruction, opcode);
            return 0;
        }
        fprintf(output, "%04lu line %u %-14s", (unsigned long)instruction,
                instruction < chunk->code_count ? chunk->lines[instruction] : 0, name);
        switch (opcode) {
        case OP_CONST:
            if (!read_u32(chunk, &offset, &first) || first >= chunk->constant_count) return 0;
            {
                PyObject *repr = PyObject_Repr(chunk->constants[first]);
                const char *text;
                if (repr == NULL) return 0;
                text = PyUnicode_AsUTF8(repr);
                if (text == NULL) { Py_DECREF(repr); return 0; }
                fprintf(output, "%u = %s", first, text);
                Py_DECREF(repr);
            }
            break;
        case OP_LOAD:
        case OP_STORE:
            if (!read_u32(chunk, &offset, &first) || first >= chunk->name_count) return 0;
            fprintf(output, "%u (%s)", first, chunk->names[first]);
            break;
        case OP_CALL:
        case OP_CALL_BUILTIN:
            if (!read_u32(chunk, &offset, &first) || !read_u32(chunk, &offset, &second)) return 0;
            fprintf(output, "%u argc=%u", first, second);
            break;
        case OP_MAKE_LIST:
        case OP_MAKE_DICT:
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_ITER_NEXT:
        case OP_TRY_BEGIN:
            if (!read_u32(chunk, &offset, &first)) return 0;
            fprintf(output, "%d", (int32_t)first);
            break;
        default:
            break;
        }
        fputc('\n', output);
        if (ferror(output)) return 0;
    }
    return !ferror(output);
}
