// Stubs for Javelin platform functions not relevant to ZMK.

#include "button_script.h"

void ButtonScript::CancelAllStenoKeys() {}

// ZMK's behavior owns physical key state; the empty button script is never used
// to generate/cancel steno chords. These hooks only satisfy its function table.
void ButtonScript::OnStenoKeyPressed() {}
void ButtonScript::OnStenoKeyReleased() {}
void ButtonScript::CancelStenoKeys(StenoKeyState state) { (void)state; }
