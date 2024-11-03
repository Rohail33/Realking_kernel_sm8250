#!/bin/bash
#set -e
# Copy this script inside the kernel directory
# Define variables
DIR=$(readlink -f .)
MAIN=$(readlink -f ${DIR}/..)
ZIMAGE_DIR="$(pwd)/out/arch/arm64/boot"
KERNEL_DEFCONFIG=munch_defconfig

LINKER="lld"
MAKE="./makeparallel"
BUILD_START=$(date +"%s")
TIME="$(date "+%Y%m%d-%H%M%S")"

# Colors 
blue='\033[0;34m'
nocol='\033[0m'


# Check if the clang compiler is present, if not, clone it from GitHub
if [ ! -d "$MAIN/clang" ]; then
    echo "No clang compiler found ... Cloning from GitHub"

    # Prompt user to choose Clang version
    echo "Choose which Clang to use:"
    echo "1. ZyC Stable (Clang 16.0.6)"
    echo "2. WeebX Stable (Cland 19.1.2)"
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

# Set up environment variables for the build
export PATH="$MAIN/clang/bin:$PATH"
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_COMPILER_STRING="$($MAIN/clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')"

clear

# Display initialization message
echo -e "$blue***********************************************"
echo "          Initializing Kernel Compilation          "
echo -e "***********************************************$nocol"

# Prompt user to choose the build type (MIUI or AOSP)
echo "Choose the build type:"
echo "1. MIUI"
echo "2. AOSP"
read -p "Enter the number of your choice: " build_choice

# Modify dtsi file if MIUI build is selected
if [ "$build_choice" = "1" ]; then
    sed -i 's/qcom,mdss-pan-physical-width-dimension = <70>;$/qcom,mdss-pan-physical-width-dimension = <695>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi
    sed -i 's/qcom,mdss-pan-physical-height-dimension = <155>;$/qcom,mdss-pan-physical-height-dimension = <1546>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi
    zip_name="MIUI"
elif [ "$build_choice" = "2" ]; then
    echo "AOSP build selected. No modifications needed."
    zip_name="AOSP"
else
    echo "Invalid choice. Exiting..."
    exit 1
fi

# Build the kernel
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

# Create a zip file with the built kernel
mkdir -p tmp
cp -fp $ZIMAGE_DIR/Image.gz tmp
cp -fp $ZIMAGE_DIR/dtbo.img tmp
cp -fp $ZIMAGE_DIR/dtb tmp
cp -rp ./anykernel/* tmp
cd tmp
7za a -mx9 tmp.zip *
cd ..
rm -f *.zip
cp -fp tmp/tmp.zip RealKing-Munch-${zip_name}-$TIME.zip
rm -rf tmp
echo $TIME

# Function to revert changes made to the dtsi file
revert_changes() {
    sed -i 's/qcom,mdss-pan-physical-width-dimension = <695>;$/qcom,mdss-pan-physical-width-dimension = <70>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi
    sed -i 's/qcom,mdss-pan-physical-height-dimension = <1546>;$/qcom,mdss-pan-physical-height-dimension = <155>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi
}

# Revert changes after compiling kernel
revert_changes
