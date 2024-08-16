#!/bin/bash
#set -e
## Copy this script inside the kernel directory
LINKER="lld"
DIR=`readlink -f .`
MAIN=`readlink -f ${DIR}/..`
KERNEL_DEFCONFIG=vendor/kona-perf_defconfig
export PATH="$MAIN/clang/bin:$PATH"
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_COMPILER_STRING="$($MAIN/clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')"

if ! [ -d "$MAIN/clang" ]; then
    echo "No clang compiler found ... Cloning from GitHub"

    # Prompt user to choose Clang version
    echo "Choose which Clang to use:"
    echo "1. ZyC Stable"
    echo "2. WeebX Stable"
    echo "3. WeebX Beta"
    read -p "Enter the number of your choice: " clang_choice

    # Set URL and archive name based on user choice
    case "$clang_choice" in
        1)
            CLANG_URL=$(curl -s https://raw.githubusercontent.com/v3kt0r-87/Clang-Stable/main/clang-zyc.txt)
            ARCHIVE_NAME="zyc-clang.tar.gz"
            ;;
        2)
            CLANG_URL=$(curl -s https://raw.githubusercontent.com/v3kt0r-87/Clang-Stable/main/clang-weebx.txt)
            ARCHIVE_NAME="weebx-clang.tar.gz"
            ;;
        3)
            CLANG_URL=$(curl -s https://raw.githubusercontent.com/v3kt0r-87/Clang-Stable/main/clang-weebx-beta.txt)
            ARCHIVE_NAME="weebx-clang-beta.tar.gz"
            ;;
        *)
            echo "Invalid choice. Exiting..."
            exit 1
            ;;
    esac
    
    # Download Clang archive
    echo "Downloading Clang ... Please Wait ..."
    if ! wget -P "$MAIN" "$CLANG_URL" -O "$MAIN/$ARCHIVE_NAME"; then
        echo "Failed to download Clang. Exiting..."
        exit 1
    fi

    # Create clang directory and extract archive
    mkdir -p "$MAIN/clang"
    if ! tar -xvf "$MAIN/$ARCHIVE_NAME" -C "$MAIN/clang"; then
        echo "Failed to extract Clang. Exiting..."
        exit 1
    fi

    # Clean up
    rm -f "$MAIN/$ARCHIVE_NAME"

    # Verify directory creation
    if [ ! -d "$MAIN/clang" ]; then
        echo "Failed to create the 'clang' directory. Exiting..."
        exit 1
    fi
fi

KERNEL_DIR=`pwd`
ZIMAGE_DIR="$KERNEL_DIR/out/arch/arm64/boot"
# Speed up build process
MAKE="./makeparallel"
BUILD_START=$(date +"%s")
blue='\033[0;34m'
cyan='\033[0;36m'
yellow='\033[0;33m'
red='\033[0;31m'
nocol='\033[0m'

echo "**** Kernel defconfig is set to $KERNEL_DEFCONFIG ****"
echo -e "$blue***********************************************"
echo "          BUILDING KERNEL          "
echo -e "***********************************************$nocol"
make $KERNEL_DEFCONFIG O=out CC=clang
make -j$(nproc --all) O=out \
                      CC=clang \
                      ARCH=arm64 \
                      CROSS_COMPILE=aarch64-linux-gnu- \
                      NM=llvm-nm \
                      OBJDUMP=llvm-objdump \
                      STRIP=llvm-strip

TIME="$(date "+%Y%m%d-%H%M%S")"
mkdir -p tmp
cp -fp $ZIMAGE_DIR/Image.gz tmp
cp -fp $ZIMAGE_DIR/dtbo.img tmp
cp -fp $ZIMAGE_DIR/dtb tmp
cp -rp ./anykernel/* tmp
cd tmp
7za a -mx9 tmp.zip *
cd ..
rm *.zip
cp -fp tmp/tmp.zip RealKing-OPKona-$TIME.zip
rm -rf tmp
echo $TIME
