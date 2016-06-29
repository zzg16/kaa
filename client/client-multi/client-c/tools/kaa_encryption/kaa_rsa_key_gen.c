/*
 *  Copyright 2014-2016 CyberVision, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>

/* Filename where public/private keys are stored */
#define KAA_KEYS_STORAGE "kaa_keys_gen.h"

/* RSA Endpoint definitions */
#define KAA_RSA_KEY_LENGTH 2048
#define KAA_RSA_EXPONENT   65537

#define KAA_RSA_PUBLIC_KEY_LENGTH_MAX  294
#define KAA_RSA_PRIVATE_KEY_LENGTH_MAX 1200

#define SHA1_LENGTH 20
/*
 * Structure which contains Endpoint keys.
 *
 * public_key is a pointer to RSA public key.
 * private_key is a pointer to RSA private key.
 *
 * note: the main purpose of the structure is
 * caching calculated keys.
 */
typedef struct {
    uint8_t public_key[KAA_RSA_PUBLIC_KEY_LENGTH_MAX];
    uint8_t private_key[KAA_RSA_PRIVATE_KEY_LENGTH_MAX];
    size_t  public_key_length;
    size_t  private_key_length;
} endpoint_keys_t;

/* Endpoint's RSA Keys */
static endpoint_keys_t keys;
static mbedtls_pk_context pk_context_;

static int rsa_genkey(mbedtls_pk_context *pk);


/* Use this function to extract RSA keys from mbedtls_pk_context.
 * private_key_length and public_key_length should poing to the
 * value which is the size of the private and public keys respectively.
 * They will be initialized with actual length of the keys.
 */
static int kaa_write_keys(mbedtls_pk_context *pk, uint8_t *public_key,
                          size_t *public_key_length, uint8_t *private_key,
                          size_t *private_key_length)
{

    int key_length = mbedtls_pk_write_pubkey_der(pk, public_key, *public_key_length);
    if (key_length < 0) {
        return -1;
    }

    *public_key_length = (size_t)key_length;

    key_length = mbedtls_pk_write_key_der(pk, private_key, *private_key_length);
    if (key_length < 0) {
        return -1;
    }

    *private_key_length = key_length;

    return 0;
}

static int rsa_keys_create(mbedtls_pk_context *pk, uint8_t *public_key,
                           size_t *public_key_length, uint8_t *private_key,
                           size_t *private_key_length)
{
    /* Generate RSA Keys */
    if (rsa_genkey(pk)) {
        return -1;
    }

    return kaa_write_keys(pk, public_key, public_key_length,
                          private_key, private_key_length);
}


static int rsa_genkey(mbedtls_pk_context *pk)
{
    int ret = 0;
    const char *pers = "gen_key";

    mbedtls_pk_init(pk);

    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ctr_drbg_init(&ctr_drbg);

    mbedtls_entropy_context entropy;
    mbedtls_entropy_init(&entropy);


    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                       (const unsigned char *) pers,
                                       strlen(pers))) != 0) {
        goto exit;
    }

    if ((ret = mbedtls_pk_setup(pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0) {
        goto exit;
    }

    mbedtls_rsa_context *context = mbedtls_pk_rsa(*pk);
    mbedtls_rsa_init(context, MBEDTLS_RSA_PKCS_V15, 0);

    ret = mbedtls_rsa_gen_key(context, mbedtls_ctr_drbg_random, &ctr_drbg,
                              KAA_RSA_KEY_LENGTH, KAA_RSA_EXPONENT);
    if (ret) {
        goto exit;
    }

exit:
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return ret;
}

/* File structure */
#define GUARD_IFNDEF                    "#ifndef KAA_RSA_KEYS_H_\n"
#define GUARD_DEF                       "#define KAA_RSA_KEYS_H_\n\n\n"
#define PUBLIC_KEY_LEN                  "#define KAA_RSA_PUBLIC_KEY_LENGTH  %zu\n"
#define PRIVATE_KEY_LEN                 "#define KAA_RSA_PRIVATE_KEY_LENGTH %zu\n\n\n"
#define KAA_SHA1_PUB_LEN                "#define KAA_SHA1_PUB_LEN %zu\n"
#define KAA_SHA1_PUB_BASE64_LEN         "#define KAA_SHA1_PUB_BASE64_LEN %zu\n\n\n"
#define KEY_STARTS                      "{ "
#define KEY_SEPARATOR                   ", "
#define KEY_ENDS                        " };\n\n"
#define KAA_RSA_PUBLIC_KEY              "uint8_t KAA_RSA_PUBLIC_KEY[] = "
#define KAA_RSA_PRIVATE_KEY             "uint8_t KAA_RSA_PRIVATE_KEY[] = "
#define KAA_SHA1_PUB                    "uint8_t KAA_SHA1_PUB[] = "
#define KAA_SHA1_PUB_BASE64             "uint8_t KAA_SHA1_PUB_BASE64[] = "
#define GUARD_ENDIF                     "#endif /* KAA_RSA_KEYS_H */\n"

char buffer[500];
char key[KAA_RSA_PRIVATE_KEY_LENGTH_MAX];

static void store_key(FILE *fd, const char *prefix, size_t prefix_size,
                      uint8_t *key, size_t length)
{
    size_t i;
    size_t written;
    fwrite(prefix, prefix_size, 1, fd);
    fwrite(KEY_STARTS, sizeof(KEY_STARTS) - 1, 1, fd);

    for (i = 0; i < length; i++) {
        written = sprintf(buffer, "0x%02X, ", (int) key[i]);
        fwrite(buffer, written, 1, fd);
    }

    fwrite(KEY_ENDS, sizeof(KEY_ENDS) - 1, 1, fd);
}

static int sha1_store(FILE *fd, uint8_t *sha1, size_t sha1_len, uint8_t *sha1_base64, size_t sha1_base64_len)
{
    store_key(fd, KAA_SHA1_PUB, sizeof(KAA_SHA1_PUB) - 1, sha1, sha1_len);
    store_key(fd, KAA_SHA1_PUB_BASE64, sizeof(KAA_SHA1_PUB_BASE64) - 1, sha1_base64, sha1_base64_len);
    return 0;
}

static int sha1_from_public_key(uint8_t *key, size_t length, uint8_t *sha1)
{
    if (!key || !length) {
        printf("Can't generate sha1\n");
        return -1;
    }
    mbedtls_sha1_context sha1_ctx;
    mbedtls_sha1_init(&sha1_ctx);
    mbedtls_sha1_starts(&sha1_ctx);
    mbedtls_sha1_update(&sha1_ctx,(unsigned char*)key, (int)length);
    mbedtls_sha1_finish(&sha1_ctx, (unsigned char *)sha1);
    mbedtls_sha1_free(&sha1_ctx);

    return 0;
}

static int sha1_to_base64(uint8_t *key, size_t length, uint8_t *base64, size_t base64_len, size_t *output_len)
{
    if (!key || !length) {
        printf("Can't generate base64 representation of the public key\n");
        return -1;
    }

    return  mbedtls_base64_encode((unsigned char *)base64, base64_len, output_len, key, length);
}

static int kaa_keys_store(uint8_t *public_key, size_t public_key_length,
                          uint8_t *private_key, size_t private_key_length)
{

    FILE *fd = fopen(KAA_KEYS_STORAGE, "w");
    if (!fd) {
        return -1;
    }

    size_t written;
    fwrite(GUARD_IFNDEF, sizeof(GUARD_IFNDEF) - 1, 1, fd);
    fwrite(GUARD_DEF, sizeof(GUARD_DEF) - 1, 1, fd);

    written = sprintf(buffer, PUBLIC_KEY_LEN, public_key_length);
    fwrite(buffer, written, 1, fd);

    written = sprintf(buffer, PRIVATE_KEY_LEN, private_key_length);
    fwrite(buffer, written, 1, fd);

    /* Write public key */
    uint8_t *key_begins = public_key + KAA_RSA_PUBLIC_KEY_LENGTH_MAX - public_key_length;
    store_key(fd, KAA_RSA_PUBLIC_KEY, sizeof(KAA_RSA_PUBLIC_KEY) - 1, key_begins, public_key_length);

    key_begins = private_key + KAA_RSA_PRIVATE_KEY_LENGTH_MAX - private_key_length;
    store_key(fd, KAA_RSA_PRIVATE_KEY, sizeof(KAA_RSA_PRIVATE_KEY) - 1, key_begins, private_key_length);

    size_t sha1_base64_len = 0;
    uint8_t sha1[SHA1_LENGTH];
    unsigned char sha1_base64_buffer[1024];
    memset(sha1_base64_buffer, 0, sizeof(sha1_base64_buffer));
    sha1_from_public_key(keys.public_key, keys.public_key_length, sha1);

    int error = sha1_to_base64(sha1, sizeof(sha1), sha1_base64_buffer, sizeof(sha1_base64_buffer), &sha1_base64_len);
    if (error) {
        printf("Error while encoding base64");
    }

    written = sprintf(buffer, KAA_SHA1_PUB_LEN, sizeof(sha1));
    fwrite(buffer, written, 1, fd);

    written = sprintf(buffer, KAA_SHA1_PUB_BASE64_LEN, sha1_base64_len);
    fwrite(buffer, written, 1, fd);

    error = sha1_store(fd, sha1, sizeof(sha1), sha1_base64_buffer, sha1_base64_len);
    if (error) {
        printf("Can't store sha1\n");
    }

    fwrite(GUARD_ENDIF, sizeof(GUARD_ENDIF) - 1, 1, fd);
    fclose(fd);

    return 0;
}

int main()
{
    keys.public_key_length = KAA_RSA_PUBLIC_KEY_LENGTH_MAX;
    keys.private_key_length = KAA_RSA_PRIVATE_KEY_LENGTH_MAX;

    int error = rsa_keys_create(&pk_context_, keys.public_key, &keys.public_key_length,
                                keys.private_key, &keys.private_key_length);
    if (error) {
        printf("Error: can't generate keys (%i)\n", error);
    }

    error = kaa_keys_store(keys.public_key, keys.public_key_length, keys.private_key, keys.private_key_length);
    if (error) {
        printf("Error: Can't store the keys\n");
    }

    mbedtls_pk_free(&pk_context_);
    return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
