#!/bin/bash
#set -e

## Copy this script inside the kernel directory
LINKER="lld"
DIR=$(readlink -f .)
MAIN=$(readlink -f ${DIR}/..)
KERNEL_DEFCONFIG=munch_defconfig
export PATH="$MAIN/clang/bin:$PATH"
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_COMPILER_STRING="$($MAIN/clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')"

if ! [ -d "$MAIN/clang" ]; then
    echo "ZYC clang not found! Cloning..."
    if ! git clone -q https://gitlab.com/ZyCromerZ/clang.git --depth=1 --single-branch $MAIN/clang; then
        echo "Cloning failed! Aborting..."
        exit 1
    fi
fi

KERNEL_DIR=$(pwd)
ZIMAGE_DIR="$KERNEL_DIR/out/arch/arm64/boot"
# Speed up build process
MAKE="./makeparallel"
BUILD_START=$(date +"%s")
blue='\033[0;34m'
cyan='\033[0;36m'
yellow='\033[0;33m'
red='\033[0;31m'
nocol='\033[0m'

echo -e "$blue***********************************************"
echo "          Initializing Kernel Compilation          "
echo -e "***********************************************$nocol"

# Create a function to revert changes
revert_changes() {
    sed -i 's/qcom,mdss-pan-physical-width-dimension = <695>;$/qcom,mdss-pan-physical-width-dimension = <70>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi
    sed -i 's/qcom,mdss-pan-physical-height-dimension = <1546>;$/qcom,mdss-pan-physical-height-dimension = <155>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi
}

# Prompt the user for the build type (MIUI or AOSP)
echo "Choose the build type:"
echo "1. MIUI"
echo "2. AOSP"
read -p "Enter the number of your choice: " build_choice

zip_name=""
if [ "$build_choice" = "1" ]; then
    # Modify lines in the dtsi file for MIUI
    sed -i 's/qcom,mdss-pan-physical-width-dimension = <70>;$/qcom,mdss-pan-physical-width-dimension = <695>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi
    sed -i 's/qcom,mdss-pan-physical-height-dimension = <155>;$/qcom,mdss-pan-physical-height-dimension = <1546>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-l11r-38-08-0a-dsc-cmd.dtsi
    zip_name="MIUI"
elif [ "$build_choice" = "2" ]; then
    # No modifications needed for AOSP build
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
cp -fp tmp/tmp.zip RealKing-Munch-${zip_name}-$TIME.zip
rm -rf tmp
echo $TIME


# Revert changes back to the original state
revert_changes
