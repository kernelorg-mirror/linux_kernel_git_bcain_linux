#!/bin/bash
# Build and boot Hexagon Linux kernel on QEMU
# Uses qemu_defconfig with embedded initramfs for single-user shell

set -e

# Paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="${SCRIPT_DIR}"
TOOLCHAIN="/opt/clang+llvm-21.1.8-cross-hexagon-unknown-linux-musl/x86_64-linux-gnu"
ROOTFS_CPIO="/home/brian/src/toolchain_for_hexagon/hexagon-artifacts/hexagon-artifacts/llvmorg-21.1.8/rootfs.cpio"
QEMU="/opt/qemu_hexagon_11/bin/qemu-system-hexagon"
LOADLINUX="${KERNEL_DIR}/loadlinux"

# Compiler settings
# Use host clang for host tools, hexagon clang for target
HOSTCC="/usr/bin/clang"
HOSTLD="/usr/bin/ld.lld"
CC="${TOOLCHAIN}/bin/hexagon-unknown-linux-musl-clang"
LD="${TOOLCHAIN}/bin/ld.eld"
OBJCOPY="${TOOLCHAIN}/bin/llvm-objcopy"
AR="${TOOLCHAIN}/bin/llvm-ar"
NM="${TOOLCHAIN}/bin/llvm-nm"
STRIP="${TOOLCHAIN}/bin/llvm-strip"

# Build flags
MAKE_FLAGS=(
    "ARCH=hexagon"
    "HOSTCC=${HOSTCC}"
    "HOSTLD=${HOSTLD}"
    "CC=${CC}"
    "AS=${CC}"
    "LD=${LD}"
    "AR=${AR}"
    "NM=${NM}"
    "STRIP=${STRIP}"
    "OBJCOPY=${OBJCOPY}"
    "LLVM=1"
    "LLVM_IAS=1"
)

# Number of parallel jobs
JOBS=$(nproc)

usage() {
    echo "Usage: $0 [clean|config|build|boot|all]"
    echo "  clean  - Clean the build"
    echo "  config - Configure kernel with qemu_defconfig + initramfs"
    echo "  build  - Build the kernel"
    echo "  boot   - Boot the kernel in QEMU"
    echo "  all    - Do config, build, and boot (default)"
    exit 1
}

do_clean() {
    echo "=== Cleaning kernel build ==="
    cd "${KERNEL_DIR}"
    make "${MAKE_FLAGS[@]}" mrproper
}

do_config() {
    echo "=== Configuring kernel with qemu_defconfig ==="
    cd "${KERNEL_DIR}"

    # Start with qemu_defconfig
    make "${MAKE_FLAGS[@]}" qemu_defconfig

    # Add initramfs source to embed the rootfs
    echo "=== Adding initramfs configuration ==="
    if [ -f "${ROOTFS_CPIO}" ]; then
        ./scripts/config --set-str CONFIG_INITRAMFS_SOURCE "${ROOTFS_CPIO}"
        # Ensure initramfs compression is disabled for raw cpio
        ./scripts/config --set-val CONFIG_INITRAMFS_COMPRESSION_NONE y
        ./scripts/config --disable CONFIG_INITRAMFS_COMPRESSION_GZIP
        ./scripts/config --disable CONFIG_INITRAMFS_COMPRESSION_BZIP2
        ./scripts/config --disable CONFIG_INITRAMFS_COMPRESSION_LZMA
        ./scripts/config --disable CONFIG_INITRAMFS_COMPRESSION_XZ
        ./scripts/config --disable CONFIG_INITRAMFS_COMPRESSION_LZO
        ./scripts/config --disable CONFIG_INITRAMFS_COMPRESSION_LZ4
        ./scripts/config --disable CONFIG_INITRAMFS_COMPRESSION_ZSTD
        echo "Initramfs source set to: ${ROOTFS_CPIO}"
    else
        echo "ERROR: rootfs.cpio not found at ${ROOTFS_CPIO}"
        exit 1
    fi

    # Update the kernel command line for init and console
    ./scripts/config --set-str CONFIG_CMDLINE "console=ttyAMA0 maxcpus=4 debug mem=892M lpj=89124080 rdinit=/sbin/init"

    # Update config
    make "${MAKE_FLAGS[@]}" olddefconfig

    echo "=== Configuration complete ==="
}

do_build() {
    echo "=== Building kernel ==="
    cd "${KERNEL_DIR}"

    echo "Building with ld.lld..."
    if ! make "${MAKE_FLAGS[@]}" -j${JOBS} vmlinux 2>&1; then
        echo ""
        echo "=== failed ==="
        exit 1
    fi

    echo "=== Creating vmlinux.bin ==="
    ${OBJCOPY} -O binary vmlinux vmlinux.bin

    echo "=== Build complete ==="
    ls -la vmlinux vmlinux.bin
}

do_boot() {
    echo "=== Booting kernel on QEMU ==="
    cd "${KERNEL_DIR}"

    if [ ! -f vmlinux.bin ]; then
        echo "ERROR: vmlinux.bin not found. Run build first."
        exit 1
    fi

    if [ ! -f "${LOADLINUX}" ]; then
        echo "ERROR: loadlinux not found at ${LOADLINUX}"
        exit 1
    fi

    if [ ! -x "${QEMU}" ]; then
        echo "ERROR: QEMU not found at ${QEMU}"
        exit 1
    fi

    echo "Starting QEMU..."
    echo "Press Ctrl-A X to exit QEMU"
    echo ""

    ${QEMU} \
        -M virt \
        -kernel "${LOADLINUX}" \
        -device loader,addr=0xa0000000,file=vmlinux.bin \
        -m 4G \
        -accel tcg,thread=multi \
        -nographic \
        -serial mon:stdio
}

# Main
cd "${KERNEL_DIR}"

case "${1:-all}" in
    clean)
        do_clean
        ;;
    config)
        do_config
        ;;
    build)
        do_build
        ;;
    boot)
        do_boot
        ;;
    all)
        do_config
        do_build
        do_boot
        ;;
    *)
        usage
        ;;
esac
