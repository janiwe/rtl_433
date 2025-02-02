/** @file
    Oria WA150KM temperature sensor decoder.

    Copyright (C) 2025 Jan Niklaas Wechselberg

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/
/*
Oria WA150KM temperature sensor decoder.

The device uses Manchester coding with G.E. Thomas convention.
The data is bit-reflected.

Data layout after decoding:

    0  1  2  3  4  5  6  7  8  9  10 11 12 13
    FF FF FF ?? ?? CC DD TT II SS ?? ?? ?? BB

- FF = Preamble: 3 bytes of 0xff
- CC = Channel (upper nibble + 1)
- DD = Device ID
- TT = Temperature decimal (upper nibble)
- II = Temperature integer (BCD)
- SS = Sign bit (bit 4, 1 = negative)
- BB = Fixed value 0x65
 */

#include "decoder.h"

#define ORIA_WA150KM_BITLEN  227
#define WARMUP_LEN           3  // number of 0xff bytes at start

static int oria_wa150km_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    data_t *data;
    int r;
    uint8_t *b;

    // Find a valid row (skipping short preamble rows)
    for (r = 0; r < bitbuffer->num_rows; r++) {
        if (bitbuffer->bits_per_row[r] == ORIA_WA150KM_BITLEN) {
            break;
        }
    }
    if (r == bitbuffer->num_rows) {
        decoder_logf(decoder, 2, __func__, "No valid row found with %d bits", ORIA_WA150KM_BITLEN);
        return 0;
    }

    // Check warmup bytes before decoding
    b = bitbuffer->bb[r];
    for (int i = 0; i < WARMUP_LEN; i++) {
        if (b[i] != 0xAA) { // Check for alternating 1/0 pattern before Manchester decoding
            decoder_logf(decoder, 2, __func__, "Warmup byte %d is not 0xaa: %02x", i, b[i]);
            return 0;
        }
    }

    // Check last byte (raw data before Manchester decoding)
    if (b[bitbuffer->bits_per_row[r]/8 - 1] != 0x69) {
        decoder_logf(decoder, 2, __func__, "Last byte is not 0x69: %02x",
                b[bitbuffer->bits_per_row[r]/8 - 1]);
        return 0;
    }

    // Invert the buffer for G.E. Thomas decoding
    bitbuffer_invert(bitbuffer);

    // Manchester decode the row
    bitbuffer_t manchester_buffer = {0};
    bitbuffer_manchester_decode(bitbuffer, r, 0, &manchester_buffer, ORIA_WA150KM_BITLEN);

    // Reflect bits in each byte
    reflect_bytes(manchester_buffer.bb[0], manchester_buffer.bits_per_row[0]/8 + 1);

    b = manchester_buffer.bb[0];

    // Extract channel (upper nibble + 1)
    uint8_t channel = ((b[5] >> 4) & 0x0F) + 1;

    // Extract device ID
    uint8_t device_id = b[6];

    // Extract temperature
    // BCD: Convert each nibble of byte 8 to decimal (tens+ones) and add decimal from byte 7
    float temperature = (((b[8] >> 4) & 0x0F) * 10 + (b[8] & 0x0F)) + ((b[7] >> 4) & 0x0F) * 0.1;
    // Check sign byte (bit 4)
    if (b[9] & 0x08) {
        temperature = -temperature;
    }

    /* clang-format off */
    data = data_make(
            "model",        "", DATA_STRING, "Oria-WA150KM",
            "id",           "", DATA_INT,    device_id,
            "channel",      "", DATA_INT,    channel,
            "temperature",  "", DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            NULL);
    /* clang-format on */
    decoder_output_data(decoder, data);

    return 1;
}

/*
 * List of fields that may appear in the output
 *
 * Used to determine what fields will be output in what
 * order for this device when using -F csv.
 *
 */
static char const *const output_fields[] = {
        "model",
        "id",
        "channel",
        "temperature",
        NULL,
};

/*
 * r_device - registers device/callback. see rtl_433_devices.h
 */
r_device const oria_wa150km = {
        .name        = "Oria WA150KM freezer and fridge thermometer",
        .modulation  = OOK_PULSE_PCM,
        .short_width = 490,
        .long_width  = 490,
        .gap_limit   = 1500,
        .reset_limit = 4000,
        .decode_fn   = &oria_wa150km_decode,
        .disabled    = 0,
        .fields      = output_fields,
};
