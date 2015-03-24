#include <stdint.h>

// From http://www.mirrors.wiretapped.net/security/cryptography/hashes/sha1/sha1.c

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA1_CTX;

void SHA1Init(SHA1_CTX* context);
void SHA1Update(SHA1_CTX* context, uint8_t* data, uint32_t len);
void SHA1Final(SHA1_CTX* context, uint8_t digest[20]);

#ifdef __cplusplus
}
#endif