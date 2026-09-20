/* Stub for the C++ name demangler (__cxa_demangle).
 *
 * unrar7 is compiled with C++ exceptions enabled -- dll.cpp catches
 * RAR_EXIT and std::bad_alloc, unpack.cpp/model.cpp throw bad_alloc -- so
 * the runtime's __cxa_throw chain holds a reference to __cxa_demangle. That
 * one reference drags the whole Itanium demangler TU into the link: 607
 * symbols, ~105 KiB, over 10% of the final ELF (see docs/SIZE-OPTIMIZATION.md).
 *
 * __cxa_demangle is only ever reached on the uncaught-exception diagnostic
 * path (std::terminate printing the exception's type name). Every unrar
 * exception is caught inside dll.cpp, so that path is unreachable here.
 * Defining the symbol in our own TU keeps cxa_demangle.o out of the archive
 * pull -- the linker resolves against ours and never opens the member.
 *
 * Returning NULL is the documented "demangle failed" result; the caller
 * falls back to printing the mangled name. Exception handling itself
 * (__cxa_throw / __cxa_begin_catch / _Unwind_Resume / __gxx_personality_v0)
 * is untouched. Applies to both the PS5 and the host/linux builds. */
#include <stddef.h>

char *__cxa_demangle(const char *mangled, char *buf, size_t *len, int *status) {
  (void)mangled;
  (void)buf;
  (void)len;

  if(status) {
    *status = -1;
  }

  return 0;
}
