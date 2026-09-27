/*
 * kb_keymap.c — Keymap selector
 *
 * ビルド時に KEYMAP= オプションでキーマップを切り替える:
 *
 *   make KEYMAP=default all     (デフォルト: QWERTY)
 *   make KEYMAP=dvorak  all     (Dvorak)
 *
 * QMK と同じディレクトリ構造:
 *   keymaps/default/keymap.c
 *   keymaps/dvorak/keymap.c
 *
 * 新しいキーマップを追加するには:
 *   1. keymaps/<name>/keymap.c を作成
 *   2. make KEYMAP=<name> all でビルド
 */

/* KEYMAP_FILE は subdir.mk の -D オプションで定義される */
#ifndef KEYMAP_FILE
#define KEYMAP_FILE "keymaps/default/keymap.c"
#endif

#include KEYMAP_FILE
