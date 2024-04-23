#include <stdio.h>
#include <stdint.h>

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

int main(int argc, char **argv)
{
    FILE *fp = fopen("twiddler.cfg", "r");
    u8 configurationBuffer[1024 * 1024];

    if (fp)
    {
        u64 length = fread(configurationBuffer, sizeof(*configurationBuffer), ARRAY_SIZE(configurationBuffer), fp);
        if (feof(fp))
        {
            Header *header = (Header *)configurationBuffer;
            printf("yay! %lu\n", length);
        }
    }
    printf("hello\n");
    return 0;
}
