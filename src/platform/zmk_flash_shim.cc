#include "flash.h"
#include "mem.h"

#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
#include "user_dictionary_store.h"
#include <string.h>
#endif

void Flash::EraseBlockInternal(const void *target, size_t size) {
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  if (zen_user_dictionary_owns(target, size)) {
    memset(const_cast<void *>(target), 0xff, size);
  }
#else
  (void)target;
  (void)size;
#endif
}

void Flash::WriteBlockInternal(const void *target, const void *data,
                               size_t size) {
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  if (zen_user_dictionary_owns(target, size)) {
    memcpy(const_cast<void *>(target), data, size);
  }
#else
  (void)target;
  (void)data;
  (void)size;
#endif
}
