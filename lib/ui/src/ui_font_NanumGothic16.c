/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --bpp 1 --size 16 --font C:/Users/lck13/Documents/SquaeLine_Studio/M5StickCplus2TEST03/assets/NanumGothic.ttf -o C:/Users/lck13/Documents/SquaeLine_Studio/M5StickCplus2TEST03/assets\ui_font_NanumGothic16.c --format lvgl -r 0x20-0x7f --no-compress --no-prefilter
 ******************************************************************************/

#include "ui.h"

#ifndef UI_FONT_NANUMGOTHIC16
#define UI_FONT_NANUMGOTHIC16 1
#endif

#if UI_FONT_NANUMGOTHIC16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0x90,

    /* U+0022 "\"" */
    0xb6, 0x80,

    /* U+0023 "#" */
    0x12, 0x12, 0x22, 0x22, 0xff, 0x24, 0x24, 0xff,
    0x24, 0x44, 0x44, 0x48,

    /* U+0024 "$" */
    0x10, 0x7b, 0x44, 0x89, 0xa, 0xc, 0xe, 0x16,
    0x24, 0x4c, 0xaf, 0x82, 0x4, 0x0,

    /* U+0025 "%" */
    0x70, 0x44, 0x42, 0x22, 0x21, 0x11, 0x8, 0x90,
    0x39, 0xb8, 0xb, 0x60, 0x91, 0x4, 0x88, 0x44,
    0x42, 0x36, 0x20, 0xe0,

    /* U+0026 "&" */
    0x3c, 0xc, 0xc1, 0x8, 0x21, 0x6, 0x40, 0x70,
    0x12, 0x14, 0x22, 0x86, 0x90, 0x73, 0xe, 0x3e,
    0x60,

    /* U+0027 "'" */
    0xe0,

    /* U+0028 "(" */
    0x22, 0x44, 0x88, 0x88, 0x88, 0x84, 0x42, 0x20,

    /* U+0029 ")" */
    0x44, 0x22, 0x11, 0x11, 0x11, 0x12, 0x24, 0x40,

    /* U+002A "*" */
    0x10, 0x23, 0xf8, 0x82, 0x88, 0x80,

    /* U+002B "+" */
    0x8, 0x4, 0x2, 0x1f, 0xf0, 0x80, 0x40, 0x20,
    0x10,

    /* U+002C "," */
    0x56,

    /* U+002D "-" */
    0xf0,

    /* U+002E "." */
    0x80,

    /* U+002F "/" */
    0x8, 0x20, 0x84, 0x10, 0x42, 0x8, 0x61, 0x4,
    0x30, 0x80,

    /* U+0030 "0" */
    0x3c, 0x42, 0x42, 0x81, 0x81, 0x81, 0x81, 0x81,
    0x81, 0x43, 0x42, 0x3c,

    /* U+0031 "1" */
    0x17, 0x51, 0x11, 0x11, 0x11, 0x11,

    /* U+0032 "2" */
    0x7c, 0x8c, 0x8, 0x10, 0x20, 0x82, 0xc, 0x10,
    0x41, 0x7, 0xf0,

    /* U+0033 "3" */
    0x7c, 0xc, 0x8, 0x10, 0x47, 0x1, 0x1, 0x2,
    0x4, 0x17, 0xc0,

    /* U+0034 "4" */
    0x6, 0x3, 0x2, 0x82, 0x41, 0x21, 0x11, 0x8,
    0x84, 0xff, 0x81, 0x0, 0x80, 0x40,

    /* U+0035 "5" */
    0xfd, 0x2, 0x4, 0x8, 0x1f, 0x1, 0x1, 0x2,
    0x4, 0x17, 0xc0,

    /* U+0036 "6" */
    0x1f, 0x20, 0x40, 0x80, 0xbc, 0xc2, 0x81, 0x81,
    0x81, 0x81, 0x42, 0x3c,

    /* U+0037 "7" */
    0xfe, 0x4, 0x8, 0x20, 0x41, 0x82, 0xc, 0x10,
    0x20, 0x81, 0x0,

    /* U+0038 "8" */
    0x3c, 0xc3, 0x81, 0x81, 0x42, 0x3c, 0x66, 0x83,
    0x81, 0x81, 0x43, 0x3c,

    /* U+0039 "9" */
    0x3c, 0x42, 0x81, 0x81, 0x81, 0x81, 0x43, 0x3d,
    0x1, 0x2, 0x6, 0x78,

    /* U+003A ":" */
    0xf0, 0x3, 0xc0,

    /* U+003B ";" */
    0x6c, 0x0, 0x0, 0x5a, 0x0,

    /* U+003C "<" */
    0x0, 0x4, 0x30, 0xc3, 0xc, 0x30, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x60, 0x40,

    /* U+003D "=" */
    0xff, 0x80, 0x0, 0x1f, 0xf0,

    /* U+003E ">" */
    0x1, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x4,
    0x10, 0x41, 0x4, 0x10, 0x0,

    /* U+003F "?" */
    0xf8, 0x30, 0x41, 0x4, 0x21, 0xc, 0x20, 0x80,
    0x8,

    /* U+0040 "@" */
    0x1f, 0x83, 0xc, 0x40, 0x2c, 0x75, 0x88, 0x99,
    0x9, 0x90, 0x99, 0x12, 0xd3, 0x24, 0xdc, 0x30,
    0x60, 0xf8,

    /* U+0041 "A" */
    0x6, 0x0, 0xc0, 0x2c, 0x4, 0x81, 0x90, 0x21,
    0x4, 0x21, 0x6, 0x3f, 0xcc, 0x9, 0x1, 0xa0,
    0x10,

    /* U+0042 "B" */
    0xfd, 0xe, 0xc, 0x18, 0x7f, 0x21, 0x41, 0x83,
    0x6, 0x17, 0xc0,

    /* U+0043 "C" */
    0x1f, 0x18, 0x10, 0x18, 0x8, 0x4, 0x2, 0x1,
    0x0, 0x80, 0x20, 0x8, 0x3, 0xe0,

    /* U+0044 "D" */
    0xfc, 0x43, 0x20, 0x50, 0x18, 0xc, 0x6, 0x3,
    0x1, 0x81, 0xc0, 0xa1, 0x9f, 0x0,

    /* U+0045 "E" */
    0xfe, 0x8, 0x20, 0x83, 0xf8, 0x20, 0x82, 0x8,
    0x3f,

    /* U+0046 "F" */
    0xfe, 0x8, 0x20, 0x82, 0xf, 0xe0, 0x82, 0x8,
    0x20,

    /* U+0047 "G" */
    0x1f, 0x8c, 0x4, 0x3, 0x0, 0x80, 0x20, 0x8,
    0x3e, 0x1, 0xc0, 0x50, 0x13, 0x4, 0x3f,

    /* U+0048 "H" */
    0x81, 0x81, 0x81, 0x81, 0x81, 0xff, 0x81, 0x81,
    0x81, 0x81, 0x81, 0x81,

    /* U+0049 "I" */
    0xff, 0xf0,

    /* U+004A "J" */
    0x11, 0x11, 0x11, 0x11, 0x11, 0x3e,

    /* U+004B "K" */
    0x82, 0x84, 0x8c, 0x98, 0x90, 0xb0, 0xd0, 0x88,
    0x8c, 0x84, 0x82, 0x83,

    /* U+004C "L" */
    0x82, 0x8, 0x20, 0x82, 0x8, 0x20, 0x82, 0x8,
    0x3f,

    /* U+004D "M" */
    0xc0, 0x3c, 0x3, 0xa0, 0x5a, 0x5, 0xb0, 0xd9,
    0x9, 0x90, 0x98, 0x91, 0x89, 0x18, 0xd1, 0x86,
    0x18, 0x61,

    /* U+004E "N" */
    0xc0, 0xe0, 0x68, 0x36, 0x19, 0xc, 0xc6, 0x23,
    0x9, 0x86, 0xc1, 0x60, 0x70, 0x30,

    /* U+004F "O" */
    0x1e, 0x18, 0x64, 0xa, 0x1, 0x80, 0x60, 0x18,
    0x6, 0x1, 0x80, 0x50, 0x26, 0x18, 0x78,

    /* U+0050 "P" */
    0xf9, 0xe, 0xc, 0x18, 0x30, 0xbe, 0x40, 0x81,
    0x2, 0x4, 0x0,

    /* U+0051 "Q" */
    0x1e, 0x18, 0x64, 0xa, 0x1, 0x80, 0x60, 0x18,
    0x6, 0x1, 0x80, 0x50, 0x26, 0x18, 0x78, 0x1,
    0x0, 0x20,

    /* U+0052 "R" */
    0xfc, 0x86, 0x82, 0x82, 0x82, 0x84, 0xf8, 0x8c,
    0x84, 0x86, 0x82, 0x83,

    /* U+0053 "S" */
    0x7d, 0x82, 0x4, 0x4, 0x6, 0x3, 0x3, 0x2,
    0x4, 0x1b, 0xc0,

    /* U+0054 "T" */
    0xff, 0x84, 0x2, 0x1, 0x0, 0x80, 0x40, 0x20,
    0x10, 0x8, 0x4, 0x2, 0x1, 0x0,

    /* U+0055 "U" */
    0x80, 0xc0, 0x60, 0x30, 0x18, 0xc, 0x6, 0x3,
    0x1, 0x80, 0xc0, 0x50, 0x47, 0xc0,

    /* U+0056 "V" */
    0xc0, 0x50, 0x14, 0xc, 0x82, 0x20, 0x88, 0x41,
    0x10, 0x44, 0x1a, 0x2, 0x80, 0xe0, 0x30,

    /* U+0057 "W" */
    0xc1, 0x81, 0x41, 0x81, 0x41, 0x42, 0x41, 0x42,
    0x22, 0x42, 0x22, 0x66, 0x22, 0x24, 0x34, 0x24,
    0x14, 0x24, 0x14, 0x18, 0x14, 0x18, 0x8, 0x18,

    /* U+0058 "X" */
    0x40, 0xc8, 0x23, 0x10, 0x6c, 0xa, 0x1, 0x0,
    0xe0, 0x68, 0x11, 0x8, 0x66, 0x9, 0x1,

    /* U+0059 "Y" */
    0x80, 0xa0, 0x90, 0x44, 0x43, 0x60, 0xa0, 0x20,
    0x10, 0x8, 0x4, 0x2, 0x1, 0x0,

    /* U+005A "Z" */
    0x7f, 0x3, 0x2, 0x4, 0x4, 0x8, 0x18, 0x10,
    0x20, 0x60, 0x40, 0xff,

    /* U+005B "[" */
    0xf2, 0x49, 0x24, 0x92, 0x49, 0x38,

    /* U+005C "\\" */
    0x42, 0x9, 0xc, 0x44, 0x51, 0x9, 0x44, 0xff,
    0xfc, 0x92, 0x82, 0x4a, 0xa, 0x28, 0x18, 0xa0,
    0x61, 0x1, 0x84, 0x0,

    /* U+005D "]" */
    0xe4, 0x92, 0x49, 0x24, 0x92, 0x78,

    /* U+005E "^" */
    0x18, 0xa, 0xd, 0x4, 0x46, 0x22, 0xb, 0x4,

    /* U+005F "_" */
    0xff, 0x80,

    /* U+0060 "`" */
    0x8, 0x80,

    /* U+0061 "a" */
    0x7c, 0x4, 0x9, 0xf4, 0x30, 0x60, 0xc3, 0x7a,

    /* U+0062 "b" */
    0x81, 0x2, 0x4, 0xb, 0x98, 0xa0, 0xc1, 0x83,
    0x6, 0xe, 0x2b, 0x80,

    /* U+0063 "c" */
    0x3d, 0x8, 0x20, 0x82, 0xc, 0x10, 0x3c,

    /* U+0064 "d" */
    0x2, 0x4, 0x8, 0x17, 0xa8, 0xe0, 0xc1, 0x83,
    0x6, 0xa, 0x33, 0xa0,

    /* U+0065 "e" */
    0x38, 0x8a, 0xc, 0x1f, 0xf0, 0x20, 0x20, 0x3c,

    /* U+0066 "f" */
    0x19, 0x8, 0x4f, 0x90, 0x84, 0x21, 0x8, 0x42,
    0x0,

    /* U+0067 "g" */
    0x3a, 0x8e, 0xc, 0x18, 0x30, 0x60, 0xa3, 0x3a,
    0x9, 0xe0,

    /* U+0068 "h" */
    0x81, 0x2, 0x4, 0xb, 0xd8, 0xe0, 0xc1, 0x83,
    0x6, 0xc, 0x18, 0x20,

    /* U+0069 "i" */
    0x8f, 0xf8,

    /* U+006A "j" */
    0x20, 0x2, 0x49, 0x24, 0x92, 0x70,

    /* U+006B "k" */
    0x82, 0x8, 0x20, 0x8e, 0x29, 0x28, 0xe2, 0x49,
    0xa2, 0x84,

    /* U+006C "l" */
    0xff, 0xf8,

    /* U+006D "m" */
    0xb9, 0xd9, 0xce, 0x10, 0xc2, 0x18, 0x43, 0x8,
    0x61, 0xc, 0x21, 0x84, 0x20,

    /* U+006E "n" */
    0xbd, 0x8e, 0xc, 0x18, 0x30, 0x60, 0xc1, 0x82,

    /* U+006F "o" */
    0x3c, 0x42, 0x81, 0x81, 0x81, 0x81, 0x81, 0x42,
    0x3c,

    /* U+0070 "p" */
    0xb9, 0x8a, 0xc, 0x18, 0x30, 0x60, 0xe2, 0xb9,
    0x2, 0x0,

    /* U+0071 "q" */
    0x3a, 0x8e, 0xc, 0x18, 0x30, 0x60, 0xa3, 0x3a,
    0x4, 0x8,

    /* U+0072 "r" */
    0xbc, 0x88, 0x88, 0x88, 0x80,

    /* U+0073 "s" */
    0x7c, 0x21, 0x83, 0x4, 0x21, 0xf0,

    /* U+0074 "t" */
    0x21, 0x3e, 0x42, 0x10, 0x84, 0x21, 0xe,

    /* U+0075 "u" */
    0x83, 0x6, 0xc, 0x18, 0x30, 0x60, 0xe3, 0x7a,

    /* U+0076 "v" */
    0xc2, 0x42, 0x42, 0x64, 0x24, 0x24, 0x38, 0x18,
    0x18,

    /* U+0077 "w" */
    0xc3, 0xa, 0x18, 0x51, 0x46, 0x89, 0x22, 0x49,
    0x14, 0x48, 0xa2, 0x83, 0xc, 0x18, 0x60,

    /* U+0078 "x" */
    0x42, 0x26, 0x34, 0x18, 0x18, 0x1c, 0x24, 0x62,
    0x42,

    /* U+0079 "y" */
    0x42, 0x85, 0x11, 0x22, 0xc7, 0x6, 0x8, 0x10,
    0x21, 0x80,

    /* U+007A "z" */
    0xfc, 0x10, 0x84, 0x30, 0x84, 0x20, 0xfc,

    /* U+007B "{" */
    0x34, 0x44, 0x44, 0x48, 0x44, 0x44, 0x44, 0x30,

    /* U+007C "|" */
    0xff, 0xfc,

    /* U+007D "}" */
    0xc2, 0x22, 0x22, 0x21, 0x22, 0x22, 0x22, 0xc0,

    /* U+007E "~" */
    0x71, 0xcf
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 72, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 109, .box_w = 1, .box_h = 12, .ofs_x = 3, .ofs_y = -1},
    {.bitmap_index = 3, .adv_w = 110, .box_w = 3, .box_h = 3, .ofs_x = 2, .ofs_y = 8},
    {.bitmap_index = 5, .adv_w = 155, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 17, .adv_w = 155, .box_w = 7, .box_h = 15, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 31, .adv_w = 279, .box_w = 13, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 51, .adv_w = 186, .box_w = 11, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 68, .adv_w = 78, .box_w = 1, .box_h = 3, .ofs_x = 2, .ofs_y = 8},
    {.bitmap_index = 69, .adv_w = 93, .box_w = 4, .box_h = 15, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 77, .adv_w = 93, .box_w = 4, .box_h = 15, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 85, .adv_w = 155, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 91, .adv_w = 167, .box_w = 9, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 100, .adv_w = 78, .box_w = 2, .box_h = 4, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 101, .adv_w = 93, .box_w = 4, .box_h = 1, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 102, .adv_w = 78, .box_w = 1, .box_h = 1, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 103, .adv_w = 96, .box_w = 6, .box_h = 13, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 113, .adv_w = 155, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 125, .adv_w = 155, .box_w = 4, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 131, .adv_w = 155, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 142, .adv_w = 155, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 153, .adv_w = 155, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 167, .adv_w = 155, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 178, .adv_w = 155, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 190, .adv_w = 155, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 201, .adv_w = 155, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 213, .adv_w = 155, .box_w = 8, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 225, .adv_w = 78, .box_w = 2, .box_h = 9, .ofs_x = 2, .ofs_y = 1},
    {.bitmap_index = 228, .adv_w = 78, .box_w = 3, .box_h = 11, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 233, .adv_w = 140, .box_w = 7, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 246, .adv_w = 167, .box_w = 9, .box_h = 4, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 251, .adv_w = 140, .box_w = 7, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 264, .adv_w = 140, .box_w = 6, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 273, .adv_w = 223, .box_w = 12, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 291, .adv_w = 186, .box_w = 11, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 308, .adv_w = 155, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 319, .adv_w = 172, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 333, .adv_w = 186, .box_w = 9, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 347, .adv_w = 147, .box_w = 6, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 356, .adv_w = 129, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 365, .adv_w = 204, .box_w = 10, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 380, .adv_w = 186, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 392, .adv_w = 62, .box_w = 1, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 394, .adv_w = 93, .box_w = 4, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 400, .adv_w = 164, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 412, .adv_w = 124, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 421, .adv_w = 248, .box_w = 12, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 439, .adv_w = 186, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 453, .adv_w = 201, .box_w = 10, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 468, .adv_w = 148, .box_w = 7, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 479, .adv_w = 201, .box_w = 10, .box_h = 14, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 497, .adv_w = 159, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 509, .adv_w = 140, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 520, .adv_w = 140, .box_w = 9, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 534, .adv_w = 186, .box_w = 9, .box_h = 12, .ofs_x = 2, .ofs_y = -1},
    {.bitmap_index = 548, .adv_w = 170, .box_w = 10, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 563, .adv_w = 263, .box_w = 16, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 587, .adv_w = 170, .box_w = 10, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 602, .adv_w = 170, .box_w = 9, .box_h = 12, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 616, .adv_w = 140, .box_w = 8, .box_h = 12, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 628, .adv_w = 93, .box_w = 3, .box_h = 15, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 634, .adv_w = 247, .box_w = 14, .box_h = 11, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 654, .adv_w = 93, .box_w = 3, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 660, .adv_w = 167, .box_w = 9, .box_h = 7, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 668, .adv_w = 140, .box_w = 9, .box_h = 1, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 670, .adv_w = 62, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 9},
    {.bitmap_index = 672, .adv_w = 140, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 680, .adv_w = 155, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 692, .adv_w = 124, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 699, .adv_w = 155, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 711, .adv_w = 140, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 719, .adv_w = 93, .box_w = 5, .box_h = 13, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 728, .adv_w = 155, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 738, .adv_w = 155, .box_w = 7, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 750, .adv_w = 62, .box_w = 1, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 752, .adv_w = 69, .box_w = 3, .box_h = 15, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 758, .adv_w = 133, .box_w = 6, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 768, .adv_w = 62, .box_w = 1, .box_h = 13, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 770, .adv_w = 232, .box_w = 11, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 783, .adv_w = 155, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 791, .adv_w = 155, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 800, .adv_w = 155, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 810, .adv_w = 155, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 820, .adv_w = 93, .box_w = 4, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 825, .adv_w = 109, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 831, .adv_w = 93, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 838, .adv_w = 155, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 846, .adv_w = 124, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 855, .adv_w = 217, .box_w = 13, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 870, .adv_w = 131, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 879, .adv_w = 119, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 889, .adv_w = 124, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 896, .adv_w = 78, .box_w = 4, .box_h = 15, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 904, .adv_w = 62, .box_w = 1, .box_h = 14, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 906, .adv_w = 78, .box_w = 4, .box_h = 15, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 914, .adv_w = 167, .box_w = 8, .box_h = 2, .ofs_x = 1, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] =
{
    34, 53,
    34, 55,
    34, 56,
    34, 58,
    34, 87,
    34, 88,
    34, 90,
    39, 34,
    39, 66,
    39, 68,
    39, 72,
    39, 90,
    45, 53,
    45, 55,
    45, 58,
    49, 34,
    49, 43,
    49, 66,
    49, 68,
    49, 69,
    49, 70,
    49, 72,
    49, 75,
    49, 80,
    49, 82,
    49, 84,
    49, 86,
    53, 34,
    53, 36,
    53, 40,
    53, 43,
    53, 48,
    53, 50,
    53, 52,
    53, 66,
    53, 68,
    53, 69,
    53, 70,
    53, 71,
    53, 72,
    53, 75,
    53, 78,
    53, 79,
    53, 80,
    53, 81,
    53, 82,
    53, 83,
    53, 84,
    53, 85,
    53, 86,
    53, 87,
    53, 88,
    53, 89,
    53, 90,
    53, 91,
    55, 34,
    55, 66,
    55, 68,
    55, 69,
    55, 70,
    55, 72,
    55, 75,
    55, 80,
    55, 82,
    55, 83,
    55, 84,
    55, 85,
    55, 86,
    55, 87,
    55, 88,
    55, 89,
    55, 90,
    55, 91,
    56, 34,
    56, 66,
    56, 68,
    56, 69,
    56, 70,
    56, 72,
    56, 75,
    56, 80,
    56, 82,
    56, 83,
    56, 84,
    56, 86,
    56, 87,
    56, 88,
    56, 89,
    56, 90,
    56, 91,
    58, 34,
    58, 36,
    58, 40,
    58, 43,
    58, 48,
    58, 50,
    58, 52,
    58, 66,
    58, 68,
    58, 69,
    58, 70,
    58, 72,
    58, 75,
    58, 80,
    58, 82,
    58, 83,
    58, 84,
    58, 86,
    58, 87,
    58, 88,
    58, 89,
    58, 90,
    58, 91
};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] =
{
    -14, -24, -17, -29, -13, -13, -5, -16,
    -9, -4, -5, -7, -9, -14, -16, -24,
    -14, -10, -6, -11, -11, -14, -8, -11,
    -11, -11, -11, -26, -16, -12, -12, -13,
    -13, -8, -23, -21, -19, -23, -7, -25,
    -7, -24, -24, -23, -20, -28, -25, -21,
    -11, -25, -14, -17, -17, -15, -19, -25,
    -17, -15, -15, -12, -15, -4, -14, -10,
    -13, -10, -7, -18, -5, -7, -8, -6,
    -8, -21, -12, -11, -13, -12, -10, -7,
    -8, -8, -8, -8, -11, -5, -4, -6,
    -5, -7, -30, -23, -19, -15, -17, -14,
    -8, -24, -24, -26, -28, -24, -8, -25,
    -22, -19, -19, -22, -11, -11, -9, -12,
    -19
};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs =
{
    .glyph_ids = kern_pair_glyph_ids,
    .values = kern_pair_values,
    .pair_cnt = 113,
    .glyph_ids_size = 0
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_pairs,
    .kern_scale = 16,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ui_font_NanumGothic16 = {
#else
lv_font_t ui_font_NanumGothic16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -4,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if UI_FONT_NANUMGOTHIC16*/

