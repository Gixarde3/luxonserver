// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "luxon_ser_c.h"

#include "luxon/ser_gp_binary_v18.hpp"
#include "luxon/ser_gp_binary_v16.hpp"
#include "luxon/ser_ipc_binary.hpp"
#include "luxon/ser_types.hpp"

#include <string>
#include <span>
#include <memory>
#include <exception>
#include <cstdlib>
#include <cstring>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#endif

struct luxon_ser_ctx {
    std::unique_ptr<luxon::ser::IProtocol> proto;
    luxon::ser::IPCBinaryProtocol ipc;
    std::string exception_str;
};

// Internal helpers
static luxon_ser_error_code_t map_error(luxon::ser::Error::Code code) { return static_cast<luxon_ser_error_code_t>(code); }

static luxon_ser_buffer_t make_error_buffer(luxon_ser_error_code_t err) { return {nullptr, 0, err}; }

static luxon_ser_buffer_t make_buffer(const luxon::ser::ByteArray& arr) {
    if (arr.empty()) {
        return {nullptr, 0, LUXON_SER_ERR_OK};
    }
    uint8_t *data = static_cast<uint8_t *>(std::malloc(arr.size()));
    if (!data) {
        return make_error_buffer(LUXON_SER_ERR_UNKNOWN); // OOM
    }
    std::memcpy(data, arr.data(), arr.size());
    return {data, arr.size(), LUXON_SER_ERR_OK};
}

std::string exception_to_string(const std::exception& e) {
    std::string type_name = typeid(e).name();

#if defined(__GNUC__) || defined(__clang__)
    int status = -1;
    char *demangled = abi::__cxa_demangle(type_name.c_str(), nullptr, nullptr, &status);

    if (status == 0 && demangled != nullptr)
        type_name = demangled;

    if (demangled != nullptr)
        std::free(demangled);
#endif

    return type_name + ": " + e.what();
}

extern "C" {
luxon_ser_ctx_t *luxon_ser_ctx_create(luxon_ser_proto_id_t proto_id) {
    try {
        auto ctx = std::make_unique<luxon_ser_ctx>();
        if (proto_id == LUXON_SER_PROTO_GP18) {
            ctx->proto = std::make_unique<luxon::ser::GpBinaryV18>();
        } else if (proto_id == LUXON_SER_PROTO_GP16) {
            ctx->proto = std::make_unique<luxon::ser::GpBinaryV16>();
        } else {
            return nullptr;
        }
        return ctx.release();
    } catch (...) {
        return nullptr;
    }
}

void luxon_ser_ctx_destroy(luxon_ser_ctx_t *ctx) { delete ctx; }

const char *luxon_ser_ctx_get_exc_message(luxon_ser_ctx_t *ctx) { return ctx->exception_str.c_str(); }

void luxon_ser_buffer_free(luxon_ser_buffer_t *buf) {
    if (buf && buf->data) {
        std::free(buf->data);
        buf->data = nullptr;
        buf->size = 0;
    }
}

luxon_ser_buffer_t luxon_ser_gp_to_ipc(luxon_ser_ctx_t *ctx, const uint8_t *gp_bytes, size_t gp_len) {
    if (!ctx || (!gp_bytes && gp_len > 0))
        return make_error_buffer(LUXON_SER_ERR_UNKNOWN);
    try {
        auto msg_res = ctx->proto->Deserialize(std::span<const uint8_t>(gp_bytes, gp_len));
        if (!msg_res) {
            return make_error_buffer(map_error(msg_res.error().code));
        }

        auto ipc_res = ctx->ipc.Serialize(*msg_res);
        if (!ipc_res) {
            return make_error_buffer(map_error(ipc_res.error().code));
        }

        return make_buffer(*ipc_res);
    } catch (const std::exception& e) {
        ctx->exception_str = exception_to_string(e);
        return make_error_buffer(LUXON_SER_ERR_EXCEPTION);
    } catch (...) {
        return make_error_buffer(LUXON_SER_ERR_EXCEPTION);
    }
}

luxon_ser_buffer_t luxon_ser_ipc_to_gp(luxon_ser_ctx_t *ctx, const uint8_t *ipc_bytes, size_t ipc_len, bool encrypt) {
    if (!ctx || (!ipc_bytes && ipc_len > 0))
        return make_error_buffer(LUXON_SER_ERR_UNKNOWN);
    try {
        auto msg_res = ctx->ipc.Deserialize(std::span<const uint8_t>(ipc_bytes, ipc_len));
        if (!msg_res) {
            return make_error_buffer(map_error(msg_res.error().code));
        }

        auto gp_res = ctx->proto->Serialize(std::move(*msg_res), encrypt);
        if (!gp_res) {
            return make_error_buffer(map_error(gp_res.error().code));
        }

        return make_buffer(*gp_res);
    } catch (const std::exception& e) {
        ctx->exception_str = exception_to_string(e);
        return make_error_buffer(LUXON_SER_ERR_EXCEPTION);
    } catch (...) {
        return make_error_buffer(LUXON_SER_ERR_EXCEPTION);
    }
}

bool luxon_ser_has_encryption_key(luxon_ser_ctx_t *ctx) {
    if (!ctx)
        return false;
    return ctx->proto->has_encryption_key();
}

luxon_ser_buffer_t luxon_ser_create_init_encryption_request(luxon_ser_ctx_t *ctx) {
    if (!ctx)
        return make_error_buffer(LUXON_SER_ERR_UNKNOWN);
    try {
        auto req_res = ctx->proto->CreateInitEncryptionRequest();
        if (!req_res) {
            return make_error_buffer(map_error(req_res.error().code));
        }
        return make_buffer(*req_res);
    } catch (const std::exception& e) {
        ctx->exception_str = exception_to_string(e);
        return make_error_buffer(LUXON_SER_ERR_EXCEPTION);
    } catch (...) {
        return make_error_buffer(LUXON_SER_ERR_EXCEPTION);
    }
}

luxon_ser_buffer_t luxon_ser_handle_init_encryption_request(luxon_ser_ctx_t *ctx, const uint8_t *ipc_request_bytes, size_t len) {
    if (!ctx || (!ipc_request_bytes && len > 0))
        return make_error_buffer(LUXON_SER_ERR_UNKNOWN);
    try {
        auto msg_res = ctx->ipc.Deserialize(std::span<const uint8_t>(ipc_request_bytes, len));
        if (!msg_res)
            return make_error_buffer(map_error(msg_res.error().code));

        auto *req = msg_res->get_if<luxon::ser::InternalOperationRequestMessage>();
        if (!req)
            return make_error_buffer(LUXON_SER_ERR_INVALID_VALUE);

        auto resp_res = ctx->proto->HandleInitEncryptionRequest(*req);
        if (!resp_res)
            return make_error_buffer(map_error(resp_res.error().code));

        luxon::ser::Message resp_msg{*resp_res};
        auto ipc_res = ctx->ipc.Serialize(resp_msg);
        if (!ipc_res)
            return make_error_buffer(map_error(ipc_res.error().code));

        return make_buffer(*ipc_res);
    } catch (const std::exception& e) {
        ctx->exception_str = exception_to_string(e);
        return make_error_buffer(LUXON_SER_ERR_EXCEPTION);
    } catch (...) {
        return make_error_buffer(LUXON_SER_ERR_EXCEPTION);
    }
}

luxon_ser_error_code_t luxon_ser_handle_init_encryption_response(luxon_ser_ctx_t *ctx, const uint8_t *ipc_response_bytes, size_t len) {
    if (!ctx || (!ipc_response_bytes && len > 0))
        return LUXON_SER_ERR_UNKNOWN;
    try {
        auto msg_res = ctx->ipc.Deserialize(std::span<const uint8_t>(ipc_response_bytes, len));
        if (!msg_res)
            return map_error(msg_res.error().code);

        auto *resp = msg_res->get_if<luxon::ser::InternalOperationResponseMessage>();
        if (!resp)
            return LUXON_SER_ERR_INVALID_VALUE;

        auto err = ctx->proto->HandleInitEncryptionResponse(*resp);
        if (!err)
            return map_error(err.error().code);

        return LUXON_SER_ERR_OK;
    } catch (const std::exception& e) {
        ctx->exception_str = exception_to_string(e);
        return LUXON_SER_ERR_EXCEPTION;
    } catch (...) {
        return LUXON_SER_ERR_EXCEPTION;
    }
}
} // extern "C"
