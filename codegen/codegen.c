#define USFM_IMPLEMENTATION
#define USFM_CODEGEN_BOOTSTRAP
#define _POSIX_C_SOURCE 200809L
#include "../usfm.h"
#include "markers.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TEMP_BUFFER_SIZE 128

typedef enum {
    START,
    END,
} CodeGen_CommentType;

typedef struct {
    char *name;
    size_t length;
    size_t index_start;
    size_t index_end;
    bool is_data;
    CodeGen_CommentType type;
} CodeGen_Comment;

typedef struct {
    char *name;
    size_t name_length;
    size_t start_comment_index_start;
    size_t start_comment_index_end;
    size_t end_comment_index_start;
    size_t end_comment_index_end;
    uint64_t content_hash;
    bool is_data;
} CodeGen_CommentBlock;

typedef enum {
    NOT_CODEGEN,
    INVALID_CODEGEN,
    CODEGEN,
} CodeGen_ParseResult;

typedef struct {
    char *buffer;
    size_t length;
    size_t capacity;
} CodeGen_WriteBuffer;

uint64_t CodeGen_ContentHash_Calculate(char *input, size_t start, size_t end);
void CodeGen_GenerationComments_Serialize(CodeGen_WriteBuffer *buffer, uint64_t content_hash);

void CodeGen_WriteBuffer_Append(CodeGen_WriteBuffer *buffer, const char *text, size_t length)
{
    assert(buffer->length + length <= buffer->capacity);
    memcpy(&buffer->buffer[buffer->length], text, length);
    buffer->length += length;
}

void CodeGen_WriteBuffer_Appendf(CodeGen_WriteBuffer *buffer, const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    int length = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    assert(length >= 0 && (size_t)length < sizeof line);
    CodeGen_WriteBuffer_Append(buffer, line, (size_t)length);
}

bool CodeGen_Block_Finish(CodeGen_WriteBuffer *buffer, CodeGen_WriteBuffer *content,
    uint64_t current_hash, const char *name)
{
    uint64_t hash = CodeGen_ContentHash_Calculate(content->buffer, 0, content->length);
    bool unchanged = hash == current_hash;
    if (!unchanged)
    {
        printf("Writing new version of %s\n", name);
        CodeGen_GenerationComments_Serialize(buffer, hash);
        CodeGen_WriteBuffer_Append(buffer, content->buffer, content->length);
    }
    free(content->buffer);
    return unchanged;
}

CodeGen_WriteBuffer CodeGen_WriteBuffer_Temp(size_t capacity)
{
    CodeGen_WriteBuffer temp = {
        .buffer = malloc(capacity),
        .length = 0,
        .capacity = capacity,
    };
    assert(temp.buffer != NULL);
    return temp;
}

CodeGen_ParseResult CodeGen_Comment_Parse(char *input, size_t *index,
    size_t line_count, size_t index_at_line_start, CodeGen_CommentType expected_type, CodeGen_Comment *comment)
{
    assert(*index == index_at_line_start);
    comment->index_start = *index;
    const char *codegen_comment_start = "// USFM_CODEGEN ";
    const char *codegen_data_comment_start = "// USFM_CODEGEN_DATA ";
    if (memcmp(&input[*index], codegen_comment_start, strlen(codegen_comment_start)) == 0)
    {
        *index += strlen(codegen_comment_start);
        comment->name = &input[*index];
        comment->is_data = false;
    }
    else if (memcmp(&input[*index], codegen_data_comment_start, strlen(codegen_data_comment_start)) == 0)
    {
        *index += strlen(codegen_data_comment_start);
        comment->name = &input[*index];
        comment->is_data = true;
    }
    else
    {
        return NOT_CODEGEN;
    }

    char c = input[*index];
    while (c != ' ')
    {
        (*index)++;
        comment->length++;
        c = input[*index];
    }
    if (comment->length == 0)
    {
        printf("CodeGen Error: Expected a codegen name after \"%s\" at %ld:%ld\n",
            comment->is_data ? codegen_data_comment_start : codegen_comment_start,
            line_count, (*index-index_at_line_start)+1);
        return INVALID_CODEGEN;
    }
    if (input[*index] != ' ')
    {
        return INVALID_CODEGEN;
        printf("CodeGen Error: Expected ' ' after \"%*.*s\" at %ld:%ld\n",
            (int)comment->length, (int)comment->length, comment->name,
            line_count, (*index-index_at_line_start)+1);
    }
    (*index)++;
    const char *start = "start";
    const char *end = "end";
    if (memcmp(&input[*index], start, strlen(start)) == 0)
    {
        comment->type = START;
    }
    else if (memcmp(&input[*index], end, strlen(end)) == 0)
    {
        comment->type = END;
    }
    else
    {
        printf("CodeGen Error: Expected \"%s\" or \"%s\" after \"%*.*s\" at %ld:%ld\n",
            start, end, (int)comment->length, (int)comment->length, comment->name,
            line_count, (*index-index_at_line_start)+1);
        return INVALID_CODEGEN;
    }
    if (comment->type != expected_type)
    {
        printf("CodeGen Error: Expected type \"%s\" but received \"%s\" at %ld:%ld\n",
            expected_type == START ? start : end, comment->type == START ? start : end,
            line_count, (*index-index_at_line_start)+1);
        return INVALID_CODEGEN;
    }
    *index += comment->type == START ? strlen(start) : strlen(end);
    if (input[*index] != '\n')
    {
        printf("CodeGen Error: Expected \\n after \"%s\" at %ld:%ld\n",
            comment->type == START ? start : end,
            line_count, (*index-index_at_line_start)+1);
        return INVALID_CODEGEN;
    }
    comment->index_end = *index + 1;
    return CODEGEN;
}

uint64_t CodeGen_ContentHash_Calculate(char *input, size_t start, size_t end)
{
    // NOTE(mattg): Based on the djb2 hash algorithm
    assert(input != NULL);
    assert (start <= end);
    uint64_t hash = 5381;
    for (size_t i = start; i < end; i++)
    {
        hash = ((hash << 5) + hash) + input[i];
    }
    return hash;
}

void CodeGen_CommentBlock_GetHash(char *input, CodeGen_CommentBlock *block)
{
    assert(input != NULL);
    assert(block != NULL);
    assert(!block->is_data);
    const char *multiline_comment_start = "/*";
    const char *multiline_comment_end = "*/";
    size_t start = block->start_comment_index_end;
    size_t end = block->end_comment_index_start;
    if (memcmp(&input[start], multiline_comment_start, strlen(multiline_comment_start)) == 0)
    {
        start += strlen(multiline_comment_start);
        // NOTE(mattg): advance "start" until after multiline comment
        while (start < end)
        {
            if (input[start] == '*' &&
                memcmp(&input[start], multiline_comment_end, strlen(multiline_comment_end)) == 0)
            {
                start += strlen(multiline_comment_end) + 1;
                break;
            }
            else
            {
                start++;
            }
        }
    }
    block->content_hash = CodeGen_ContentHash_Calculate(input, start, end);
}

void CodeGen_GenerationComments_Serialize(CodeGen_WriteBuffer *buffer, uint64_t content_hash)
{
    const char *start = "/* USFM_CODEGEN_COMMENT --------------------------------------------------- *\n";
    const char *note  = " * NOTE: This code is generated via codegen. Please do not modify manually!\n";
    const char *date  = " * Last generated: %F %T %Z\n";
    const char *hash  = " * Hash: %x\n";
    const char *end   = " * USFM_CODEGEN_COMMENT --------------------------------------------------- */\n";
    CodeGen_WriteBuffer_Append(buffer, start, strlen(start));
    CodeGen_WriteBuffer_Append(buffer, note, strlen(note));

    char temp_time_buffer[TEMP_BUFFER_SIZE] = {0};
    time_t raw_time = time(NULL);
    struct tm time_info = {0};
    gmtime_r(&raw_time, &time_info);
    size_t time_len = strftime(temp_time_buffer, TEMP_BUFFER_SIZE, date, &time_info);
    assert(time_len <= TEMP_BUFFER_SIZE);
    CodeGen_WriteBuffer_Append(buffer, (const char *)temp_time_buffer, time_len);

    char temp_hash_buffer[TEMP_BUFFER_SIZE] = {0};
    size_t hash_len = snprintf(temp_hash_buffer, TEMP_BUFFER_SIZE, hash, content_hash);
    assert(hash_len <= TEMP_BUFFER_SIZE);
    CodeGen_WriteBuffer_Append(buffer, (const char *)temp_hash_buffer, hash_len);
    CodeGen_WriteBuffer_Append(buffer, end, strlen(end));
}

void CodeGen_ByteClass_Generate(uint8_t classes[256])
{
    memset(classes, 0, 256);
    const char *stops = "\\|/~\r\n\t";
    for (size_t i = 0; stops[i] != '\0'; i++)
    {
        classes[(uint8_t)stops[i]] |= USFM_BC_STOP;
    }
    for (int c = 'A'; c <= 'Z'; c++)
    {
        classes[c] |= USFM_BC_NAME | USFM_BC_ALPHA;
    }
    for (int c = 'a'; c <= 'z'; c++)
    {
        classes[c] |= USFM_BC_NAME | USFM_BC_ALPHA;
    }
    for (int c = '0'; c <= '9'; c++)
    {
        classes[c] |= USFM_BC_NAME | USFM_BC_DIGIT;
    }
    classes['-'] |= USFM_BC_NAME;
    classes[' '] |= USFM_BC_SPACE;
    classes['\t'] |= USFM_BC_SPACE;
    classes['\r'] |= USFM_BC_NEWLINE;
    classes['\n'] |= USFM_BC_NEWLINE;
    const char *escapes = "\\/|~\"";
    for (size_t i = 0; escapes[i] != '\0'; i++)
    {
        classes[(uint8_t)escapes[i]] |= USFM_BC_ESCAPE;
    }
}

bool CodeGen_ByteClass_Serialize(const uint8_t classes[256], CodeGen_WriteBuffer *buffer,
    uint64_t current_hash)
{
    CodeGen_WriteBuffer temp = CodeGen_WriteBuffer_Temp(buffer->capacity);
    CodeGen_WriteBuffer_Appendf(&temp, "static const uint8_t USFM_ByteClass[256] = {\n");
    for (size_t i = 0; i < 256; i += 16)
    {
        CodeGen_WriteBuffer_Appendf(&temp, "   ");
        for (size_t j = i; j < i + 16; j++)
        {
            CodeGen_WriteBuffer_Appendf(&temp, " 0x%02x,", classes[j]);
        }
        CodeGen_WriteBuffer_Appendf(&temp, "\n");
    }
    CodeGen_WriteBuffer_Appendf(&temp, "};\n");
    return CodeGen_Block_Finish(buffer, &temp, current_hash, "character_class");
}

/* Seeds tried per table size, and the largest size tried (as a multiple
 * of the key count). Both bound the search so codegen always ends. */
#define CODEGEN_HASH_SEED_LIMIT 65536u
#define CODEGEN_HASH_SIZE_FACTOR 64u

typedef struct {
    uint32_t seed;
    uint32_t slot_count;
    size_t name_max;
    uint16_t *slots;        /* slot -> row index + 1; 0 = empty */
} CodeGen_PerfectHash;

/* Finds the smallest slot count (and a seed) that puts every key in
 * its own slot. Prints an error and returns false if the bounds are hit
 * or two keys have identical hash input. */
bool CodeGen_PerfectHash_Search(const CodeGen_MarkerDef *defs, size_t count,
    CodeGen_PerfectHash *out)
{
    assert(count > 0 && count < 0xFFFF);
    uint32_t *hashes = malloc(count * sizeof *hashes);
    assert(hashes != NULL);
    size_t name_max = 0;
    for (size_t i = 0; i < count; i++)
    {
        size_t length = strlen(defs[i].name);
        if (length == 0 || length > 255)
        {
            printf("CodeGen Error: marker name \"%s\" must be 1..255 bytes\n", defs[i].name);
            free(hashes);
            return false;
        }
        hashes[i] = usfm__marker_hash(defs[i].name, (uint32_t)length);
        if (length > name_max)
        {
            name_max = length;
        }
        for (size_t j = 0; j < i; j++)
        {
            if (strcmp(defs[i].name, defs[j].name) == 0)
            {
                printf("CodeGen Error: marker \"%s\" is listed twice\n", defs[i].name);
                free(hashes);
                return false;
            }
            if (hashes[i] == hashes[j])
            {
                printf("CodeGen Error: \"%s\" and \"%s\" have identical hash input; "
                    "extend usfm__marker_hash\n", defs[i].name, defs[j].name);
                free(hashes);
                return false;
            }
        }
    }

    uint32_t max_size = (uint32_t)count * CODEGEN_HASH_SIZE_FACTOR;
    uint32_t *stamp = calloc(max_size, sizeof *stamp);
    assert(stamp != NULL);
    uint32_t generation = 0;
    for (uint32_t size = (uint32_t)count; size <= max_size; size++)
    {
        for (uint32_t k = 0; k < CODEGEN_HASH_SEED_LIMIT; k++)
        {
            uint32_t seed = (k + 1u) * 0x9E3779B1u;
            generation++;
            bool collision = false;
            for (size_t i = 0; i < count; i++)
            {
                uint32_t slot = usfm__marker_slot(hashes[i], seed, size);
                if (stamp[slot] == generation)
                {
                    collision = true;
                    break;
                }
                stamp[slot] = generation;
            }
            if (!collision)
            {
                out->seed = seed;
                out->slot_count = size;
                out->name_max = name_max;
                out->slots = calloc(size, sizeof *out->slots);
                assert(out->slots != NULL);
                for (size_t i = 0; i < count; i++)
                {
                    out->slots[usfm__marker_slot(hashes[i], seed, size)] = (uint16_t)(i + 1);
                }
                printf("Perfect hash: %zu keys in %u slots (seed 0x%08x)\n", count, size, seed);
                free(stamp);
                free(hashes);
                return true;
            }
        }
    }
    printf("CodeGen Error: no collision-free hash for %zu markers with up to %u slots "
        "and %u seeds per size\n", count, max_size, CODEGEN_HASH_SEED_LIMIT);
    free(stamp);
    free(hashes);
    return false;
}

bool CodeGen_MarkerTable_Serialize(CodeGen_WriteBuffer *buffer, uint64_t current_hash)
{
    size_t count = sizeof CodeGen_Markers / sizeof CodeGen_Markers[0];
    CodeGen_PerfectHash hash = {0};
    if (!CodeGen_PerfectHash_Search(CodeGen_Markers, count, &hash))
    {
        exit(1);
    }
    CodeGen_WriteBuffer temp = CodeGen_WriteBuffer_Temp(buffer->capacity);
    CodeGen_WriteBuffer_Appendf(&temp, "#define USFM_MARKER_NAME_MAX %zuu\n", hash.name_max);
    CodeGen_WriteBuffer_Appendf(&temp, "#define USFM_MARKER_HASH_SEED 0x%08xu\n", hash.seed);
    CodeGen_WriteBuffer_Appendf(&temp, "#define USFM_MARKER_SLOT_COUNT %uu\n", hash.slot_count);
    CodeGen_WriteBuffer_Appendf(&temp, "static const USFM_MarkerRow USFM_MarkerRows[%zu] = {\n", count);
    for (size_t i = 0; i < count; i++)
    {
        const CodeGen_MarkerDef *def = &CodeGen_Markers[i];
        CodeGen_WriteBuffer_Appendf(&temp, "    { \"%s\", %zu, %s, %u, %s, %s },\n",
            def->name, strlen(def->name), def->role, def->level, def->props, def->marker);
    }
    CodeGen_WriteBuffer_Appendf(&temp, "};\n");
    CodeGen_WriteBuffer_Appendf(&temp, "static const %s USFM_MarkerSlots[%u] = {\n",
        count < 255 ? "uint8_t" : "uint16_t", hash.slot_count);
    for (uint32_t i = 0; i < hash.slot_count; i += 16)
    {
        CodeGen_WriteBuffer_Appendf(&temp, "   ");
        for (uint32_t j = i; j < i + 16 && j < hash.slot_count; j++)
        {
            CodeGen_WriteBuffer_Appendf(&temp, " %u,", hash.slots[j]);
        }
        CodeGen_WriteBuffer_Appendf(&temp, "\n");
    }
    CodeGen_WriteBuffer_Appendf(&temp, "};\n");
    free(hash.slots);
    return CodeGen_Block_Finish(buffer, &temp, current_hash, "marker_table");
}

int main(int argc, char **argv)
{
    uint8_t byte_class[256];
    CodeGen_ByteClass_Generate(byte_class);

    bool overwrite = false;
    const char *overwrite_arg_short = "-o";
    const char *overwrite_arg_long = "--overwrite";
    if (argc > 1)
    {
        for (size_t i = 1; i < (size_t)argc; i++)
        {
            if (memcmp(argv[i], overwrite_arg_short, strlen(overwrite_arg_short)) == 0)
            {
                overwrite = true;
            }
            else if (memcmp(argv[i], overwrite_arg_long, strlen(overwrite_arg_long)) == 0)
            {
                overwrite = true;
            }
        }
    }
    const char *input_path = "../usfm.h";
    const char *output_path = overwrite ? input_path : "../usfm_copy.h";
    printf("Writing to %s\n", output_path);
    int fd = open(input_path, O_RDONLY);
    assert(fd != -1);

    struct stat statbuf = {0};
    assert(fstat(fd, &statbuf) != -1);
    mode_t input_mode = statbuf.st_mode;
    size_t size = (size_t)statbuf.st_size;
    char *readbuf = malloc(size);
    assert(readbuf != NULL);

    size_t write_size = size * 2;
    CodeGen_WriteBuffer writebuf = {
        .buffer = malloc(write_size),
        .capacity = write_size,
    };
    assert(writebuf.buffer != NULL);
    memset(writebuf.buffer, 0, writebuf.capacity);

    assert(read(fd, readbuf, size) != -1);
    close(fd);
    CodeGen_Comment comments[10] = {0};
    size_t comment_capacity = sizeof(comments) / sizeof(comments[0]);
    size_t comment_length = 0;

    CodeGen_CommentType expected_type = START;
    size_t index = 0;
    size_t line_count = 1;
    size_t index_at_line_start = 0;
    bool line_start = true;
    while (index < size)
    {
        char c = readbuf[index];
        if (c == '\n')
        {
            index++;
            line_count++;
            index_at_line_start = index;
            line_start = true;
            continue;
        }
        else if (line_start && c == '/')
        {
            CodeGen_ParseResult result = CodeGen_Comment_Parse(readbuf, &index, line_count, index_at_line_start,
                    expected_type, &comments[comment_length]);
            switch (result)
            {
            case CODEGEN:
                assert(comment_length < comment_capacity);
                expected_type = expected_type == START ? END : START;
                comment_length++;
                break;
            case INVALID_CODEGEN:
                exit(1);
                break;
            case NOT_CODEGEN:
                line_start = false;
                index++;
            }
        }
        else
        {
            line_start = false;
            index++;
        }
    }
    // NOTE(mattg): there should be an even number of comments at all times
    if (comment_length % 2 != 0)
    {
        printf("CodeGen Error: Expected there to be an even number of comments (%ld)\n", comment_length);
        exit(1);
    }
    // NOTE(mattg): ensure pairs are next to each other
    for (size_t i = 0; i < comment_length; i++)
    {
        int modulo = i % 2;
        assert((modulo == 0 && comments[i].type == START) || (modulo == 1 && comments[i].type == END));
        if (comments[i].type == END)
        {
            if (comments[i].is_data != comments[i-1].is_data)
            {
                printf("CodeGen Error: Expected comment pairs to both be data comments\n");
                exit(1);
            }
            // NOTE(mattg): ensure start/end pairs have the same name
            if (comments[i].length != comments[i-1].length ||
                memcmp(comments[i].name, comments[i-1].name, comments[i].length) != 0)
            {
                printf("CodeGen Error: Expected \"%*.*s\" and \"%*.*s\" to equal\n",
                    (int)comments[i-1].length, (int)comments[i-1].length, comments[i-1].name,
                    (int)comments[i].length, (int)comments[i].length, comments[i].name);
                exit(1);
            }
        }
    }
    CodeGen_CommentBlock blocks[5] = {0};
    size_t block_capacity = sizeof(blocks) / sizeof(blocks[0]);
    size_t block_length = 0;

    for (size_t i = 0; i < comment_length; i++)
    {
        CodeGen_CommentBlock block = {0};

        assert(comments[i].type == START);
        CodeGen_Comment start_comment = comments[i];
        block.name = start_comment.name;
        block.name_length = start_comment.length;
        block.start_comment_index_start = start_comment.index_start;
        block.start_comment_index_end = start_comment.index_end;
        block.is_data = start_comment.is_data;

        i++;
        assert(comments[i].type == END);
        CodeGen_Comment end_comment = comments[i];
        block.end_comment_index_start = end_comment.index_start;
        block.end_comment_index_end = end_comment.index_end;

        assert(block_length < block_capacity);
        blocks[block_length] = block;
        block_length++;
    }
    
    for (size_t i = 0; i < block_length; i++)
    {
        if (!blocks[i].is_data)
        {
            CodeGen_CommentBlock_GetHash(readbuf, &blocks[i]);
        }
    }

    size_t copy_section_start = 0;
    size_t copy_section_end = 0;
    if (block_length > 0)
    {
        copy_section_end = blocks[0].start_comment_index_end;
        size_t length = copy_section_end-copy_section_start;
        CodeGen_WriteBuffer_Append(&writebuf, (const char *)&readbuf[copy_section_start], length);
    }
    bool content_changed = false;
    for (size_t i = 0; i < block_length; i++)
    {
        CodeGen_CommentBlock block = blocks[i];
        bool should_copy_content = false;
        if (block.is_data)
        {
            should_copy_content = true;
        }
        else
        {
            const char *cc = "character_class";
            const char *mt = "marker_table";
            bool unchanged = true;
            if (block.name_length == strlen(cc) && memcmp(block.name, cc, strlen(cc)) == 0)
            {
                unchanged = CodeGen_ByteClass_Serialize(byte_class, &writebuf, block.content_hash);
            }
            else if (block.name_length == strlen(mt) && memcmp(block.name, mt, strlen(mt)) == 0)
            {
                unchanged = CodeGen_MarkerTable_Serialize(&writebuf, block.content_hash);
            }
            else
            {
                printf("CodeGen Error: unknown codegen block \"%.*s\"\n",
                    (int)block.name_length, block.name);
                exit(1);
            }
            if (unchanged)
            {
                should_copy_content = true;
            }
            else
            {
                content_changed = true;
            }
        }
        if (should_copy_content)
        {
            size_t start = block.start_comment_index_end;
            size_t end = block.end_comment_index_start;
            size_t length = end-start;
            CodeGen_WriteBuffer_Append(&writebuf, (const char *)&readbuf[start], length);
        }
        // copy current to next, or to end of file
        copy_section_start = block.end_comment_index_start;
        copy_section_end = i < block_length-1 ? blocks[i+1].start_comment_index_end : size;
        size_t length = copy_section_end - copy_section_start;
        CodeGen_WriteBuffer_Append(&writebuf, (const char *)&readbuf[copy_section_start], length);
    }
    if (writebuf.length > 0)
    {
        fd = open(output_path, O_CREAT|O_WRONLY|O_TRUNC, input_mode);
        assert(fd != -1);
        assert(write(fd, writebuf.buffer, writebuf.length) != -1);
        close(fd);
    }
    if (!content_changed)
    {
        printf("No content changed!\n");
    }

    free(readbuf);
    free(writebuf.buffer);
    return 0;
}
