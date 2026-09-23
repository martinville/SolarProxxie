#include "security.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include <string.h>
bool ghost_equal(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
        diff |= x[i] ^ y[i];
    return diff == 0;
}
bool ghost_password_hash_iterations(const char *password, const uint8_t salt[16],
                                    uint32_t iterations, uint8_t out[32]) {
    return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const unsigned char *)password,
                                         strlen(password), salt, 16, iterations, 32, out) == 0;
}
bool ghost_password_hash(const char *password, const uint8_t salt[16], uint8_t out[32]) {
    return ghost_password_hash_iterations(password, salt, GHOST_KDF_ITERATIONS, out);
}
bool ghost_password_verify_iterations(const char *password, const uint8_t salt[16],
                                      const uint8_t expected[32], uint32_t iterations) {
    uint8_t out[32];
    bool ok = ghost_password_hash_iterations(password, salt, iterations, out) &&
              ghost_equal(out, expected, 32);
    memset(out, 0, sizeof(out));
    return ok;
}
bool ghost_password_verify(const char *password, const uint8_t salt[16],
                           const uint8_t expected[32]) {
    return ghost_password_verify_iterations(password, salt, expected, GHOST_KDF_ITERATIONS);
}
void ghost_random_hex(char *out, size_t bytes) {
    static const char h[] = "0123456789abcdef";
    for (size_t i = 0; i < bytes; i++) {
        uint8_t b;
        esp_fill_random(&b, 1);
        out[2 * i] = h[b >> 4];
        out[2 * i + 1] = h[b & 15];
    }
    out[bytes * 2] = 0;
}
