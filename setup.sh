#!/bin/bash
#
# t-hid setup script
# Applies BSP patches and prepares the build environment.
#
# Usage:
#   cd t-hid
#   ./setup.sh
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BSP_DIR="$SCRIPT_DIR/mtk3_bsp"

echo "=== t-hid setup ==="

# 1. Initialize submodule
if [ ! -f "$BSP_DIR/kernel/tkernel/task.c" ]; then
    echo "Initializing submodule..."
    git submodule update --init --recursive
fi

# 1b. Apply patches to OSS submodules (idempotent: skip if already applied)
for patch_file in "$SCRIPT_DIR/patches/btstack_hci_run_after_acl.patch"; do
    [ -f "$patch_file" ] || continue
    case "$(basename "$patch_file")" in
        btstack_*) target="$SCRIPT_DIR/lib/btstack" ;;
        *) echo "  WARNING: unknown patch target for $patch_file"; continue ;;
    esac
    if (cd "$target" && git apply --reverse --check "$patch_file" 2>/dev/null); then
        echo "  Patch already applied: $(basename "$patch_file")"
    elif (cd "$target" && git apply --check "$patch_file" 2>/dev/null); then
        (cd "$target" && git apply "$patch_file")
        echo "  Applied: $(basename "$patch_file")"
    else
        echo "  ERROR: patch does not apply cleanly: $(basename "$patch_file")"
        exit 1
    fi
done

# 2. Apply BSP modifications (overlay copy, replaces patch-based approach)
echo "Applying BSP modifications..."
if [ -d "$SCRIPT_DIR/bsp_overlay" ]; then
    cp -r "$SCRIPT_DIR/bsp_overlay/"* "$BSP_DIR/"
    echo "  BSP overlay applied"
fi

# 3. Create symlinks / copy overlay files into BSP tree
echo "Setting up overlay..."
cd "$SCRIPT_DIR"

# app_program (W5100S ネットワークテスト用)
rm -rf "$BSP_DIR/app_program/app_main.c"
cp app_program/app_main.c "$BSP_DIR/app_program/"
rm -rf "$BSP_DIR/app_program/keyboard"
cp -r app_program/keyboard "$BSP_DIR/app_program/"

# app_kb_program (BLE キーボード専用)
if [ -d "app_kb_program" ]; then
    rm -rf "$BSP_DIR/app_kb_program"
    cp -r app_kb_program "$BSP_DIR/app_kb_program"
fi

# app_wifi_program (WiFi + BLE キーボード)
if [ -d "app_wifi_program" ]; then
    rm -rf "$BSP_DIR/app_wifi_program"
    cp -r app_wifi_program "$BSP_DIR/app_wifi_program"
fi

# app_presence (Pico4ML の離席判定)
if [ -d "app_presence" ]; then
    rm -rf "$BSP_DIR/app_presence"
    cp -r app_presence "$BSP_DIR/app_presence"
fi

# device drivers
for drv in usb_hid i2c_slave w5100s wiznet wifi common ble_hid; do
    if [ -d "device/$drv" ]; then
        rm -rf "$BSP_DIR/device/$drv"
        cp -r "device/$drv" "$BSP_DIR/device/"
    fi
done
for hdr in dev_usb_hid.h dev_i2c_slave.h dev_w5100s.h dev_wiznet.h; do
    if [ -f "device/include/$hdr" ]; then
        cp "device/include/$hdr" "$BSP_DIR/device/include/"
    else
        echo "  WARNING: device/include/$hdr not found"
    fi
done

# build_make (makefile, .mk, .py, subdir.mk 全て)
cp build_make/makefile "$BSP_DIR/build_make/"
for mk in pico_rp2040.mk pico_w.mk pico_w_wifi.mk; do
    cp "build_make/$mk" "$BSP_DIR/build_make/"
done
cp build_make/bin2uf2.py "$BSP_DIR/build_make/" 2>/dev/null || true
cp build_make/elf2uf2.py "$BSP_DIR/build_make/" 2>/dev/null || true
cp build_make/gen_apikey_header.py "$BSP_DIR/build_make/" 2>/dev/null || true
cp .mcp_api_key "$BSP_DIR/" 2>/dev/null || true
cp build_make/gen_wifi_header.py "$BSP_DIR/build_make/" 2>/dev/null || true
if [ -f .wifi_config ]; then
    cp .wifi_config "$BSP_DIR/"
else
    rm -f "$BSP_DIR/.wifi_config"
fi

# kernel extension
if [ -d kernel/extension ]; then
    cp -r kernel/extension "$BSP_DIR/kernel/"
fi

# lib (submodule のシンボリックリンク/コピー)
if [ -d "$SCRIPT_DIR/lib/cjson" ]; then
    rm -rf "$BSP_DIR/lib/cjson"
    cp -r "$SCRIPT_DIR/lib/cjson" "$BSP_DIR/lib/"
fi
# btstack, cyw43-driver, ioLibrary はサブモジュール
# BSP ツリーの既存を強制削除してからコピー
#
# RACE RISK: rm -rf + cp -r is not atomic. If a build is running
# concurrently, deleting these directories mid-compilation will cause
# cryptic "No such file" errors. Guard: skip if a build appears active.
if [ -f "$BSP_DIR/build_make/mtkernel_3.elf" ] && \
   find "$BSP_DIR/build_make" -name '*.o' -newer "$BSP_DIR/build_make/mtkernel_3.elf" -print -quit 2>/dev/null | grep -q .; then
    echo "  WARNING: Build appears to be in progress. Skipping library copy to avoid race."
    echo "  Re-run setup.sh after the build finishes."
else
    for libdir in btstack cyw43-driver ioLibrary wasm3; do
        if [ -d "$SCRIPT_DIR/lib/$libdir" ]; then
            # BSP 側に残っている場合は強制削除 (read-only ファイル対応)
            if [ -d "$BSP_DIR/lib/$libdir" ]; then
                chmod -R u+w "$BSP_DIR/lib/$libdir" 2>/dev/null || true
                rm -rf "$BSP_DIR/lib/$libdir"
            fi
            cp -r "$SCRIPT_DIR/lib/$libdir" "$BSP_DIR/lib/"
        fi
    done
fi

# include
cp include/tk/datetime.h "$BSP_DIR/include/tk/" 2>/dev/null || true

# build_make subdir.mk (IDE 自動生成相当)
if [ -d "$SCRIPT_DIR/build_make/mtkernel_3" ]; then
    cp -r "$SCRIPT_DIR/build_make/mtkernel_3" "$BSP_DIR/build_make/"
fi

# ---- BSP2 (NUCLEO-H533RE) ----
BSP2_DIR="$SCRIPT_DIR/mtk3_bsp2"

if [ -d "$BSP2_DIR" ] && [ -f "$BSP2_DIR/README.md" ]; then
    echo "Setting up BSP2 (STM32H533)..."

    # bsp2_overlay → mtk3_bsp2
    if [ -d "$SCRIPT_DIR/bsp2_overlay" ]; then
        cp -r "$SCRIPT_DIR/bsp2_overlay/"* "$BSP2_DIR/"
        echo "  BSP2 overlay applied"
    fi

    # include/tk/datetime.h (BSP2 ルートと入れ子 mtkernel の両方へ)
    cp include/tk/datetime.h "$BSP2_DIR/include/tk/" 2>/dev/null || true
    if [ -d "$BSP2_DIR/mtkernel/include/tk" ]; then
        cp include/tk/datetime.h "$BSP2_DIR/mtkernel/include/tk/" 2>/dev/null || true
    fi

    # STM32H5 sysdef.h (CPU レベル — bsp2_overlay にないためここでコピー)
    if [ -d "$BSP2_DIR/include/sys/sysdepend/stm32_cube/cpu/stm32h5" ]; then
        echo "  stm32h5/sysdef.h already present"
    fi
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "Build (RP2040):"
echo "  cd mtk3_bsp/build_make"
echo "  make TARGET=_PICO_RP2040_ KEYMAP=default split"
echo ""
echo "Build (NUCLEO-H533RE):"
echo "  cd build_bsp2"
echo "  make"
