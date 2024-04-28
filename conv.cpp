#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include "codes.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

#pragma pack(push, 1)

struct Header
{
    u8 version;
    u8 options_a;
    u16 chord_count;
    u16 sleep_timeout;
    u16 mouse_left_action;
    u16 mouse_middle_action;
    u16 mouse_right_action;
    u8 mouse_acceleration;
    u8 key_repeat_delay;
    u8 options_b;
    u8 options_c;
};

struct ChordTableEntry
{
    u16 buttons;
    u8 hidModifiers;
    u8 hidCode;
};

#pragma pack(pop)

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

struct Arena
{
    void *base;
    void *next;
    size_t size;
};

#define KiloBytes(n) (1024 * (n))
#define MegaBytes(n) (1024 * KiloBytes((n)))
#define GigaBytes(n) (1024 * MegaBytes((n)))

#define MAIN_ARENA_BASE (void *)0x1000000

#define MAX_CHORD_COUNT 1020

inline void
initArena(Arena *arena, size_t size, void *addressHint = 0)
{
    *arena = {};
    void *base = mmap(addressHint, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (base == MAP_FAILED)
    {
        fprintf(stderr, "Could not allocate memory\n");
        exit(1);
    }
    else
    {
        arena->base = base;
        arena->next = base;
        arena->size = size;
    }
}

size_t bytesAvailable(Arena *arena)
{
    size_t used = (u8 *)(arena->next) - (u8 *)(arena->base);
    return arena->size - used;
}

void takeBytes(Arena *arena, size_t bytes)
{
    arena->next = (void *)((u8 *)arena->next + bytes);
}

#define ERROR(...)                \
    fprintf(stderr, __VA_ARGS__); \
    exit(1);

struct ReadFileResult
{
    enum Outcome
    {
        Uninitialized,
        Success,
        Unreadable,
        Partial
    };
    Outcome outcome;
    u8 *data;
    u64 length;
};

ReadFileResult
readFile(Arena *arena, const char *fileName)
{
    ReadFileResult result = {};

    FILE *fp = fopen(fileName, "r");
    if (!fp)
    {
        result = {ReadFileResult::Unreadable};
    }
    else
    {
        u8 *fileBytes = (u8 *)arena->next;
        u64 length = fread(fileBytes, sizeof(*fileBytes), bytesAvailable(arena), fp);

        if (!feof(fp))
        {
            result = {ReadFileResult::Partial, 0, 0};
        }
        else
        {
            takeBytes(arena, length);
            result = {ReadFileResult::Success, fileBytes, length};
        }
    }
    if (fp)
    {
        fclose(fp);
    }
    return result;
}

struct TwiddlerConfig
{
    enum Outcome
    {
        Uninitialized,
        Success,
        IncompleteHeader,
        ChordCountTooHigh,
    };
    Outcome outcome;
    Header *header;
    ChordTableEntry *chordTable;
};

TwiddlerConfig parseTwiddlerConfigV5Bytes(Arena *arena, u8 *sourceBytes, u64 length, char *fileName)
{
    TwiddlerConfig result = {};
    if (length < sizeof(Header))
    {
        result = {TwiddlerConfig::Outcome::IncompleteHeader};
    }
    else
    {
        Header *header = (Header *)sourceBytes;

        if (header->chord_count > MAX_CHORD_COUNT)
        {
            result = {TwiddlerConfig::Outcome::ChordCountTooHigh};
        }
        else
        {
            ChordTableEntry *chordTable = (ChordTableEntry *)(sourceBytes + sizeof(Header));
            for (u32 chordIndex = 0;
                 chordIndex < header->chord_count;
                 ++chordIndex)
            {
                ChordTableEntry *chord = chordTable + chordIndex;
                printf("chord buttons=%04x code=0x%02x mod=%02x\n", chord->buttons, chord->hidCode, chord->hidModifiers);
            }
        }
    }
    return result;
}

int main(int argc, char **argv)
{
    Arena arena;
    initArena(&arena, MegaBytes(1), MAIN_ARENA_BASE);
    char *fileName = argv[1];

    ReadFileResult confFile = readFile(&arena, fileName);
    if (confFile.outcome != ReadFileResult::Success)
    {
        ERROR("Could not read file \"%s\"\n", fileName)
    }
    else
    {
        parseTwiddlerConfigV5Bytes(&arena, confFile.data, confFile.length, fileName);
    }
}
