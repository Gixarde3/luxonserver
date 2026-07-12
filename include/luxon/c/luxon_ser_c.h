// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct luxon_ser_ctx luxon_ser_ctx_t;

// Supported GP protocol versions
typedef enum { LUXON_SER_PROTO_GP18 = 0, LUXON_SER_PROTO_GP16 = 1 } luxon_ser_proto_id_t;

// Maps to luxon::ser::Error::Code
typedef enum {
    LUXON_SER_ERR_OK = 0,
    LUXON_SER_ERR_BAD_PACKET = 1,
    LUXON_SER_ERR_BAD_MAGIC = 2,
    LUXON_SER_ERR_UNSUPPORTED_KIND = 3,
    LUXON_SER_ERR_UNSUPPORTED_TYPE_CODE = 4,
    LUXON_SER_ERR_TRUNCATED = 5,
    LUXON_SER_ERR_INVALID_VALUE = 6,
    LUXON_SER_ERR_DEPTH_LIMIT = 7,
    LUXON_SER_ERR_CRYPTO_NOT_READY = 8,
    LUXON_SER_ERR_CRYPTO_ERROR = 9,
    LUXON_SER_ERR_BAD_PADDING = 10,
    LUXON_SER_ERR_BAD_CIPHERTEXT_LENGTH = 11,
    LUXON_SER_ERR_DH_ERROR = 12,
    LUXON_SER_ERR_HANDSHAKE_ERROR = 13,

    // C-API specific errors
    LUXON_SER_ERR_EXCEPTION = 254,
    LUXON_SER_ERR_UNKNOWN = 255
} luxon_ser_error_code_t;

// Buffer structure for returning dynamically allocated data
typedef struct {
    uint8_t *data;
    size_t size;
    luxon_ser_error_code_t error;
} luxon_ser_buffer_t;

/*
 * Context management
 */

// Creates new serializer context (holds crypto state too)
luxon_ser_ctx_t *luxon_ser_ctx_create(luxon_ser_proto_id_t proto);

// Destroys serializer context
void luxon_ser_ctx_destroy(luxon_ser_ctx_t *ctx);

// Gets last exception error message
const char *luxon_ser_ctx_get_exc_message(luxon_ser_ctx_t *ctx);

// Frees data pointer inside given luxon_ser_buffer_t
void luxon_ser_buffer_free(luxon_ser_buffer_t *buf);

/*
 * Core translators (GP binary bytes <-> IPC binary bytes)
 */

// Translates incoming GP network bytes into IPC binary bytes
luxon_ser_buffer_t luxon_ser_gp_to_ipc(luxon_ser_ctx_t *ctx, const uint8_t *gp_bytes, size_t gp_len);

// Translates local IPC binary bytes into outgoing GP network bytes
// If 'encrypt' is true, outgoing GP payload will be encrypted (requires established crypto)
luxon_ser_buffer_t luxon_ser_ipc_to_gp(luxon_ser_ctx_t *ctx, const uint8_t *ipc_bytes, size_t ipc_len, bool encrypt);

/*
 * Encryption / handshake (IPC boundary wrappers)
 */

// Checks whether context has established encryption key
bool luxon_ser_has_encryption_key(luxon_ser_ctx_t *ctx);

// Emits Init Encryption Request as raw GP network bytes
luxon_ser_buffer_t luxon_ser_create_init_encryption_request(luxon_ser_ctx_t *ctx);

// Feeds incoming Init Encryption Request (already decoded into IPC bytes)
// Returns resulting Init Encryption Response as IPC bytes
luxon_ser_buffer_t luxon_ser_handle_init_encryption_request(luxon_ser_ctx_t *ctx, const uint8_t *ipc_request_bytes, size_t len);

// Feeds incoming Init Encryption Response (already decoded into IPC bytes)
// Completes handshake and initializes context's internal AES key.
luxon_ser_error_code_t luxon_ser_handle_init_encryption_response(luxon_ser_ctx_t *ctx, const uint8_t *ipc_response_bytes, size_t len);

#ifdef __cplusplus
}
#endif
