#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>

enum MouseClick
{
    right = 0,
    left = 1
};

enum Button
{
    num = 0,
    alt = 4,
    ctrl = 8,
    shift = 12,

    one_right = 1,
    one_middle = 2,
    one_left = 3,

    two_right = 5,
    two_middle = 6,
    two_left = 7,

    three_right = 9,
    three_middle = 10,
    three_left = 11,

    four_right = 13,
    four_middle = 14,
    four_left = 15,
};

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
        char *configurationBuffer = (char *)arena.next;

        u64 length = fread(configurationBuffer, sizeof(*configurationBuffer), bytesAvailable(&arena), fp);
        takeBytes(&arena, length);

        if (feof(fp))
        {
            Header *header = (Header *)configurationBuffer;
            printf("yay! %lu\n", length);
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
    printf("hello\n");
    return 0;
}
