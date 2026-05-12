#ifndef WASM_SOCKET_BITS_WASM_SOCKET_IMPORT_H
#define WASM_SOCKET_BITS_WASM_SOCKET_IMPORT_H

#ifndef WASM_SOCKET_IMPORT_MODULE
#define WASM_SOCKET_IMPORT_MODULE "env"
#endif

#if defined(__wasm__) && defined(__has_attribute)
#  if __has_attribute(import_module) && __has_attribute(import_name)
#    define __WASM_SOCKET_IMPORT(name) \
        __asm__(name) __attribute__((import_module(WASM_SOCKET_IMPORT_MODULE), import_name(name)))
#  else
#    define __WASM_SOCKET_IMPORT(name)
#  endif
#else
#  define __WASM_SOCKET_IMPORT(name)
#endif

#endif /* WASM_SOCKET_BITS_WASM_SOCKET_IMPORT_H */
