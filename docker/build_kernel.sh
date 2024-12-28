#!/bin/bash
#set -e
## Copy this script inside the kernel directory
LINKER="lld"
DIR=$(readlink -f .)
MAIN=$(readlink -f ${DIR}/..)
export PATH="$MAIN/clang/bin:$PATH"
export ARCH=arm64
export SUBARCH=arm64
export LLVM=1
export LLVM_IAS=1

wget -O clang.tar.gz https://github.com/ZyCromerZ/Clang/releases/download/16.0.6-20240430-release/Clang-16.0.6-20240430.tar.gz
mkdir -p ../clang
tar -xvzf clang.tar.gz -C ../clang
rm -rf clang.tar.gz

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

# Function to revert modifications

revert_modifications() {
  if [ "$choice" = "miui" ]; then
    if [ "$device" = "apollo" ]; then
      sed -i 's/qcom,mdss-pan-physical-width-dimension = <700>;$/qcom,mdss-pan-physical-width-dimension = <70>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-j3s-37-02-0a-dsc-video.dtsi
      sed -i 's/qcom,mdss-pan-physical-height-dimension = <1540>;$/qcom,mdss-pan-physical-height-dimension = <155>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-j3s-37-02-0a-dsc-video.dtsi
    elif [ "$device" = "alioth" ]; then
      sed -i 's/qcom,mdss-pan-physical-width-dimension = <700>;$/qcom,mdss-pan-physical-width-dimension = <70>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-k11a-38-08-0a-dsc-cmd.dtsi
      sed -i 's/qcom,mdss-pan-physical-height-dimension = <1540>;$/qcom,mdss-pan-physical-height-dimension = <155>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-k11a-38-08-0a-dsc-cmd.dtsi
    fi
  fi
  git checkout arch/arm64/boot/dts/vendor/qcom/dsi-panel-* &>/dev/null
}


echo -e "$blue***********************************************"
echo "          BUILDING KERNEL          "
echo -e "***********************************************$nocol"

# Prompt for device choice
read -p "Choose device (apollo/alioth): " device
if [ "$device" = "apollo" ]; then
  KERNEL_DEFCONFIG=apollo_defconfig
  DEVICE_NAME1="apollo"
  DEVICE_NAME2="apollon"
  IS_SLOT_DEVICE=0
  
  # Remove vendor_boot block for apollo
  VENDOR_BOOT_LINES_REMOVED=1
else
  KERNEL_DEFCONFIG=alioth_defconfig
  DEVICE_NAME1="alioth"
  DEVICE_NAME2="aliothin"
  IS_SLOT_DEVICE=1
  VENDOR_BOOT_LINES_REMOVED=0
fi

# Prompt for MIUI or AOSP
read -p "Do you want MIUI or AOSP? (miui/aosp): " choice
if [ "$choice" = "miui" ]; then
  # Modify the dimensions for MIUI
  if [ "$device" = "apollo" ]; then
    sed -i 's/qcom,mdss-pan-physical-width-dimension = <70>;$/qcom,mdss-pan-physical-width-dimension = <700>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-j3s-37-02-0a-dsc-video.dtsi
    sed -i 's/qcom,mdss-pan-physical-height-dimension = <155>;$/qcom,mdss-pan-physical-height-dimension = <1540>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-j3s-37-02-0a-dsc-video.dtsi
  elif [ "$device" = "alioth" ]; then
    sed -i 's/qcom,mdss-pan-physical-width-dimension = <70>;$/qcom,mdss-pan-physical-width-dimension = <700>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-k11a-38-08-0a-dsc-cmd.dtsi
    sed -i 's/qcom,mdss-pan-physical-height-dimension = <155>;$/qcom,mdss-pan-physical-height-dimension = <1540>;/' arch/arm64/boot/dts/vendor/qcom/dsi-panel-k11a-38-08-0a-dsc-cmd.dtsi
  fi
fi

# Backup anykernel.sh
cp -p anykernel/anykernel.sh anykernel/anykernel.sh.bak

# Modify anykernel.sh based on device and is_slot_device
sed -i "s/device.name1=.*/device.name1=$DEVICE_NAME1/" anykernel/anykernel.sh
sed -i "s/device.name2=.*/device.name2=$DEVICE_NAME2/" anykernel/anykernel.sh
sed -i "s/is_slot_device=.*/is_slot_device=$IS_SLOT_DEVICE/" anykernel/anykernel.sh

# Remove vendor_boot block if necessary
if [ "$VENDOR_BOOT_LINES_REMOVED" -eq 1 ]; then
  sed -i '/## vendor_boot shell variables/,/## end vendor_boot install/d' anykernel/anykernel.sh
fi

# kernel-Compilation

make $KERNEL_DEFCONFIG O=out CC=clang
make -j$(nproc --all) O=out \
  CC=clang \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  NM=llvm-nm \
  OBJDUMP=llvm-objdump \
  STRIP=llvm-strip \
  LD=/opt/android-ndk/android-ndk-r26b/toolchains/llvm/prebuilt/linux-x86_64/bin/ld.lld

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
cp -fp tmp/tmp.zip RealKing-$device-$choice-$TIME.zip
rm -rf tmp
echo $TIME

# Restore anykernel.sh
mv -f anykernel/anykernel.sh.bak anykernel/anykernel.sh

# Revert changes back to the original state
revert_modifications
