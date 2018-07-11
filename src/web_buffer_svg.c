// SPDX-License-Identifier: GPL-3.0+
#include "common.h"

#define BADGE_HORIZONTAL_PADDING 4
#define VERDANA_KERNING 0.2
#define VERDANA_PADDING 1.0

/*
 * verdana11_widths[] has been generated with this method:
 * https://github.com/badges/shields/blob/master/measure-text.js
*/

double verdana11_widths[256] = {
    [0] = 0.0,
    [1] = 0.0,
    [2] = 0.0,
    [3] = 0.0,
    [4] = 0.0,
    [5] = 0.0,
    [6] = 0.0,
    [7] = 0.0,
    [8] = 0.0,
    [9] = 0.0,
    [10] = 0.0,
    [11] = 0.0,
    [12] = 0.0,
    [13] = 0.0,
    [14] = 0.0,
    [15] = 0.0,
    [16] = 0.0,
    [17] = 0.0,
    [18] = 0.0,
    [19] = 0.0,
    [20] = 0.0,
    [21] = 0.0,
    [22] = 0.0,
    [23] = 0.0,
    [24] = 0.0,
    [25] = 0.0,
    [26] = 0.0,
    [27] = 0.0,
    [28] = 0.0,
    [29] = 0.0,
    [30] = 0.0,
    [31] = 0.0,
    [32] = 3.8671874999999996, //
    [33] = 4.3291015625, // !
    [34] = 5.048828125, // "
    [35] = 9.001953125, // #
    [36] = 6.9931640625, // $
    [37] = 11.837890625, // %
    [38] = 7.992187499999999, // &
    [39] = 2.9541015625, // '
    [40] = 4.9951171875, // (
    [41] = 4.9951171875, // )
    [42] = 6.9931640625, // *
    [43] = 9.001953125, // +
    [44] = 4.00146484375, // ,
    [45] = 4.9951171875, // -
    [46] = 4.00146484375, // .
    [47] = 4.9951171875, // /
    [48] = 6.9931640625, // 0
    [49] = 6.9931640625, // 1
    [50] = 6.9931640625, // 2
    [51] = 6.9931640625, // 3
    [52] = 6.9931640625, // 4
    [53] = 6.9931640625, // 5
    [54] = 6.9931640625, // 6
    [55] = 6.9931640625, // 7
    [56] = 6.9931640625, // 8
    [57] = 6.9931640625, // 9
    [58] = 4.9951171875, // :
    [59] = 4.9951171875, // ;
    [60] = 9.001953125, // <
    [61] = 9.001953125, // =
    [62] = 9.001953125, // >
    [63] = 5.99951171875, // ?
    [64] = 11.0, // @
    [65] = 7.51953125, // A
    [66] = 7.541015625, // B
    [67] = 7.680664062499999, // C
    [68] = 8.4755859375, // D
    [69] = 6.95556640625, // E
    [70] = 6.32177734375, // F
    [71] = 8.529296875, // G
    [72] = 8.26611328125, // H
    [73] = 4.6298828125, // I
    [74] = 5.00048828125, // J
    [75] = 7.62158203125, // K
    [76] = 6.123046875, // L
    [77] = 9.2705078125, // M
    [78] = 8.228515625, // N
    [79] = 8.658203125, // O
    [80] = 6.63330078125, // P
    [81] = 8.658203125, // Q
    [82] = 7.6484375, // R
    [83] = 7.51953125, // S
    [84] = 6.7783203125, // T
    [85] = 8.05126953125, // U
    [86] = 7.51953125, // V
    [87] = 10.87646484375, // W
    [88] = 7.53564453125, // X
    [89] = 6.767578125, // Y
    [90] = 7.53564453125, // Z
    [91] = 4.9951171875, // [
    [92] = 4.9951171875, // backslash
    [93] = 4.9951171875, // ]
    [94] = 9.001953125, // ^
    [95] = 6.9931640625, // _
    [96] = 6.9931640625, // `
    [97] = 6.6064453125, // a
    [98] = 6.853515625, // b
    [99] = 5.73095703125, // c
    [100] = 6.853515625, // d
    [101] = 6.552734375, // e
    [102] = 3.8671874999999996, // f
    [103] = 6.853515625, // g
    [104] = 6.9609375, // h
    [105] = 3.0185546875, // i
    [106] = 3.78662109375, // j
    [107] = 6.509765625, // k
    [108] = 3.0185546875, // l
    [109] = 10.69921875, // m
    [110] = 6.9609375, // n
    [111] = 6.67626953125, // o
    [112] = 6.853515625, // p
    [113] = 6.853515625, // q
    [114] = 4.6943359375, // r
    [115] = 5.73095703125, // s
    [116] = 4.33447265625, // t
    [117] = 6.9609375, // u
    [118] = 6.509765625, // v
    [119] = 9.001953125, // w
    [120] = 6.509765625, // x
    [121] = 6.509765625, // y
    [122] = 5.779296875, // z
    [123] = 6.982421875, // {
    [124] = 4.9951171875, // |
    [125] = 6.982421875, // }
    [126] = 9.001953125, // ~
    [127] = 0.0,
    [128] = 0.0,
    [129] = 0.0,
    [130] = 0.0,
    [131] = 0.0,
    [132] = 0.0,
    [133] = 0.0,
    [134] = 0.0,
    [135] = 0.0,
    [136] = 0.0,
    [137] = 0.0,
    [138] = 0.0,
    [139] = 0.0,
    [140] = 0.0,
    [141] = 0.0,
    [142] = 0.0,
    [143] = 0.0,
    [144] = 0.0,
    [145] = 0.0,
    [146] = 0.0,
    [147] = 0.0,
    [148] = 0.0,
    [149] = 0.0,
    [150] = 0.0,
    [151] = 0.0,
    [152] = 0.0,
    [153] = 0.0,
    [154] = 0.0,
    [155] = 0.0,
    [156] = 0.0,
    [157] = 0.0,
    [158] = 0.0,
    [159] = 0.0,
    [160] = 0.0,
    [161] = 0.0,
    [162] = 0.0,
    [163] = 0.0,
    [164] = 0.0,
    [165] = 0.0,
    [166] = 0.0,
    [167] = 0.0,
    [168] = 0.0,
    [169] = 0.0,
    [170] = 0.0,
    [171] = 0.0,
    [172] = 0.0,
    [173] = 0.0,
    [174] = 0.0,
    [175] = 0.0,
    [176] = 0.0,
    [177] = 0.0,
    [178] = 0.0,
    [179] = 0.0,
    [180] = 0.0,
    [181] = 0.0,
    [182] = 0.0,
    [183] = 0.0,
    [184] = 0.0,
    [185] = 0.0,
    [186] = 0.0,
    [187] = 0.0,
    [188] = 0.0,
    [189] = 0.0,
    [190] = 0.0,
    [191] = 0.0,
    [192] = 0.0,
    [193] = 0.0,
    [194] = 0.0,
    [195] = 0.0,
    [196] = 0.0,
    [197] = 0.0,
    [198] = 0.0,
    [199] = 0.0,
    [200] = 0.0,
    [201] = 0.0,
    [202] = 0.0,
    [203] = 0.0,
    [204] = 0.0,
    [205] = 0.0,
    [206] = 0.0,
    [207] = 0.0,
    [208] = 0.0,
    [209] = 0.0,
    [210] = 0.0,
    [211] = 0.0,
    [212] = 0.0,
    [213] = 0.0,
    [214] = 0.0,
    [215] = 0.0,
    [216] = 0.0,
    [217] = 0.0,
    [218] = 0.0,
    [219] = 0.0,
    [220] = 0.0,
    [221] = 0.0,
    [222] = 0.0,
    [223] = 0.0,
    [224] = 0.0,
    [225] = 0.0,
    [226] = 0.0,
    [227] = 0.0,
    [228] = 0.0,
    [229] = 0.0,
    [230] = 0.0,
    [231] = 0.0,
    [232] = 0.0,
    [233] = 0.0,
    [234] = 0.0,
    [235] = 0.0,
    [236] = 0.0,
    [237] = 0.0,
    [238] = 0.0,
    [239] = 0.0,
    [240] = 0.0,
    [241] = 0.0,
    [242] = 0.0,
    [243] = 0.0,
    [244] = 0.0,
    [245] = 0.0,
    [246] = 0.0,
    [247] = 0.0,
    [248] = 0.0,
    [249] = 0.0,
    [250] = 0.0,
    [251] = 0.0,
    [252] = 0.0,
    [253] = 0.0,
    [254] = 0.0,
    [255] = 0.0
};

// find the width of the string using the verdana 11points font
// re-write the string in place, skiping zero-length characters
static inline double verdana11_width(char *s) {
    double w = 0.0;
    char *d = s;

    while(*s) {
        double t = verdana11_widths[(unsigned char)*s];
        if(t == 0.0)
            s++;
        else {
            w += t + VERDANA_KERNING;
            if(d != s)
                *d++ = *s++;
            else
                d = ++s;
        }
    }

    *d = '\0';
    w -= VERDANA_KERNING;
    w += VERDANA_PADDING;
    return w;
}

static inline size_t escape_xmlz(char *dst, const char *src, size_t len) {
    size_t i = len;

    // required escapes from
    // https://github.com/badges/shields/blob/master/badge.js
    while(*src && i) {
        switch(*src) {
            case '\\':
                *dst++ = '/';
                src++;
                i--;
                break;

            case '&':
                if(i > 5) {
                    strcpy(dst, "&amp;");
                    i -= 5;
                    dst += 5;
                    src++;
                }
                else goto cleanup;
                break;

            case '<':
                if(i > 4) {
                    strcpy(dst, "&lt;");
                    i -= 4;
                    dst += 4;
                    src++;
                }
                else goto cleanup;
                break;

            case '>':
                if(i > 4) {
                    strcpy(dst, "&gt;");
                    i -= 4;
                    dst += 4;
                    src++;
                }
                else goto cleanup;
                break;

            case '"':
                if(i > 6) {
                    strcpy(dst, "&quot;");
                    i -= 6;
                    dst += 6;
                    src++;
                }
                else goto cleanup;
                break;

            case '\'':
                if(i > 6) {
                    strcpy(dst, "&apos;");
                    i -= 6;
                    dst += 6;
                    src++;
                }
                else goto cleanup;
                break;

            default:
                i--;
                *dst++ = *src++;
                break;
        }
    }

cleanup:
    *dst = '\0';
    return len - i;
}

static inline char *format_value_with_precision_and_unit(char *value_string, size_t value_string_len, calculated_number value, const char *units, int precision) {
    if(unlikely(isnan(value) || isinf(value)))
        value = 0.0;

    char *separator = "";
    if(unlikely(isalnum(*units)))
        separator = " ";

    if(precision < 0) {
        int len, lstop = 0, trim_zeros = 1;

        calculated_number abs = value;
        if(isless(value, 0)) {
            lstop = 1;
            abs = calculated_number_fabs(value);
        }

        if(isgreaterequal(abs, 1000)) {
            len = snprintfz(value_string, value_string_len, "%0.0" LONG_DOUBLE_MODIFIER, (LONG_DOUBLE) value);
            trim_zeros = 0;
        }
        else if(isgreaterequal(abs, 10))     len = snprintfz(value_string, value_string_len, "%0.1" LONG_DOUBLE_MODIFIER, (LONG_DOUBLE) value);
        else if(isgreaterequal(abs, 1))      len = snprintfz(value_string, value_string_len, "%0.2" LONG_DOUBLE_MODIFIER, (LONG_DOUBLE) value);
        else if(isgreaterequal(abs, 0.1))    len = snprintfz(value_string, value_string_len, "%0.2" LONG_DOUBLE_MODIFIER, (LONG_DOUBLE) value);
        else if(isgreaterequal(abs, 0.01))   len = snprintfz(value_string, value_string_len, "%0.4" LONG_DOUBLE_MODIFIER, (LONG_DOUBLE) value);
        else if(isgreaterequal(abs, 0.001))  len = snprintfz(value_string, value_string_len, "%0.5" LONG_DOUBLE_MODIFIER, (LONG_DOUBLE) value);
        else if(isgreaterequal(abs, 0.0001)) len = snprintfz(value_string, value_string_len, "%0.6" LONG_DOUBLE_MODIFIER, (LONG_DOUBLE) value);
        else                                 len = snprintfz(value_string, value_string_len, "%0.7" LONG_DOUBLE_MODIFIER, (LONG_DOUBLE) value);

        if(unlikely(trim_zeros)) {
            int l;
            // remove trailing zeros from the decimal part
            for(l = len - 1; l > lstop; l--) {
                if(likely(value_string[l] == '0')) {
                    value_string[l] = '\0';
                    len--;
                }

                else if(unlikely(value_string[l] == '.')) {
                    value_string[l] = '\0';
                    len--;
                    break;
                }

                else
                    break;
            }
        }

        if(unlikely(len <= 0)) len = 1;
        snprintfz(&value_string[len], value_string_len - len, "%s%s", separator, units);
    }
    else {
        if(precision > 50) precision = 50;
        snprintfz(value_string, value_string_len, "%0.*" LONG_DOUBLE_MODIFIER "%s%s", precision, (LONG_DOUBLE) value, separator, units);
    }

    return value_string;
}

typedef enum badge_units_format {
    UNITS_FORMAT_NONE,
    UNITS_FORMAT_SECONDS,
    UNITS_FORMAT_SECONDS_AGO,
    UNITS_FORMAT_MINUTES,
    UNITS_FORMAT_MINUTES_AGO,
    UNITS_FORMAT_HOURS,
    UNITS_FORMAT_HOURS_AGO,
    UNITS_FORMAT_ONOFF,
    UNITS_FORMAT_UPDOWN,
    UNITS_FORMAT_OKERROR,
    UNITS_FORMAT_OKFAILED,
    UNITS_FORMAT_EMPTY,
    UNITS_FORMAT_PERCENT
} UNITS_FORMAT;


static struct units_formatter {
    const char *units;
    uint32_t hash;
    UNITS_FORMAT format;
} badge_units_formatters[] = {
        { "seconds",     0, UNITS_FORMAT_SECONDS },
        { "seconds ago", 0, UNITS_FORMAT_SECONDS_AGO },
        { "minutes",     0, UNITS_FORMAT_MINUTES },
        { "minutes ago", 0, UNITS_FORMAT_MINUTES_AGO },
        { "hours",       0, UNITS_FORMAT_HOURS },
        { "hours ago",   0, UNITS_FORMAT_HOURS_AGO },
        { "on/off",      0, UNITS_FORMAT_ONOFF },
        { "on-off",      0, UNITS_FORMAT_ONOFF },
        { "onoff",       0, UNITS_FORMAT_ONOFF },
        { "up/down",     0, UNITS_FORMAT_UPDOWN },
        { "up-down",     0, UNITS_FORMAT_UPDOWN },
        { "updown",      0, UNITS_FORMAT_UPDOWN },
        { "ok/error",    0, UNITS_FORMAT_OKERROR },
        { "ok-error",    0, UNITS_FORMAT_OKERROR },
        { "okerror",     0, UNITS_FORMAT_OKERROR },
        { "ok/failed",   0, UNITS_FORMAT_OKFAILED },
        { "ok-failed",   0, UNITS_FORMAT_OKFAILED },
        { "okfailed",    0, UNITS_FORMAT_OKFAILED },
        { "empty",       0, UNITS_FORMAT_EMPTY },
        { "null",        0, UNITS_FORMAT_EMPTY },
        { "percentage",  0, UNITS_FORMAT_PERCENT },
        { "percent",     0, UNITS_FORMAT_PERCENT },
        { "pcent",       0, UNITS_FORMAT_PERCENT },

        // terminator
        { NULL,          0, UNITS_FORMAT_NONE }
};

inline char *format_value_and_unit(char *value_string, size_t value_string_len, calculated_number value, const char *units, int precision) {
    static int max = -1;
    int i;

    if(unlikely(max == -1)) {
        for(i = 0; badge_units_formatters[i].units; i++)
            badge_units_formatters[i].hash = simple_hash(badge_units_formatters[i].units);

        max = i;
    }

    if(unlikely(!units)) units = "";
    uint32_t hash_units = simple_hash(units);

    UNITS_FORMAT format = UNITS_FORMAT_NONE;
    for(i = 0; i < max; i++) {
        struct units_formatter *ptr = &badge_units_formatters[i];

        if(hash_units == ptr->hash && !strcmp(units, ptr->units)) {
            format = ptr->format;
            break;
        }
    }

    if(unlikely(format == UNITS_FORMAT_SECONDS || format == UNITS_FORMAT_SECONDS_AGO)) {
        if(value == 0.0) {
            snprintfz(value_string, value_string_len, "%s", "now");
            return value_string;
        }
        else if(isnan(value) || isinf(value)) {
            snprintfz(value_string, value_string_len, "%s", "never");
            return value_string;
        }

        const char *suffix = (format == UNITS_FORMAT_SECONDS_AGO)?" ago":"";

        size_t s = (size_t)value;
        size_t d = s / 86400;
        s = s % 86400;

        size_t h = s / 3600;
        s = s % 3600;

        size_t m = s / 60;
        s = s % 60;

        if(d)
            snprintfz(value_string, value_string_len, "%zu %s %02zu:%02zu:%02zu%s", d, (d == 1)?"day":"days", h, m, s, suffix);
        else
            snprintfz(value_string, value_string_len, "%02zu:%02zu:%02zu%s", h, m, s, suffix);

        return value_string;
    }

    else if(unlikely(format == UNITS_FORMAT_MINUTES || format == UNITS_FORMAT_MINUTES_AGO)) {
        if(value == 0.0) {
            snprintfz(value_string, value_string_len, "%s", "now");
            return value_string;
        }
        else if(isnan(value) || isinf(value)) {
            snprintfz(value_string, value_string_len, "%s", "never");
            return value_string;
        }

        const char *suffix = (format == UNITS_FORMAT_MINUTES_AGO)?" ago":"";

        size_t m = (size_t)value;
        size_t d = m / (60 * 24);
        m = m % (60 * 24);

        size_t h = m / 60;
        m = m % 60;

        if(d)
            snprintfz(value_string, value_string_len, "%zud %02zuh %02zum%s", d, h, m, suffix);
        else
            snprintfz(value_string, value_string_len, "%zuh %zum%s", h, m, suffix);

        return value_string;
    }

    else if(unlikely(format == UNITS_FORMAT_HOURS || format == UNITS_FORMAT_HOURS_AGO)) {
        if(value == 0.0) {
            snprintfz(value_string, value_string_len, "%s", "now");
            return value_string;
        }
        else if(isnan(value) || isinf(value)) {
            snprintfz(value_string, value_string_len, "%s", "never");
            return value_string;
        }

        const char *suffix = (format == UNITS_FORMAT_HOURS_AGO)?" ago":"";

        size_t h = (size_t)value;
        size_t d = h / 24;
        h = h % 24;

        if(d)
            snprintfz(value_string, value_string_len, "%zud %zuh%s", d, h, suffix);
        else
            snprintfz(value_string, value_string_len, "%zuh%s", h, suffix);

        return value_string;
    }

    else if(unlikely(format == UNITS_FORMAT_ONOFF)) {
        snprintfz(value_string, value_string_len, "%s", (value != 0.0)?"on":"off");
        return value_string;
    }

    else if(unlikely(format == UNITS_FORMAT_UPDOWN)) {
        snprintfz(value_string, value_string_len, "%s", (value != 0.0)?"up":"down");
        return value_string;
    }

    else if(unlikely(format == UNITS_FORMAT_OKERROR)) {
        snprintfz(value_string, value_string_len, "%s", (value != 0.0)?"ok":"error");
        return value_string;
    }

    else if(unlikely(format == UNITS_FORMAT_OKFAILED)) {
        snprintfz(value_string, value_string_len, "%s", (value != 0.0)?"ok":"failed");
        return value_string;
    }

    else if(unlikely(format == UNITS_FORMAT_EMPTY))
        units = "";

    else if(unlikely(format == UNITS_FORMAT_PERCENT))
        units = "%";

    if(unlikely(isnan(value) || isinf(value))) {
        strcpy(value_string, "-");
        return value_string;
    }

    return format_value_with_precision_and_unit(value_string, value_string_len, value, units, precision);
}

static struct badge_color {
    const char *name;
    uint32_t hash;
    const char *color;
} badge_colors[] = {

        // colors from:
        // https://github.com/badges/shields/blob/master/colorscheme.json

        { "brightgreen", 0, "#4c1"    },
        { "green",       0, "#97CA00" },
        { "yellow",      0, "#dfb317" },
        { "yellowgreen", 0, "#a4a61d" },
        { "orange",      0, "#fe7d37" },
        { "red",         0, "#e05d44" },
        { "blue",        0, "#007ec6" },
        { "grey",        0, "#555"    },
        { "gray",        0, "#555"    },
        { "lightgrey",   0, "#9f9f9f" },
        { "lightgray",   0, "#9f9f9f" },

        // terminator
        { NULL,          0, NULL      }
};

static inline const char *color_map(const char *color) {
    static int max = -1;
    int i;

    if(unlikely(max == -1)) {
        for(i = 0; badge_colors[i].name ;i++)
            badge_colors[i].hash = simple_hash(badge_colors[i].name);

        max = i;
    }

    uint32_t hash = simple_hash(color);

    for(i = 0; i < max; i++) {
        struct badge_color *ptr = &badge_colors[i];

        if(hash == ptr->hash && !strcmp(color, ptr->name))
            return ptr->color;
    }

    return color;
}

typedef enum color_comparison {
    COLOR_COMPARE_EQUAL,
    COLOR_COMPARE_NOTEQUAL,
    COLOR_COMPARE_LESS,
    COLOR_COMPARE_LESSEQUAL,
    COLOR_COMPARE_GREATER,
    COLOR_COMPARE_GREATEREQUAL,
} BADGE_COLOR_COMPARISON;

static inline void calc_colorz(const char *color, char *final, size_t len, calculated_number value) {
    if(isnan(value) || isinf(value))
        value = NAN;

    char color_buffer[256 + 1] = "";
    char value_buffer[256 + 1] = "";
    BADGE_COLOR_COMPARISON comparison = COLOR_COMPARE_GREATER;

    // example input:
    // color<max|color>min|color:null...

    const char *c = color;
    while(*c) {
        char *dc = color_buffer, *dv = NULL;
        size_t ci = 0, vi = 0;

        const char *t = c;

        while(*t && *t != '|') {
            switch(*t) {
                case '!':
                    if(t[1] == '=') t++;
                    comparison = COLOR_COMPARE_NOTEQUAL;
                    dv = value_buffer;
                    break;

                case '=':
                case ':':
                    comparison = COLOR_COMPARE_EQUAL;
                    dv = value_buffer;
                    break;

                case '}':
                case ')':
                case '>':
                    if(t[1] == '=') {
                        comparison = COLOR_COMPARE_GREATEREQUAL;
                        t++;
                    }
                    else
                        comparison = COLOR_COMPARE_GREATER;
                    dv = value_buffer;
                    break;

                case '{':
                case '(':
                case '<':
                    if(t[1] == '=') {
                        comparison = COLOR_COMPARE_LESSEQUAL;
                        t++;
                    }
                    else if(t[1] == '>' || t[1] == ')' || t[1] == '}') {
                        comparison = COLOR_COMPARE_NOTEQUAL;
                        t++;
                    }
                    else
                        comparison = COLOR_COMPARE_LESS;
                    dv = value_buffer;
                    break;

                default:
                    if(dv) {
                        if(vi < 256) {
                            vi++;
                            *dv++ = *t;
                        }
                    }
                    else {
                        if(ci < 256) {
                            ci++;
                            *dc++ = *t;
                        }
                    }
                    break;
            }

            t++;
        }

        // prepare for next iteration
        if(*t == '|') t++;
        c = t;

        // do the math
        *dc = '\0';
        if(dv) {
            *dv = '\0';
            calculated_number v;

            if(!*value_buffer || !strcmp(value_buffer, "null")) {
                v = NAN;
            }
            else {
                v = str2l(value_buffer);
                if(isnan(v) || isinf(v))
                    v = NAN;
            }

            if(unlikely(isnan(value) || isnan(v))) {
                if(isnan(value) && isnan(v))
                    break;
            }
            else {
                     if (unlikely(comparison == COLOR_COMPARE_LESS && isless(value, v))) break;
                else if (unlikely(comparison == COLOR_COMPARE_LESSEQUAL && islessequal(value, v))) break;
                else if (unlikely(comparison == COLOR_COMPARE_GREATER && isgreater(value, v))) break;
                else if (unlikely(comparison == COLOR_COMPARE_GREATEREQUAL && isgreaterequal(value, v))) break;
                else if (unlikely(comparison == COLOR_COMPARE_EQUAL && !islessgreater(value, v))) break;
                else if (unlikely(comparison == COLOR_COMPARE_NOTEQUAL && islessgreater(value, v))) break;
            }
        }
        else
            break;
    }

    const char *b;
    if(color_buffer[0])
        b = color_buffer;
    else
        b = color;

    strncpyz(final, b, len);
}

// value + units
#define VALUE_STRING_SIZE 100

// label
#define LABEL_STRING_SIZE 200

// colors
#define COLOR_STRING_SIZE 100

void buffer_svg(BUFFER *wb, const char *label, calculated_number value, const char *units, const char *label_color, const char *value_color, int precision, int scale, uint32_t options) {
    char      label_buffer[LABEL_STRING_SIZE + 1]
            , value_color_buffer[COLOR_STRING_SIZE + 1]
            , value_string[VALUE_STRING_SIZE + 1]
            , label_escaped[LABEL_STRING_SIZE + 1]
            , value_escaped[VALUE_STRING_SIZE + 1]
            , label_color_escaped[COLOR_STRING_SIZE + 1]
            , value_color_escaped[COLOR_STRING_SIZE + 1];

    double label_width, value_width, total_width, height = 20.0, font_size = 11.0, text_offset = 5.8, round_corner = 3.0;

    if(scale < 100) scale = 100;

    if(unlikely(!label_color || !*label_color))
        label_color = "#555";

    if(unlikely(!value_color || !*value_color))
        value_color = (isnan(value) || isinf(value))?"#999":"#4c1";

    calc_colorz(value_color, value_color_buffer, COLOR_STRING_SIZE, value);
    format_value_and_unit(value_string, VALUE_STRING_SIZE, (options & RRDR_OPTION_DISPLAY_ABS)?calculated_number_fabs(value):value, units, precision);

    // we need to copy the label, since verdana11_width may write to it
    strncpyz(label_buffer, label, LABEL_STRING_SIZE);

    label_width = verdana11_width(label_buffer) + (BADGE_HORIZONTAL_PADDING * 2);
    value_width = verdana11_width(value_string) + (BADGE_HORIZONTAL_PADDING * 2);
    total_width = label_width + value_width;

    escape_xmlz(label_escaped, label_buffer, LABEL_STRING_SIZE);
    escape_xmlz(value_escaped, value_string, VALUE_STRING_SIZE);
    escape_xmlz(label_color_escaped, color_map(label_color), COLOR_STRING_SIZE);
    escape_xmlz(value_color_escaped, color_map(value_color_buffer), COLOR_STRING_SIZE);

    wb->contenttype = CT_IMAGE_SVG_XML;

    total_width  = total_width * scale / 100.0;
    height       = height      * scale / 100.0;
    font_size    = font_size   * scale / 100.0;
    text_offset  = text_offset * scale / 100.0;
    label_width  = label_width * scale / 100.0;
    value_width  = value_width * scale / 100.0;
    round_corner = round_corner * scale / 100.0;

    // svg template from:
    // https://raw.githubusercontent.com/badges/shields/master/templates/flat-template.svg
    buffer_sprintf(wb,
        "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" width=\"%0.2f\" height=\"%0.2f\">"
            "<linearGradient id=\"smooth\" x2=\"0\" y2=\"100%%\">"
                "<stop offset=\"0\" stop-color=\"#bbb\" stop-opacity=\".1\"/>"
                "<stop offset=\"1\" stop-opacity=\".1\"/>"
            "</linearGradient>"
            "<mask id=\"round\">"
                "<rect width=\"%0.2f\" height=\"%0.2f\" rx=\"%0.2f\" fill=\"#fff\"/>"
            "</mask>"
            "<g mask=\"url(#round)\">"
                "<rect width=\"%0.2f\" height=\"%0.2f\" fill=\"%s\"/>"
                "<rect x=\"%0.2f\" width=\"%0.2f\" height=\"%0.2f\" fill=\"%s\"/>"
                "<rect width=\"%0.2f\" height=\"%0.2f\" fill=\"url(#smooth)\"/>"
            "</g>"
            "<g fill=\"#fff\" text-anchor=\"middle\" font-family=\"DejaVu Sans,Verdana,Geneva,sans-serif\" font-size=\"%0.2f\">"
                "<text x=\"%0.2f\" y=\"%0.0f\" fill=\"#010101\" fill-opacity=\".3\">%s</text>"
                "<text x=\"%0.2f\" y=\"%0.0f\">%s</text>"
                "<text x=\"%0.2f\" y=\"%0.0f\" fill=\"#010101\" fill-opacity=\".3\">%s</text>"
                "<text x=\"%0.2f\" y=\"%0.0f\">%s</text>"
            "</g>"
        "</svg>",
        total_width, height,
        total_width, height, round_corner,
        label_width, height, label_color_escaped,
        label_width, value_width, height, value_color_escaped,
        total_width, height,
        font_size,
        label_width / 2, ceil(height - text_offset), label_escaped,
        label_width / 2, ceil(height - text_offset - 1.0), label_escaped,
        label_width + value_width / 2 -1, ceil(height - text_offset), value_escaped,
        label_width + value_width / 2 -1, ceil(height - text_offset - 1.0), value_escaped);
}
