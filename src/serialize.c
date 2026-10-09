#include "serialize.h"

#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIMC_VERSION 2u

static const unsigned char simc_magic[8] = { 'S', 'I', 'M', 'P', 'L', 'C', 0, 0 };

typedef struct {
    FILE *file;
    uint64_t remaining;
} Reader;

static int write_bytes(FILE *file, const void *data, size_t size)
{
    return size == 0 || fwrite(data, 1, size, file) == size;
}

static int read_bytes(Reader *reader, void *data, size_t size)
{
    if ((uint64_t)size > reader->remaining
        || fread(data, 1, size, reader->file) != size) return 0;
    reader->remaining -= (uint64_t)size;
    return 1;
}

static int write_u32(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24)
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

static int read_u32(Reader *reader, uint32_t *value)
{
    unsigned char bytes[4];
    if (!read_bytes(reader, bytes, sizeof(bytes))) return 0;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8)
        | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 1;
}

static int write_u64(FILE *file, uint64_t value)
{
    unsigned char bytes[8];
    unsigned int i;
    for (i = 0; i < 8; ++i) bytes[i] = (unsigned char)(value >> (i * 8));
    return write_bytes(file, bytes, sizeof(bytes));
}

static int read_u64(Reader *reader, uint64_t *value)
{
    unsigned char bytes[8];
    unsigned int i;
    uint64_t result = 0;
    if (!read_bytes(reader, bytes, sizeof(bytes))) return 0;
    for (i = 0; i < 8; ++i) result |= (uint64_t)bytes[i] << (i * 8);
    *value = result;
    return 1;
}

static int write_string(FILE *file, const char *value, size_t length)
{
    return length <= UINT32_MAX && write_u32(file, (uint32_t)length)
        && write_bytes(file, value, length);
}

static int save_constant(FILE *file, PyObject *value)
{
    unsigned char tag;
    if (value == Py_None) {
        tag = 0;
        return write_bytes(file, &tag, 1);
    }
    if (PyBool_Check(value)) {
        tag = 3;
        return write_bytes(file, &tag, 1)
            && write_bytes(file, value == Py_True ? "\1" : "\0", 1);
    }
    if (PyLong_Check(value)) {
        long long number = PyLong_AsLongLong(value);
        if (number == -1 && PyErr_Occurred()) {
            PyErr_Clear();
            return 0;
        }
        tag = 1;
        return write_bytes(file, &tag, 1) && write_u64(file, (uint64_t)(int64_t)number);
    }
    if (PyFloat_Check(value)) {
        double number = PyFloat_AsDouble(value);
        uint64_t bits;
        if (sizeof(number) != sizeof(bits) || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024)
            return 0;
        memcpy(&bits, &number, sizeof(bits));
        tag = 2;
        return write_bytes(file, &tag, 1) && write_u64(file, bits);
    }
    if (PyUnicode_Check(value)) {
        Py_ssize_t length;
        const char *utf8 = PyUnicode_AsUTF8AndSize(value, &length);
        tag = 4;
        return utf8 != NULL && length >= 0
            && write_bytes(file, &tag, 1)
            && write_string(file, utf8, (size_t)length);
    }
    return 0;
}

static PyObject *load_constant(Reader *reader)
{
    unsigned char tag;
    if (!read_bytes(reader, &tag, 1)) return NULL;
    if (tag == 0) {
        Py_INCREF(Py_None);
        return Py_None;
    }
    if (tag == 1 || tag == 2) {
        uint64_t bits;
        if (!read_u64(reader, &bits)) return NULL;
        if (tag == 1) return PyLong_FromLongLong((long long)(int64_t)bits);
        {
            double number;
            if (sizeof(number) != sizeof(bits) || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024)
                return NULL;
            memcpy(&number, &bits, sizeof(number));
            return PyFloat_FromDouble(number);
        }
    }
    if (tag == 3) {
        unsigned char boolean;
        if (!read_bytes(reader, &boolean, 1) || boolean > 1) return NULL;
        Py_INCREF(boolean ? Py_True : Py_False);
        return boolean ? Py_True : Py_False;
    }
    if (tag == 4) {
        uint32_t length;
        char *buffer;
        PyObject *value;
        if (!read_u32(reader, &length) || (uint64_t)length > reader->remaining)
            return NULL;
#if SIZE_MAX <= UINT32_MAX
        if (length == UINT32_MAX) return NULL;
#endif
        buffer = malloc((size_t)length + 1);
        if (buffer == NULL) return NULL;
        if (!read_bytes(reader, buffer, length)) {
            free(buffer);
            return NULL;
        }
        value = PyUnicode_DecodeUTF8(buffer, length, "strict");
        free(buffer);
        return value;
    }
    return NULL;
}

int serialize_write(const Chunk *chunk, FILE *file)
{
    size_t i, j;
    if (chunk->constant_count > UINT32_MAX || chunk->name_count > UINT32_MAX
        || chunk->code_count > UINT32_MAX || chunk->function_count > UINT32_MAX)
        return 0;
    if (!write_bytes(file, simc_magic, sizeof(simc_magic))
        || !write_u32(file, SIMC_VERSION)
        || !write_u32(file, (uint32_t)chunk->constant_count)) return 0;
    for (i = 0; i < chunk->constant_count; ++i)
        if (!save_constant(file, chunk->constants[i])) return 0;
    if (!write_u32(file, (uint32_t)chunk->name_count)) return 0;
    for (i = 0; i < chunk->name_count; ++i)
        if (!write_string(file, chunk->names[i], strlen(chunk->names[i]))) return 0;
    if (!write_u32(file, (uint32_t)chunk->code_count)
        || !write_bytes(file, chunk->code, chunk->code_count)
        || !write_u32(file, (uint32_t)chunk->code_count)) return 0;
    for (i = 0; i < chunk->code_count; ++i)
        if (!write_u32(file, chunk->lines[i])) return 0;
    if (!write_u32(file, (uint32_t)chunk->function_count)) return 0;
    for (i = 0; i < chunk->function_count; ++i) {
        const ChunkFunction *function = &chunk->functions[i];
        if (function->parameter_count > UINT32_MAX || function->address > UINT32_MAX
            || !write_u32(file, function->name_index)
            || !write_u32(file, (uint32_t)function->parameter_count)) return 0;
        for (j = 0; j < function->parameter_count; ++j)
            if (!write_u32(file, function->parameters[j])) return 0;
        if (!write_u32(file, (uint32_t)function->address)) return 0;
    }
    return fflush(file) == 0 && !ferror(file);
}

int serialize_save(const Chunk *chunk, const char *path)
{
    FILE *file = fopen(path, "wb");
    int ok;
    if (file == NULL) return 0;
    ok = serialize_write(chunk, file);
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int read_string(Reader *reader, char **value)
{
    uint32_t length;
    char *buffer;
    if (!read_u32(reader, &length) || (uint64_t)length > reader->remaining)
        return 0;
#if SIZE_MAX <= UINT32_MAX
    if (length == UINT32_MAX) return 0;
#endif
    buffer = malloc((size_t)length + 1);
    if (buffer == NULL) return 0;
    if (!read_bytes(reader, buffer, length) || memchr(buffer, '\0', length) != NULL) {
        free(buffer);
        return 0;
    }
    buffer[length] = '\0';
    *value = buffer;
    return 1;
}

int serialize_load(Chunk *chunk, const char *path)
{
    FILE *file;
    long file_size;
    Reader reader;
    unsigned char magic[8];
    uint32_t version, count, i, j;
    Chunk loaded;
    int ok = 0;
    chunk_init_empty(&loaded);
    file = fopen(path, "rb");
    if (file == NULL) return 0;
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0
        || fseek(file, 0, SEEK_SET) != 0) goto done;
    reader.file = file;
    reader.remaining = (uint64_t)file_size;
    if (!read_bytes(&reader, magic, sizeof(magic))
        || memcmp(magic, simc_magic, sizeof(magic)) != 0
        || !read_u32(&reader, &version) || version != SIMC_VERSION
        || !read_u32(&reader, &count)) goto done;
    for (i = 0; i < count; ++i) {
        PyObject *value = load_constant(&reader);
        uint32_t index;
        if (value == NULL) goto done;
        if (!chunk_add_constant(&loaded, value, &index)) {
            Py_DECREF(value);
            goto done;
        }
        Py_DECREF(value);
    }
    if (!read_u32(&reader, &count)) goto done;
    for (i = 0; i < count; ++i) {
        char *name;
        uint32_t index;
        if (!read_string(&reader, &name)) goto done;
        if (!chunk_intern_name(&loaded, name, &index) || index != i) {
            free(name);
            goto done;
        }
        free(name);
    }
    if (!read_u32(&reader, &count) || (uint64_t)count > reader.remaining) goto done;
    for (i = 0; i < count; ++i) {
        unsigned char byte;
        if (!read_bytes(&reader, &byte, 1) || !chunk_emit(&loaded, byte)) goto done;
    }
    if (!read_u32(&reader, &count) || count != loaded.code_count
        || (uint64_t)count * 4 > reader.remaining) goto done;
    for (i = 0; i < count; ++i) {
        if (!read_u32(&reader, &loaded.lines[i])) goto done;
    }
    if (!read_u32(&reader, &count)) goto done;
    for (i = 0; i < count; ++i) {
        uint32_t name_index, parameter_count, address, function_index;
        uint32_t *parameters = NULL;
        if (!read_u32(&reader, &name_index) || name_index >= loaded.name_count
            || !read_u32(&reader, &parameter_count)
            || (uint64_t)parameter_count * 4 > reader.remaining) goto done;
        if (parameter_count != 0) {
#if SIZE_MAX / UINT32_MAX < 4
            if (parameter_count > SIZE_MAX / sizeof(*parameters)) goto done;
#endif
            parameters = malloc((size_t)parameter_count * sizeof(*parameters));
            if (parameters == NULL) goto done;
        }
        for (j = 0; j < parameter_count; ++j) {
            if (!read_u32(&reader, &parameters[j]) || parameters[j] >= loaded.name_count) {
                free(parameters);
                goto done;
            }
        }
        if (!read_u32(&reader, &address) || address >= loaded.code_count
            || !chunk_add_function(&loaded, name_index, parameters, parameter_count,
                                   &function_index)) {
            free(parameters);
            goto done;
        }
        free(parameters);
        loaded.functions[function_index].address = address;
    }
    if (reader.remaining != 0) goto done;
    ok = 1;
done:
    if (fclose(file) != 0) ok = 0;
    if (ok) {
        chunk_free(chunk);
        *chunk = loaded;
    } else {
        chunk_free(&loaded);
    }
    return ok;
}
