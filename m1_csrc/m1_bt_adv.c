/* See COPYING.txt for license details. */

/*
 * m1_bt_adv.c
 *
 * BLE advertisement configuration core. The contract (pure core,
 * protocol-fact presets, wire units, builders never truncate, no
 * sending) lives in m1_bt_adv.h -- read the header first.
 *
 * PRESET PROVENANCE (protocol facts, no GPL application code copied):
 *
 *  - Apple proximity pair: manufacturer-specific data for Apple, Inc.
 *    (Bluetooth SIG company id 0x004C, little-endian "4C 00") carrying
 *    the publicly reverse-engineered continuity "Nearby Action" frame:
 *    type 0x07, frame length 0x19 (25) = 7-byte tag + 1-byte device id
 *    (conventionally the advertiser MAC LSB) + 16-byte encrypted body +
 *    1 trailing byte. Skeleton bytes are neutral placeholders (zeros,
 *    device id 0x00): a live tag would rotate them. Total 31 bytes --
 *    the full legacy cap, which the host suite pins as the boundary
 *    case. Structure: 1E FF 4C 00 07 19 <25 bytes>.
 *
 *  - Samsung Easysetup: manufacturer-specific data for Samsung
 *    Electronics (company id 0x0075, little-endian "75 00") with the
 *    publicly documented Easysetup identifiers 42 09, a 01 sequence
 *    byte, a 6-byte device-address placeholder and neutral filler to
 *    the structure length. Total 28 bytes. Structure:
 *    1B FF 75 00 42 09 01 <6+15 bytes>.
 *
 *  - Microsoft Swift Pair: manufacturer-specific data for Microsoft
 *    Corp (company id 0x0006, little-endian "06 00"), Swift Pair
 *    scenario 0x03, reserved 0x00, then device data (spec asks for
 *    >= 4 bytes for a unique name; the skeleton carries one 0x80
 *    placeholder). NOTE: the plan's anchor literal "07 FF 06 00 03 00
 *    80" is not a self-consistent AD structure under the Core Spec
 *    length rule (its length byte 0x07 would claim 7 following bytes
 *    but only 6 follow), so the shipped skeleton keeps every anchor
 *    byte after the length and corrects the length byte to the
 *    standard count: 06 FF 06 00 03 00 80 (7 bytes total). The table's
 *    own validator enforces this -- an invalid preset cannot ship.
 *
 * M1 Project
 *
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "m1_bt_adv.h"

/*************************** D E F I N E S ************************************/

/* BLE-standard advertising interval bounds in 0.625 ms units
 * (Core Spec Vol 3 Part C / GAP: 0x20 * 0.625 ms = 20 ms minimum,
 * 0x4000 * 0.625 ms = 10.24 s maximum). */
#define M1_BT_ADV_UNITS_MIN   0x20u
#define M1_BT_ADV_UNITS_MAX   0x4000u

/* AD structure type codes used by the builders (Bluetooth Core Spec
 * Supplement, Common Data Types). */
#define M1_BT_ADV_AD_FLAGS          0x01u
#define M1_BT_ADV_AD_COMPLETE_NAME  0x09u
#define M1_BT_ADV_AD_MANUFACTURER   0xFFu

/* Flags value for the NAME preset: LE General Discoverable + BR/EDR
 * Not Supported. */
#define M1_BT_ADV_FLAGS_VALUE       0x06u

/* Reply markers (line-anchored; the console echoes the typed command,
 * so a marker substring mid-line never classifies). */
#define M1_BT_ADV_REPLY_OK_STR      "ADV:OK"
#define M1_BT_ADV_REPLY_FAIL_STR    "ADV:FAIL"

/*************************** T A B L E S **************************************/

/* Apple proximity pair (continuity Nearby Action skeleton), 31 bytes. */
static const uint8_t s_adv_apple[M1_BT_ADV_MAX_PAYLOAD] =
{
    0x1E, 0xFF, 0x4C, 0x00, 0x07, 0x19,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   /* 7-byte tag        */
    0x00,                                        /* device id (MAC LSB) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 16-byte body */
    0x00                                         /* trailing byte     */
};

/* Samsung Easysetup skeleton, 28 bytes. */
static const uint8_t s_adv_samsung[28] =
{
    0x1B, 0xFF, 0x75, 0x00, 0x42, 0x09, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,          /* 6-byte address placeholder */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Microsoft Swift Pair skeleton, 7 bytes (length byte corrected to the
 * standard count -- see the provenance note above). */
static const uint8_t s_adv_swift[7] =
{
    0x06, 0xFF, 0x06, 0x00, 0x03, 0x00, 0x80
};

/* Preset table: fixed-byte entries only (CUSTOM resolves from SD at
 * run time and deliberately has no row here). */
typedef struct
{
    uint8_t         id;     /* m1_bt_adv_preset_id_t value            */
    const char     *name;   /* editor display name                    */
    const uint8_t  *bytes;  /* fixed payload (NULL for CUSTOM)        */
    uint8_t         len;
} m1_bt_adv_preset_row_t;

static const m1_bt_adv_preset_row_t s_adv_presets[] =
{
    { M1_BT_ADV_PRESET_APPLE,   "Apple Prox",  s_adv_apple,   (uint8_t)sizeof(s_adv_apple)   },
    { M1_BT_ADV_PRESET_SAMSUNG, "Samsung ES",  s_adv_samsung, (uint8_t)sizeof(s_adv_samsung) },
    { M1_BT_ADV_PRESET_SWIFT,   "Swift Pair",  s_adv_swift,   (uint8_t)sizeof(s_adv_swift)   },
};

/*************************** P R I V A T E ************************************/

static const m1_bt_adv_preset_row_t *preset_row_find(uint8_t preset)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)(sizeof(s_adv_presets) / sizeof(s_adv_presets[0])); i++)
    {
        if (s_adv_presets[i].id == preset)
        {
            return &s_adv_presets[i];
        }
    }
    return NULL;
}

/* Hex digit value or -1. */
static int hex_val(char c)
{
    if ((c >= '0') && (c <= '9')) { return c - '0'; }
    if ((c >= 'A') && (c <= 'F')) { return c - 'A' + 10; }
    if ((c >= 'a') && (c <= 'f')) { return c - 'a' + 10; }
    return -1;
}

/* True iff s starts a new line at position i (buffer start or right
 * after '\n'). */
static bool at_line_start(const char *s, size_t i)
{
    return (i == 0u) || (s[i - 1u] == '\n');
}

/*************************** P U B L I C **************************************/

uint8_t m1_bt_adv_preset_table_count(void)
{
    return (uint8_t)(sizeof(s_adv_presets) / sizeof(s_adv_presets[0]));
}

bool m1_bt_adv_preset_name(uint8_t preset, char *out, uint8_t out_size)
{
    const m1_bt_adv_preset_row_t *row;

    if ((out == NULL) || (out_size == 0U))
    {
        return false;
    }
    out[0] = '\0';

    if (preset == M1_BT_ADV_PRESET_NAME)
    {
        (void)strncpy(out, "Name beacon", (size_t)(out_size - 1U));
        out[out_size - 1U] = '\0';
        return true;
    }
    if (preset == M1_BT_ADV_PRESET_CUSTOM)
    {
        (void)strncpy(out, "Custom (SD)", (size_t)(out_size - 1U));
        out[out_size - 1U] = '\0';
        return true;
    }

    row = preset_row_find(preset);
    if (row == NULL)
    {
        return false;
    }
    (void)strncpy(out, row->name, (size_t)(out_size - 1U));
    out[out_size - 1U] = '\0';
    return true;
}

bool m1_bt_adv_payload_valid(const uint8_t *bytes, uint8_t len)
{
    uint8_t i = 0U;

    if ((bytes == NULL) || (len == 0U) || (len > M1_BT_ADV_MAX_PAYLOAD))
    {
        return false;
    }

    /* Walk [length][type][data] structures; the length byte counts the
     * type plus the data that follows it. The walk must consume the
     * payload exactly -- no trailing garbage, no structure overrun. */
    while (i < len)
    {
        uint8_t struct_len = bytes[i];

        /* A zero length byte would spin forever and carries no type. */
        if (struct_len == 0U)
        {
            return false;
        }
        /* length byte + its counted bytes must not overrun the end. */
        if ((uint16_t)i + 1u + (uint16_t)struct_len > (uint16_t)len)
        {
            return false;
        }
        i = (uint8_t)(i + 1u + struct_len);
    }
    return i == len;
}

int m1_bt_adv_build_payload(uint8_t preset, const char *name,
                            const uint8_t *custom, uint8_t custom_len,
                            uint8_t *out, uint8_t *out_len)
{
    const m1_bt_adv_preset_row_t *row;
    const char *n = (name != NULL) ? name : "";
    size_t name_len = strlen(n);
    uint8_t used = 0U;

    if ((out == NULL) || (out_len == NULL))
    {
        return -1;
    }
    out[0] = '\0';
    *out_len = 0U;

    switch (preset)
    {
        case M1_BT_ADV_PRESET_NAME:
            /* 02 01 06 + [2+n][09][name]; the name must fit the cap. */
            if (name_len > M1_BT_ADV_NAME_MAX)
            {
                return -1;
            }
            out[used++] = 0x02u;
            out[used++] = M1_BT_ADV_AD_FLAGS;
            out[used++] = M1_BT_ADV_FLAGS_VALUE;
            if (name_len > 0u)
            {
                out[used++] = (uint8_t)(1u + name_len);
                out[used++] = M1_BT_ADV_AD_COMPLETE_NAME;
                memcpy(&out[used], n, name_len);
                used = (uint8_t)(used + name_len);
            }
            break;

        case M1_BT_ADV_PRESET_APPLE:
        case M1_BT_ADV_PRESET_SAMSUNG:
        case M1_BT_ADV_PRESET_SWIFT:
            /* Mimic presets ship byte-exact: the advertisement name is
             * deliberately NOT appended (there is no room in the Apple
             * skeleton -- it fills the 31-byte cap -- and the others
             * follow the same byte-exact policy). */
            row = preset_row_find(preset);
            if (row == NULL)
            {
                return -1;   /* unreachable; keeps the switch total */
            }
            memcpy(out, row->bytes, row->len);
            used = row->len;
            break;

        case M1_BT_ADV_PRESET_CUSTOM:
            if ((custom == NULL) || (!m1_bt_adv_payload_valid(custom, custom_len)))
            {
                return -1;
            }
            memcpy(out, custom, custom_len);
            used = custom_len;
            break;

        default:
            return -1;
    }

    if (!m1_bt_adv_payload_valid(out, used))
    {
        out[0] = '\0';
        *out_len = 0U;
        return -1;
    }
    *out_len = used;
    return 0;
}

int m1_bt_adv_ms_to_units(uint32_t ms)
{
    uint64_t units;

    /* Exactness: 0.625 ms units mean only multiples of 5 ms convert
     * losslessly (ms * 8 must divide by 5). Refuse anything else --
     * a rounded interval on the wire would be a silent lie. */
    if ((ms % 5u) != 0u)
    {
        return -1;
    }
    units = (((uint64_t)ms) * 8u) / 5u;
    if ((units < M1_BT_ADV_UNITS_MIN) || (units > M1_BT_ADV_UNITS_MAX))
    {
        return -1;
    }
    return (int)units;
}

int m1_bt_adv_build_cmd(char *dst, size_t dst_size, uint32_t interval_ms,
                        const uint8_t *payload, uint8_t len)
{
    int units;
    int pos;
    uint8_t i;

    if ((dst == NULL) || (dst_size == 0U))
    {
        return -1;
    }
    dst[0] = '\0';

    units = m1_bt_adv_ms_to_units(interval_ms);
    if (units < 0)
    {
        return -1;
    }
    if (!m1_bt_adv_payload_valid(payload, len))
    {
        return -1;
    }

    pos = snprintf(dst, dst_size, "adv %d ", units);
    if ((pos < 0) || ((size_t)pos >= dst_size))
    {
        dst[0] = '\0';
        return -1;
    }

    /* Uppercase hex, refusing rather than truncating: 2 chars/byte. */
    if ((size_t)pos + ((size_t)len * 2u) + 1u > dst_size)
    {
        dst[0] = '\0';
        return -1;
    }
    for (i = 0U; i < len; i++)
    {
        pos += snprintf(&dst[(size_t)pos], 3u, "%02X", payload[i]);
    }
    return pos;
}

int m1_bt_adv_build_stop_cmd(char *dst, size_t dst_size)
{
    int pos;

    if ((dst == NULL) || (dst_size == 0U))
    {
        return -1;
    }
    pos = snprintf(dst, dst_size, "adv stop");
    if ((pos < 0) || ((size_t)pos >= dst_size))
    {
        dst[0] = '\0';
        return -1;
    }
    return pos;
}

m1_bt_adv_reply_t m1_bt_adv_classify_reply(const char *resp)
{
    size_t i;
    size_t ok_len = strlen(M1_BT_ADV_REPLY_OK_STR);
    size_t fail_len = strlen(M1_BT_ADV_REPLY_FAIL_STR);
    m1_bt_adv_reply_t found = M1_BT_ADV_REPLY_UNKNOWN;

    if (resp == NULL)
    {
        return M1_BT_ADV_REPLY_UNKNOWN;
    }

    /* Line-anchored scan (buffer start or after '\n'); the first
     * marker found at a line start decides. ADV:OK and ADV:FAIL share
     * no line-start prefix conflict, so scan order within a line is
     * not ambiguous in practice; check FAIL first so a hypothetical
     * "ADV:FAIL..." line can never be misread via a shorter prefix. */
    for (i = 0U; resp[i] != '\0'; i++)
    {
        if (!at_line_start(resp, i))
        {
            continue;
        }
        if ((strncmp(&resp[i], M1_BT_ADV_REPLY_FAIL_STR, fail_len) == 0) &&
            (found == M1_BT_ADV_REPLY_UNKNOWN))
        {
            found = M1_BT_ADV_REPLY_FAIL;
        }
        else if ((strncmp(&resp[i], M1_BT_ADV_REPLY_OK_STR, ok_len) == 0) &&
                 (found == M1_BT_ADV_REPLY_UNKNOWN))
        {
            found = M1_BT_ADV_REPLY_OK;
        }
        else
        {
            /* no marker at this line start */
        }
    }
    return found;
}

int m1_bt_adv_custom_parse(const char *hex_text, uint8_t *out, uint8_t *out_len)
{
    uint8_t nibbles = 0U;
    uint8_t acc = 0U;
    bool have_high = false;
    size_t i;

    if ((hex_text == NULL) || (out == NULL) || (out_len == NULL))
    {
        return -1;
    }
    out[0] = '\0';
    *out_len = 0U;

    for (i = 0U; hex_text[i] != '\0'; i++)
    {
        char c = hex_text[i];
        int v;

        if ((c == ' ') || (c == '\t') || (c == '\r') || (c == '\n'))
        {
            continue;
        }
        v = hex_val(c);
        if (v < 0)
        {
            return -1;   /* non-hex content: not an adv payload file */
        }
        if (!have_high)
        {
            acc = (uint8_t)v;
            have_high = true;
            nibbles++;
        }
        else
        {
            uint8_t byte = (uint8_t)((acc << 4) | (uint8_t)v);
            have_high = false;
            if ((nibbles >> 1) >= M1_BT_ADV_MAX_PAYLOAD)
            {
                return -1;   /* over the 31-byte cap */
            }
            out[nibbles >> 1] = byte;
            nibbles++;
        }
    }

    if (have_high || (nibbles == 0U))
    {
        return -1;   /* odd nibble count or empty */
    }

    if (!m1_bt_adv_payload_valid(out, (uint8_t)(nibbles >> 1)))
    {
        return -1;
    }
    *out_len = (uint8_t)(nibbles >> 1);
    return 0;
}

bool m1_bt_adv_file_is_adv(const char *filename)
{
    size_t len;
    size_t i;

    if (filename == NULL)
    {
        return false;
    }
    len = strlen(filename);
    if (len < 5u)   /* shortest possible: "a.adv" */
    {
        return false;
    }
    /* Case-insensitive ".adv" suffix: the dot sits at len-4, the
     * three letters follow it. */
    i = len - 4u;
    if (filename[i] != '.')
    {
        return false;
    }
    return (tolower((unsigned char)filename[i + 1u]) == 'a') &&
           (tolower((unsigned char)filename[i + 2u]) == 'd') &&
           (tolower((unsigned char)filename[i + 3u]) == 'v');
}
