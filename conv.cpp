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
typedef u32 b32;

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

inline void writeLEu32(u8 *bytes, u32 value)
{
    *(bytes++) = value & 0xff;
    value >>= 8;
    *(bytes++) = value & 0xff;
    value >>= 8;
    *(bytes++) = value & 0xff;
    value >>= 8;
    *(bytes++) = value & 0xff;
    value >>= 8;
}

inline void writeLEu16(u8 *bytes, u16 value)
{
    *(bytes++) = value & 0xff;
    value >>= 8;
    *(bytes++) = value & 0xff;
    value >>= 8;
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
    u16 size;
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

u32 min(u32 a, u32 b)
{
    u32 result = (a <= b) ? a : b;
    return result;
}

u32 max(u32 a, u32 b)
{
    u32 result = (a >= b) ? a : b;
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
    u8 *buffer;
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
    u32 stringCount;
    Header *header;
    Chord *chords;
};

struct ParseTwiddlerConfigV5Result
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
    TwiddlerConfig config;
};

ParseTwiddlerConfigV5Result
parseTwiddlerConfigV5(Arena *arena, u8 *buffer, u64 length)
{
    void *memory = arena->next;
    ParseTwiddlerConfigV5Result result = {};
    if (length < sizeof(Header))
    {
        result = {ParseTwiddlerConfigV5Result::IncompleteHeader};
    }
    else
    {
        Header *rawHeader = (Header *)buffer;
        TwiddlerConfig &config = result.config;
        config.header = (Header *)requestBytes(arena, sizeof(Header));
        config.chords = (Chord *)requestBytes(arena, sizeof(Chord) * MAX_CHORD_COUNT);
        if (!config.header || !config.chords)
        {
            result = {ParseTwiddlerConfigV5Result::ArenaFull};
        }
        else
        {
            *config.header = *rawHeader;
            config.header->chord_count = readLEu16((u8 *)&rawHeader->chord_count);
            config.header->sleep_timeout = readLEu16((u8 *)&rawHeader->sleep_timeout);

            if (config.header->chord_count > MAX_CHORD_COUNT)
            {
                result = {ParseTwiddlerConfigV5Result::ChordCountTooHigh};
            }
            else
            {
                config.stringCount = ((config.header->mouse_left_action == 0xff) +
                                      (config.header->mouse_middle_action == 0xff) +
                                      (config.header->mouse_right_action == 0xff));

                ChordTableEntry *chordTable = (ChordTableEntry *)(buffer + sizeof(Header));
                for (u32 chordIndex = 0;
                     chordIndex < config.header->chord_count;
                     ++chordIndex)
                {
                    ChordTableEntry *chord = chordTable + chordIndex;
                    if (chord->hid.modifiers == 0xff)
                    {
                        config.stringCount += 1;
                    }
                }

                if (config.stringCount > MAX_STRING_COUNT)
                {
                    result = {ParseTwiddlerConfigV5Result::TooManyStrings};
                }
                else
                {
                    u32 *locationTable = (u32 *)(chordTable + config.header->chord_count);

                    for (u32 stringIndex = 0;
                         stringIndex < config.stringCount;
                         ++stringIndex)
                    {
                        StringTableEntry *stringEntry = (StringTableEntry *)(buffer + locationTable[stringIndex]);
                        if ((u8 *)stringEntry + stringEntry->size > buffer + length)
                        {
                            result = {ParseTwiddlerConfigV5Result::DeclaredStringLengthOverrunsBuffer};
                            break;
                        }
                        else if (stringEntry->size > sizeof(StringTableEntry) + sizeof(HidPair) * MAX_STRING_LENGTH)
                        {
                            result = {ParseTwiddlerConfigV5Result::StringTooLong};
                            break;
                        }
                    }
                    if (result.outcome == ParseTwiddlerConfigV5Result::Uninitialized)
                    {
                        for (u32 chordIndex = 0;
                             chordIndex < config.header->chord_count;
                             ++chordIndex)
                        {
                            ChordTableEntry *chordTableEntry = chordTable + chordIndex;
                            Chord *chord = config.chords + chordIndex;
                            chord->buttons = chordTableEntry->buttons;
                            if (chordTableEntry->hid.modifiers == 0xff)
                            {
                                u32 offset = locationTable[chordTableEntry->hid.code];
                                StringTableEntry *stringEntry = (StringTableEntry *)(buffer + offset);
                                chord->codeCount = (stringEntry->size - sizeof(StringTableEntry)) / sizeof(HidPair);
                                for (u32 codeIndex = 0;
                                     codeIndex < chord->codeCount;
                                     ++codeIndex)
                                {
                                    chord->codes[codeIndex] = stringEntry->elements[codeIndex];
                                }
                            }
                            else
                            {
                                chord->codeCount = 1;
                                chord->codes[0] = chordTableEntry->hid;
                            }
                        }
                        result.outcome = ParseTwiddlerConfigV5Result::Success;
                    }
                }
            }
        }
    }
    if (result.outcome != ParseTwiddlerConfigV5Result::Success)
    {
        arena->next = memory;
    }
    return result;
}

struct UnparseTwiddlerConfigV5Result
{
    enum Outcome
    {
        Uninitialzed,
        ArenaFull,
        Success,
    };
    Outcome outcome;
    u8 *buffer;
    u64 length;
};

UnparseTwiddlerConfigV5Result unparseTwiddlerConfigV5(Arena *arena, TwiddlerConfig *config)
{
    UnparseTwiddlerConfigV5Result result = {};

    void *memory = arena->next;
    u32 chordCount = config->header->chord_count;

    u32 stringTableSize = 0;
    for (u32 chordIndex = 0;
         chordIndex < chordCount;
         ++chordIndex)
    {
        u32 codeCount = config->chords[chordIndex].codeCount;
        if (codeCount > 1)
        {
            stringTableSize += sizeof(StringTableEntry) + sizeof(HidPair) * codeCount;
        }
    }
    u32 size = (sizeof(Header) +
                sizeof(ChordTableEntry) * chordCount +
                sizeof(u32) * config->stringCount +
                stringTableSize);

    u8 *buffer = (u8 *)requestBytes(arena, size);
    if (!buffer)
    {
        result = {UnparseTwiddlerConfigV5Result::ArenaFull};
    }
    else
    {
        Header *header = (Header *)buffer;
        *header = *config->header;
        writeLEu16((u8 *)&header->chord_count, config->header->chord_count);
        writeLEu16((u8 *)&header->sleep_timeout, config->header->sleep_timeout);
        result = {UnparseTwiddlerConfigV5Result::Success};

        ChordTableEntry *chordTable = (ChordTableEntry *)(buffer + sizeof(Header));
        u32 *locationTable = (u32 *)((u8 *)(chordTable + header->chord_count));
        StringTableEntry *stringTableEntry = (StringTableEntry *)((u8 *)(locationTable + config->stringCount));
        u32 stringIndex = 0;
        for (u32 chordIndex = 0;
             chordIndex < header->chord_count;
             ++chordIndex)
        {
            Chord chord = config->chords[chordIndex];
            chordTable[chordIndex].buttons = chord.buttons;
            if (chord.codeCount == 1)
            {
                chordTable[chordIndex].hid = chord.codes[0];
            }
            else
            {
                chordTable[chordIndex].hid.modifiers = 0xff;
                chordTable[chordIndex].hid.code = stringIndex;
                stringTableEntry->size = sizeof(u16) + sizeof(HidPair) * chord.codeCount;
                for (u32 elementIndex = 0;
                     elementIndex < chord.codeCount;
                     ++elementIndex)
                {
                    stringTableEntry->elements[elementIndex] = chord.codes[elementIndex];
                }
                locationTable[stringIndex] = (u8 *)stringTableEntry - buffer;
                stringTableEntry = (StringTableEntry *)((u8 *)stringTableEntry + stringTableEntry->size);

                ++stringIndex;
            }
        }
        result.length = size;
        result.buffer = buffer;
    }

    if (result.outcome != UnparseTwiddlerConfigV5Result::Success)
    {
        arena->next = memory;
    }
    return result;
}

int main(int argc, char **argv)
{
    Arena arena;
    initArena(&arena, MegaBytes(1), MAIN_ARENA_BASE);
    char *fileName = argv[1];

    ReadFileResult configIn = readFile(&arena, fileName);
    if (configIn.outcome != ReadFileResult::Success)
    {
        ERROR("Could not read file \"%s\"\n", fileName)
    }
    else
    {
        ParseTwiddlerConfigV5Result parseResult = parseTwiddlerConfigV5(&arena, configIn.buffer, configIn.length);
        if (parseResult.outcome != ParseTwiddlerConfigV5Result::Success)
        {
            ERROR("Could not parse file \"%s\"\n", fileName)
        }
        else
        {
            for (u32 chordIndex = 0;
                 chordIndex < parseResult.config.header->chord_count;
                 ++chordIndex)
            {
                Chord *chord = parseResult.config.chords + chordIndex;
                printf("buttons=0x%04x, length=%d\n", chord->buttons, chord->codeCount);
                for (u32 codeIndex = 0;
                     codeIndex < chord->codeCount;
                     ++codeIndex)
                {
                    HidPair *code = (chord->codes + codeIndex);
                    printf("  (%d)  code=0x%02x, mod=0x%02x\n", codeIndex, code->code, code->modifiers);
                }
            }
            UnparseTwiddlerConfigV5Result configOut = unparseTwiddlerConfigV5(&arena, &parseResult.config);

            printf("original size: %lu\n", configIn.length);
            printf("new size:      %lu\n", configOut.length);

            for (u32 i = 0;
                 i < min(configIn.length, configOut.length);
                 ++i)
            {
                if (configIn.buffer[i] != (configOut.buffer[i]))
                {
                    printf("difference in char %d\n", i);
                    assert(!"eeeeh");
                    break;
                }
            }
        }
    }
}
