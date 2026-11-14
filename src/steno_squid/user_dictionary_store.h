#pragma once

#include <stddef.h>

class Console;
class StenoDictionary;
class StenoUserDictionary;

StenoUserDictionary *steno_squid_user_dictionary_open();
void steno_squid_user_dictionary_register(Console &console, StenoDictionary &parent);
bool steno_squid_user_dictionary_owns(const void *address, size_t size);
