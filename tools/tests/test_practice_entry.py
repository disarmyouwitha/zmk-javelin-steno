#!/usr/bin/env python3
"""Test Practice layer entry against the pinned ZMK layer-lock implementation."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from test_practice_menu import function

ROOT=Path(__file__).resolve().parents[2]
PREFIX=r'''
#include <cassert>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <initializer_list>
#include "dt-bindings/zmk/javelin_steno_layers.h"
using zmk_keymap_layer_id_t = uint8_t;
using zmk_keymap_layer_index_t = uint8_t;
using zmk_keymap_layers_state_t = uint32_t;
#define ZMK_KEYMAP_LAYERS_LEN 6
#define ZMK_KEYMAP_LAYER_ID_INVAL 255
#define LAYER_INDEX_TO_ID(x) (x)
#define LAYER_ID_TO_INDEX(x) (x)
#define BIT(x) (1u<<(x))
#define WRITE_BIT(v,b,s) ((v) = (s) ? (v)|BIT(b) : (v)&~BIT(b))
#define ASSERT_LAYER_VAL(layer,fail) if((layer)>=ZMK_KEYMAP_LAYERS_LEN) return (fail);
#define LOG_DBG(...)
#define LOG_WRN(...)
#define ARG_UNUSED(x) (void)(x)
#define CONFIG_ZMK_STENO_SQUID_PRACTICE 1
#define CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING 0
#define IS_ENABLED(x) x
static zmk_keymap_layers_state_t _zmk_keymap_layer_state = 1, _zmk_keymap_layer_locks;
static zmk_keymap_layer_id_t _zmk_keymap_layer_default;
static uint32_t practice_wanted;
static bool practicing;
static void apply_layer();
static int raise_layer_state_changed(uint8_t, bool, bool) { apply_layer(); return 0; }
static void atomic_set(uint32_t *p, uint32_t v) { *p=v; }
static uint32_t atomic_get(uint32_t *p) { return *p; }
static void atomic_clear(uint32_t *p) { *p=0; }
static void steno_squid_practice_set_active(bool v) { practicing=v; }
static void steno_squid_split_status_refresh() {}
'''
CASES=r'''
int main() {
    // &to locks its target. PloverHID (3) must not remain above Javelin (2).
    for (auto initial : {0,1,2,3,5}) {
        zmk_keymap_layer_to(initial,true);
        practice_wanted=0; practicing=false;
        steno_squid_practice_toggle();
        assert(active_layer()==JAVELIN_LAYER_STENO);
        assert(practice_wanted && practicing);
        assert(!(_zmk_keymap_layer_state & BIT(3)));
        steno_squid_practice_toggle();
        assert(active_layer()==JAVELIN_LAYER_STENO && !practicing);
    }
    zmk_keymap_layer_to(0,true);
    zmk_keymap_layer_activate(1,false); // Held Numpad entry.
    steno_squid_practice_toggle();
    zmk_keymap_layer_deactivate(1,false); // Release the held thumb afterward.
    assert(active_layer()==2 && practicing);
    zmk_keymap_layer_to(0,true);
    assert(active_layer()==0 && !practicing && !practice_wanted);
    puts("Practice entry passed: locked Plover/custom layers, Numpad release, toggle and exit");
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zmk_app',type=Path)
    args=parser.parse_args()
    keymap=(args.zmk_app/'src/keymap.c').read_text()
    central=(ROOT/'src/steno_squid/split_status_central.c').read_text()
    code=PREFIX
    for signature in ['zmk_keymap_layer_id_t zmk_keymap_layer_index_to_id(',
                      'zmk_keymap_layer_id_t zmk_keymap_layer_default(',
                      'static inline int set_layer_state(',
                      'bool zmk_keymap_layer_active_with_state(',
                      'bool zmk_keymap_layer_active(',
                      'zmk_keymap_layer_index_t zmk_keymap_highest_layer_active(',
                      'int zmk_keymap_layer_activate(', 'int zmk_keymap_layer_deactivate(',
                      'int zmk_keymap_layer_to(']:
        code+=function(keymap,signature)+'\n'
    for signature in ['static zmk_keymap_layer_id_t active_layer(',
                      'static void apply_layer(', 'void steno_squid_practice_toggle(']:
        code+=function(central,signature)+'\n'
    with tempfile.TemporaryDirectory() as tmp:
        path=Path(tmp)
        (path/'entry.cc').write_text(code+CASES)
        subprocess.run(['clang++','-std=c++20','-I'+str(ROOT/'include'),
                        str(path/'entry.cc'),'-o',str(path/'entry')],check=True)
        subprocess.run([str(path/'entry')],check=True)

if __name__=='__main__':main()
