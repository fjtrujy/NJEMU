#include <miniz.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const unsigned char frozen_zlib_stream[] = {
    0x78, 0x9c, 0xf3, 0xf3, 0x72, 0xf5, 0x0d, 0x55, 0x28, 0x4e,
    0x2c, 0x4b, 0xd5, 0x2d, 0x2e, 0x49, 0x2c, 0x49, 0x55, 0x48,
    0xce, 0xcf, 0x2d, 0x48, 0x2c, 0xc9, 0x4c, 0xca, 0xcc, 0xc9,
    0x2c, 0xa9, 0xe4, 0x02, 0x00, 0xad, 0xa6, 0x0b, 0x41
};
static const unsigned char payload[] = "NJEMU save-state compatibility\n";

int main(void)
{
    unsigned char decoded[sizeof(payload)];
    unsigned char encoded[128];
    mz_ulong decoded_size = sizeof(decoded);
    mz_ulong encoded_size = sizeof(encoded);
    mz_ulong roundtrip_size = sizeof(decoded);

    if (mz_uncompress(decoded, &decoded_size, frozen_zlib_stream,
            sizeof(frozen_zlib_stream)) != MZ_OK ||
        decoded_size != sizeof(payload) - 1 ||
        memcmp(decoded, payload, sizeof(payload) - 1) != 0) {
        fprintf(stderr, "miniz failed to read the frozen zlib stream\n");
        return 1;
    }

    if (mz_compress(encoded, &encoded_size, payload, sizeof(payload) - 1) != MZ_OK) {
        fprintf(stderr, "miniz compression failed\n");
        return 1;
    }

    memset(decoded, 0, sizeof(decoded));
    if (mz_uncompress(decoded, &roundtrip_size, encoded, encoded_size) != MZ_OK ||
        roundtrip_size != sizeof(payload) - 1 ||
        memcmp(decoded, payload, sizeof(payload) - 1) != 0) {
        fprintf(stderr, "miniz compression round-trip failed\n");
        return 1;
    }

    return 0;
}
