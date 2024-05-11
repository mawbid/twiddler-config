#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <cassert>

#include "codes.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

inline u32 readLEu32(u8 *bytes)
{
    return bytes[0] << 0 |
           bytes[1] << 8 |
           bytes[2] << 16 |
           bytes[3] << 24;
}

inline u32 readLEu16(u8 *bytes)
{
    return bytes[0] << 0 |
           bytes[1] << 8;
}

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

struct HidPair
{
    u8 modifiers;
    u8 code;
};

struct ChordTableEntry
{
    u16 buttons;
    HidPair hid;
};

struct StringTableEntry
{
    u16 length;
    HidPair elements[];
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
#define MAX_STRING_COUNT 256
#define MAX_STRING_LENGTH 256

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

size_t
bytesAvailable(Arena *arena)
{
    size_t used = (u8 *)(arena->next) - (u8 *)(arena->base);
    return arena->size - used;
}

void *
takeBytes(Arena *arena, size_t bytes)
{
    void *result;
    if (bytes > bytesAvailable(arena))
    {
        assert(!"Arena exhausted");
    }
    else
    {
        result = arena->next;
        arena->next = (void *)((u8 *)arena->next + bytes);
    }
    return result;
}

void *
requestBytes(Arena *arena, size_t bytes)
{
    void *result = 0;
    if (bytes <= bytesAvailable(arena))
    {
        result = arena->next;
        arena->next = (void *)((u8 *)arena->next + bytes);
    }
    return result;
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

struct Chord
{
    u16 buttons;
    u32 codeCount;
    HidPair codes[MAX_STRING_LENGTH];
};

struct TwiddlerConfig
{
    enum Outcome
    {
        Uninitialized,
        Success,
        ArenaFull,
        IncompleteHeader,
        ChordCountTooHigh,
        TooManyStrings,
        DeclaredStringLengthOverrunsBuffer,
        StringTooLong,
    };
    Outcome outcome;
    u32 stringCount;
    Header *header;
    Chord *chords;
};

TwiddlerConfig
parseTwiddlerConfigV5Bytes(Arena *arena, u8 *sourceBytes, u64 length, char *fileName)
{
    TwiddlerConfig result = {};
    if (length < sizeof(Header))
    {
        result = {TwiddlerConfig::IncompleteHeader};
    }
    else
    {
        Header *rawHeader = (Header *)sourceBytes;

        result.header = (Header *)requestBytes(arena, sizeof(Header));
        result.chords = (Chord *)requestBytes(arena, sizeof(Chord) * MAX_CHORD_COUNT);
        if (!result.header || !result.chords)
        {
            result = {TwiddlerConfig::ArenaFull};
        }
        else
        {
            *result.header = *rawHeader;
            result.header->chord_count = readLEu16((u8 *)&rawHeader->chord_count);
            result.header->sleep_timeout = readLEu16((u8 *)&rawHeader->sleep_timeout);

            if (result.header->chord_count > MAX_CHORD_COUNT)
            {
                result = {TwiddlerConfig::ChordCountTooHigh};
            }
            else
            {
                u32 stringCount = ((result.header->mouse_left_action == 0xff) +
                                   (result.header->mouse_middle_action == 0xff) +
                                   (result.header->mouse_right_action == 0xff));

                ChordTableEntry *chordTable = (ChordTableEntry *)(sourceBytes + sizeof(Header));
                for (u32 chordIndex = 0;
                     chordIndex < result.header->chord_count;
                     ++chordIndex)
                {
                    ChordTableEntry *chord = chordTable + chordIndex;
                    if (chord->hid.modifiers == 0xff)
                    {
                        stringCount += 1;
                    }
                }

                if (stringCount > MAX_STRING_COUNT)
                {
                    result = {TwiddlerConfig::TooManyStrings};
                }
                else
                {
                    u32 *stringLocationTable = (u32 *)(chordTable + result.header->chord_count);

                    for (u32 stringIndex = 0;
                         stringIndex < stringCount;
                         ++stringIndex)
                    {
                        StringTableEntry *stringEntry = (StringTableEntry *)(sourceBytes + stringLocationTable[stringIndex]);
                        if ((u8 *)stringEntry + stringEntry->length > sourceBytes + length)
                        {
                            result = {TwiddlerConfig::DeclaredStringLengthOverrunsBuffer};
                            break;
                        }
                        else if (stringEntry->length > MAX_STRING_LENGTH)
                        {
                            result = {TwiddlerConfig::StringTooLong};
                            break;
                        }
                        else
                        {
                            for (u32 chordIndex = 0;
                                 chordIndex < result.header->chord_count;
                                 ++chordIndex)
                            {
                                ChordTableEntry *chordTableEntry = chordTable + chordIndex;
                                Chord *chord = result.chords + chordIndex;
                                chord->buttons = chordTableEntry->buttons;
                                if (chordTableEntry->hid.modifiers == 0xff)
                                {
                                    u32 offset = stringLocationTable[chordTableEntry->hid.code];
                                    StringTableEntry *stringEntry = (StringTableEntry *)(sourceBytes + offset);
                                    chord->codeCount = (stringEntry->length - sizeof(StringTableEntry)) / sizeof(HidPair);
                                    for (u32 codeIndex = 0;
                                         codeIndex < chord->codeCount;
                                         ++codeIndex)
                                    {
                                        *(chord->codes + codeIndex) = *(stringEntry->elements + codeIndex);
                                    }
                                }
                                else
                                {
                                    chord->codeCount = 1;
                                    *(chord->codes) = chordTableEntry->hid;
                                }

                                result.outcome = TwiddlerConfig::Success;
                            }
                        }
                    }
                }
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
        TwiddlerConfig config = parseTwiddlerConfigV5Bytes(&arena, confFile.data, confFile.length, fileName);
        if (config.outcome != TwiddlerConfig::Success)
        {
            ERROR("Could not parse file \"%s\"\n", fileName)
        }
        else
        {
            for (u32 chordIndex = 0;
                 chordIndex < config.header->chord_count;
                 ++chordIndex)
            {
                Chord *chord = config.chords + chordIndex;
                printf("buttons=0x%04x, length=%d\n", chord->buttons, chord->codeCount);
                for (u32 codeIndex = 0;
                     codeIndex < chord->codeCount;
                     ++codeIndex)
                {
                    HidPair *code = (chord->codes + codeIndex);
                    printf("  (%d)  code=0x%02x, mod=0x%02x\n", codeIndex, code->code, code->modifiers);
                }
            }
        }
    }
}
