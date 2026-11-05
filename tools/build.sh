#!/bin/bash

# Copyright (C) 2026
#
# This file is part of Minecraft miniapp.
#
# Minecraft miniapp is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# Script configuration
set -e  # Exit immediately if a command exits with a non-zero status

# Global variables
VERBOSE=false
NATIVE=false

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

function log_info()    { echo -e "[INFO] $*"; }
function log_error()   { echo -e "${RED}[ERROR] $*${NC}" >&2; }
function log_success() { echo -e "${GREEN}[SUCCESS] $*${NC}"; }
function log_warning() { echo -e "${YELLOW}[WARNING] $*${NC}"; }

function show_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Build script for Minecraft miniapp (jsapi + ui -> amr)"
    echo ""
    echo "Options:"
    echo "  -h, --help          Display this help message"
    echo "  -v, --verbose       Enable verbose output"
    echo "  --native            Build with the local native gcc/g++"
}

# CMake build directory for the selected compiler (set by select_build_dir).
BUILD_DIR=""

# Remove everything a previous run of this script produced, so each build
# compiles from scratch: the CMake build directory, the copied .so files, the
# packaged UI and the .amr packages. Other trees under jsapi/build (the native
# test build, jsapi/build/generated) are not this script's outputs and are kept.
function clean_previous_build() {
    log_info "Cleaning previous build outputs..."
    if [ -z "$BUILD_DIR" ]; then
        log_error "Build directory is not set"
        return 1
    fi
    rm -rf -- "$BUILD_DIR" ui/libs ui/.falcon_
    rm -f -- ui/*.amr dist/*.amr
    log_info "Removed $BUILD_DIR, ui/libs, ui/.falcon_ and old .amr packages"
}

# Create necessary directories
function create_directories() {
    log_info "Creating necessary directories..."
    mkdir -p ui/libs
    mkdir -p dist
}

# Find and setup toolchain
function setup_toolchain() {
    log_info "Setting up toolchain..."

    if [ "$NATIVE" = true ]; then
        unset CROSS_TOOLCHAIN_PREFIX
        command -v gcc >/dev/null || return 1
        command -v g++ >/dev/null || return 1
        log_info "Using native compiler: $(gcc -dumpmachine)"
        return 0
    fi

    if [ -n "${CROSS_TOOLCHAIN_PREFIX:-}" ]; then
        test -x "${CROSS_TOOLCHAIN_PREFIX}gcc" || return 1
        test -x "${CROSS_TOOLCHAIN_PREFIX}g++" || return 1
        log_info "Using explicit cross compiler prefix: $CROSS_TOOLCHAIN_PREFIX"
        return 0
    fi

    local toolchain=$(find jsapi/toolchains -mindepth 1 -maxdepth 1 -type d | head -n 1)
    if [ -z "$toolchain" ]; then
        log_error "No toolchain found in jsapi/toolchains/"
        return 1
    fi

    log_info "Using toolchain: $toolchain"

    local gcc_bin=$(find "$(pwd)/$toolchain/bin" -name "*buildroot*gcc" | head -n 1)
    if [ -z "$gcc_bin" ]; then
        gcc_bin=$(find "$(pwd)/$toolchain/bin" -name "*-gnueabihf-gcc" | head -n 1)
    fi
    if [ -z "$gcc_bin" ]; then
        gcc_bin=$(find "$(pwd)/$toolchain/bin" -name "*-linux-gcc" | head -n 1)
    fi

    if [ -z "$gcc_bin" ]; then
        log_error "No suitable gcc compiler found in toolchain"
        return 1
    fi

    export CROSS_TOOLCHAIN_PREFIX="${gcc_bin%gcc}"
    log_info "Using cross compiler prefix: $CROSS_TOOLCHAIN_PREFIX"
}

function select_build_dir() {
    if [ "$NATIVE" = true ]; then
        BUILD_DIR="jsapi/build/native-arm64"
    else
        BUILD_DIR="jsapi/build/$(basename "$CROSS_TOOLCHAIN_PREFIX")"
    fi
}

# Build native library
function build_native() {
    log_info "Building native library..."

    local build_dir="$BUILD_DIR"
    if ! cmake -DCMAKE_BUILD_TYPE=Release -S jsapi -B "$build_dir"; then
        log_error "CMake configuration failed"
        return 1
    fi

    if ! cmake --build "$build_dir" --parallel "$(nproc)"; then
        log_error "Make build failed"
        return 1
    fi

    if ! cp "$build_dir/libjsapi_Minecraft.so" ui/libs/; then
        log_error "Failed to copy libjsapi_Minecraft.so to ui/libs/"
        return 1
    fi

    local so_path="ui/libs/libjsapi_Minecraft.so"
    local strip_bin
    strip_bin=$(command -v "${CROSS_TOOLCHAIN_PREFIX:-}strip" || true)
    local before_size after_size
    before_size=$(wc -c < "$so_path" | tr -d ' ')
    if [ -x "$strip_bin" ]; then
        log_info "Stripping debug symbols: $strip_bin --strip-unneeded $so_path"
        if "$strip_bin" --strip-unneeded "$so_path"; then
            after_size=$(wc -c < "$so_path" | tr -d ' ')
            log_success "Stripped libjsapi_Minecraft.so: ${before_size} -> ${after_size} bytes"
        else
            log_warning "strip failed, keeping unstripped so (${before_size} bytes)"
        fi
    else
        log_warning "cross strip not found ($strip_bin), so remains ${before_size} bytes with debug symbols"
    fi

    # 可选：若提供了 Minecraft 客户端 .so，则一并打入 amr（随包分发到设备）
    if [ -f vendor/libminecraft.so ]; then
        log_info "Found vendor/libminecraft.so, copying into package"
        cp vendor/libminecraft.so ui/libs/libminecraft.so
    fi

    log_info "Native library build completed successfully"
}

# Package UI
function package_ui() {
    log_info "Packaging UI..."
    node tools/patch-cli.cjs || return 1
    if ! pnpm -C ui package; then
        log_error "UI packaging failed"
        return 1
    fi
}

# Create final distribution
function create_distribution() {
    log_info "Creating final distribution..."

    local appid
    appid=$(node -p "require('./ui/package.json').appid")
    local version
    version=$(node -p "require('./ui/package.json').version.replace(/\\./g, '_')")
    local amr_pattern="${appid}.${version}.amr"
    local amr_file="ui/$amr_pattern"
    if [ ! -f "$amr_file" ]; then
        log_error "No AMR file found matching pattern '$amr_pattern' in ui directory"
        return 1
    fi

    local target_name
    if [ "$NATIVE" = true ]; then
        target_name="minecraft-native-$(uname -m).amr"
    else
        target_name="miniapp-$(basename "$CROSS_TOOLCHAIN_PREFIX" | sed 's/-$//').amr"
    fi
    if ! cp "$amr_file" "dist/$target_name"; then
        log_error "Failed to copy AMR file to distribution directory"
        return 1
    fi

    log_info "Distribution created successfully: dist/$target_name"
}

function main() {
    log_info "Starting Minecraft miniapp build process..."

    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)   show_help; exit 0 ;;
            -v|--verbose) VERBOSE=true; shift ;;
            --native) NATIVE=true; shift ;;
            *) log_error "Unknown option: $1"; show_help; exit 1 ;;
        esac
    done

    setup_toolchain      || exit 1
    select_build_dir
    clean_previous_build || exit 1
    create_directories   || exit 1
    build_native         || exit 1
    package_ui         || exit 1
    create_distribution || exit 1

    log_success "Build process completed successfully!"
}

main "$@"
