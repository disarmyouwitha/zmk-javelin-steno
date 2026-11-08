#pragma once

#include <stddef.h>

class Console;
class StenoDictionary;
class StenoUserDictionary;

StenoUserDictionary *zen_user_dictionary_open();
void zen_user_dictionary_register(Console &console, StenoDictionary &parent);
bool zen_user_dictionary_owns(const void *address, size_t size);
