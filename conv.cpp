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

int main(int argc, char **argv)
{
    FILE *fp = fopen(argv[1], "r");
    Arena arena;
    initArena(&arena, MegaBytes(1), MAIN_ARENA_BASE);

    if (fp)
    {
        u8 *fileBytes = (u8 *)arena.next;

        u64 length = fread(fileBytes, sizeof(*fileBytes), bytesAvailable(&arena), fp);
        takeBytes(&arena, length);

        if (feof(fp))
        {
            if (length >= sizeof(Header))
            {
                Header *header = (Header *)fileBytes;

                if (header->chord_count <= 1020)
                {
                    ChordTableEntry *chordTable = (ChordTableEntry *)(fileBytes + sizeof(Header));
                    for (u32 chordIndex = 0;
                         chordIndex < header->chord_count;
                         ++chordIndex)
                    {
                        ChordTableEntry *chord = chordTable + chordIndex;
                        printf("chord buttons=%04x code=0x%02x mod=%02x\n", chord->buttons, chord->hidCode, chord->hidModifiers);
                    }
                }
                else
                {
                    fprintf(stderr, "Too many chords\n");
                    exit(1);
                }
            }
            else
            {
                fprintf(stderr, "File %s is too short'n", argv[1]);
                exit(1);
            }
        }
        else
        {
            fprintf(stderr, "Could not read file %s\n", argv[1]);
            exit(1);
        }
    }
    else
    {
        fprintf(stderr, "Could not open file %s\n", argv[1]);
        exit(1);
    }
}
