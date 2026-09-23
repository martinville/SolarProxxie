#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GHOST_KDF_ITERATIONS 2000
#define GHOST_LEGACY_KDF_ITERATIONS 100000
bool ghost_password_hash(const char *password, const uint8_t salt[16], uint8_t out[32]);
bool ghost_password_hash_iterations(const char *password, const uint8_t salt[16],
                                    uint32_t iterations, uint8_t out[32]);
bool ghost_password_verify(const char *password, const uint8_t salt[16],
                           const uint8_t expected[32]);
bool ghost_password_verify_iterations(const char *password, const uint8_t salt[16],
                                      const uint8_t expected[32], uint32_t iterations);
void ghost_random_hex(char *out, size_t bytes);
bool ghost_equal(const void *a, const void *b, size_t n);
