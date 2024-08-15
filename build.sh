 #!/usr/bin/env bash

 #
 # Script For Building Android Kernel
 #

##----------------------------------------------------------##
# Specify Kernel Directory
KERNEL_DIR=$(pwd)

# Kernel defconfig
DEFCONFIG=vendor/meteoric_defconfig

# AnyKernel3 directory
ANYKERNEL3_DIR=$KERNEL_DIR/anykernel

# Compiler cleanup
COMPILER_CLEANUP=true

# Cleanup
CLEANUP=true

# Release Repo
RELEASE_REMOTE=$(git remote)
RELEASE_REPO=$(git ls-remote --get-url $RELEASE_REMOTE)

# Files
IMAGE=$KERNEL_DIR/out/arch/arm64/boot/Image
DTBO=$KERNEL_DIR/out/arch/arm64/boot/dtbo.img
DTB=$KERNEL_DIR/out/arch/arm64/boot/dtb

# Verbose Build
VERBOSE=0

# Kernel Version
KERVER=$(make kernelversion)

# Specify Final Zip Name
ZIPNAME=Meteoric

# Zip version
VERSION=$(cat $KERNEL_DIR/Version)

# Specify compiler
COMPILER=neutron

# Sigint detection
SIGINT_DETECT=0

# Start build
BUILD_START=$(date +"%s")
nocol='\033[0m'
red='\033[0;31m'
green='\033[0;32m'
orange='\033[0;33m'
blue='\033[0;34m'
magenta='\033[0;35m'
cyan='\033[0;36m'

# Startup
echo -e "$cyan***********************************************"
echo    "              STARTING THE ENGINE              "
echo -e "***********************************************$nocol"

##----------------------------------------------------------##
# Clone ToolChain
function cloneTC() {
    case $COMPILER in
        proton)
            if [ $COMPILER_CLEANUP = true ]; then
                rm -rf ~/meteoric/neutron-clang
            fi
            if [ $(ls $HOME/meteoric/proton-clang 2>/dev/null | wc -l) -ne 0 ]; then
                PATH="$HOME/meteoric/proton-clang/bin:$PATH"
            else
                git clone --depth=1  https://github.com/kdrag0n/proton-clang.git ~/meteoric/proton-clang
                PATH="$HOME/meteoric/proton-clang/bin:$PATH"
            fi
            ;;
        neutron)
            if [ $COMPILER_CLEANUP = true ]; then
                rm -rf ~/meteoric/proton-clang
            fi
            if [ $(ls $HOME/meteoric/neutron-clang/bin 2>/dev/null | wc -l ) -ne 0 ] && 
               [ $(find $HOME/meteoric/neutron-clang -name *.tar.zst | wc -l) -eq 0 ]; then
                PATH="$HOME/meteoric/neutron-clang/bin:$PATH"
            else
                rm -rf ~/meteoric/neutron-clang
                mkdir -p ~/meteoric/neutron-clang
                cd ~/meteoric/neutron-clang || exit
                curl -LO "https://raw.githubusercontent.com/Neutron-Toolchains/antman/main/antman"
                chmod a+x antman
                ./antman -S
                cd - || exit
                PATH="$HOME/meteoric/neutron-clang/bin:$PATH"
            fi
            ;;
    esac
}
	
##------------------------------------------------------##
# Export Variables
function exports() {
    # Export KBUILD_COMPILER_STRING
    export KBUILD_COMPILER_STRING=$($HOME/meteoric/$COMPILER-clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')

    # Export ARCH and SUBARCH
    export ARCH=arm64
    export SUBARCH=arm64

    # Export KBUILD HOST and USER
    export KBUILD_BUILD_HOST=Neoteric
    export KBUILD_BUILD_USER=HELLBOY017

    # Export PROCS and DISTRO
    export PROCS=$(nproc --all)
    export DISTRO=$(source /etc/os-release && echo "$NAME")
}

##----------------------------------------------------------##
# Sigint handler
function sigint() {
    SIGINT_DETECT=1
}

##----------------------------------------------------------##
# Compilation choices
function choices() {
    echo -e "$green***********************************************"
    echo    "                BUILDING KERNEL                "
    echo -e "***********************************************$nocol"

    # KernelSU
    read -p "Include KernelSU? If unsure, say N. (Y/N) " KSU_RESP 
    case $KSU_RESP in
        [yY] )
            if [ $(ls $KERNEL_DIR/KernelSU 2>/dev/null | wc -l) -eq 0 ]; then
                rm -rf $KERNEL_DIR/KernelSU
                git submodule update --init --recursive KernelSU
            else
            	ZIPNAME=Meteoric-KernelSU
            	KSU_CONFIG=ksu.config
            	if [ $(grep -c "KSU" arch/arm64/configs/$DEFCONFIG) -eq 0 ]; then
                    sed -i "s/-Meteoric/-Meteoric-$VERSION-KSU/" arch/arm64/configs/$DEFCONFIG
            	fi
            fi
            ;;
         *)
            if [ $(grep -c $VERSION arch/arm64/configs/$DEFCONFIG) -eq 0 ]; then
                sed -i "s/-Meteoric/-Meteoric-$VERSION/" arch/arm64/configs/$DEFCONFIG
            fi
            ;;
    esac

    # Clean build
    read -p "Do you want to do a clean build? If unsure, say N. (Y/N) " CLEAN_RESP 
    case $CLEAN_RESP in
        [yY] )
            make O=out clean && make O=out mrproper
            ;;
    esac
    
    # Interrupt detected
    if [ $SIGINT_DETECT -eq 1 ]; then
        if [ $(grep -c "KSU" arch/arm64/configs/$DEFCONFIG) -ne 0 ]; then
            sed -i "s/-Meteoric-$VERSION-KSU/-Meteoric/" arch/arm64/configs/$DEFCONFIG
        elif [ $(grep -c $VERSION arch/arm64/configs/$DEFCONFIG) -ne 0 ]; then
            sed -i "s/-Meteoric-$VERSION/-Meteoric/" arch/arm64/configs/$DEFCONFIG
        fi
        exit
    fi
}

##----------------------------------------------------------##
# Compilation process
function compile() {
    # Make kernel	
    make O=out CC=clang ARCH=arm64 $DEFCONFIG $KSU_CONFIG savedefconfig
    make -kj$(nproc --all) O=out \
    ARCH=arm64 \
    LLVM=1 \
    LLVM_IAS=1 \
    CLANG_TRIPLE=aarch64-linux-gnu- \
    CROSS_COMPILE=aarch64-linux-android- \
    CROSS_COMPILE_COMPAT=arm-linux-androideabi- \
    AR=llvm-ar \
    NM=llvm-nm \
    OBJCOPY=llvm-objcopy \
    OBJDUMP=llvm-objdump \
    STRIP=llvm-strip \
    V=$VERBOSE 2>&1 | tee out/error.log

    # KernelSU
    if [ $ZIPNAME = Meteoric-KernelSU ]; then
        sed -i 's/CONFIG_KSU=y/# CONFIG_KSU is not set/g' out/.config
        sed -i '/CONFIG_KSU=y/d' out/defconfig
        sed -i "s/-Meteoric-$VERSION-KSU/-Meteoric/" out/defconfig out/.config arch/arm64/configs/$DEFCONFIG
        
        if [ $(grep -c "# KernelSU" arch/arm64/configs/$DEFCONFIG) -eq 1 ]; then
            sed -i 's/CONFIG_KSU=y/# CONFIG_KSU is not set/g' arch/arm64/configs/$DEFCONFIG
        else   
            sed -i '/CONFIG_KSU=y/d' arch/arm64/configs/$DEFCONFIG
        fi
    else
        sed -i "s/-Meteoric-$VERSION/-Meteoric/" out/defconfig out/.config arch/arm64/configs/$DEFCONFIG
    fi

    # Verify build
    if [ $(grep -c "Error 2" out/error.log) -ne 0 ] || [ $SIGINT_DETECT -eq 1 ]; then 
        echo ""
        echo -e "$red***********************************************"
        echo    "           KERNEL COMPILATION FAILED           "
        echo -e "***********************************************$nocol"
        exit 1
    else
        echo -e "$green***********************************************"
        echo    "          KERNEL COMPILATION FINISHED          "
        echo -e "***********************************************$nocol"  
    fi
}
##----------------------------------------------------------##

function zipping() {
    # Copying kernel essentials
    cp $IMAGE $DTBO $DTB $ANYKERNEL3_DIR

    echo -e "$magenta***********************************************"
    echo    "                Time to zip up!                "
    echo -e "***********************************************$nocol"

    # Cleanup
    if [ $CLEANUP = true ]; then
        rm -rf out/*.zip
    fi

    # Make zip and transfer it to out directory
    cd $ANYKERNEL3_DIR/
    FINAL_ZIP=$ZIPNAME-$VERSION-$(date +%y%m%d-%H%M).zip
    zip -r9 "../out/$FINAL_ZIP" * -x README $FINAL_ZIP

    # Clean AnyKernel3 directory
    cd ..
    rm -rf $ANYKERNEL3_DIR/Image $ANYKERNEL3_DIR/Image.* $ANYKERNEL3_DIR/dtbo.img $ANYKERNEL3_DIR/dtb

    if [ $(find out/ -name $FINAL_ZIP | wc -l) -ne 0 ]; then
        echo -e "$orange***********************************************"
        echo    "            Done, here is your sha1            "
        echo -e "***********************************************$nocol"
        sha1sum out/$FINAL_ZIP

        # Github release
        read -p "Do you want to do a github release? If unsure, say N. (Y/N) " GIT_RESP 
        case $GIT_RESP in
            [yY] )
                gh release create $VERSION out/$FINAL_ZIP --repo $RELEASE_REPO --title Meteoric-$VERSION
                ;;
            *)
                read -p "Do you want to upload files to the current github release? If unsure, say N. (Y/N) " UPLOAD_RESP 
                case $UPLOAD_RESP in
                    [yY] )
                        gh release upload $VERSION out/$FINAL_ZIP --repo $RELEASE_REPO
                        ;;
                esac
                ;;
        esac

        echo -e "$cyan***********************************************"
        echo    "                  All done !!                  "
        echo -e "***********************************************$nocol"
    else
        echo ""
        echo -e "$red***********************************************"
        echo    "                ZIPPING FAILED!                "
        echo -e "***********************************************$nocol"
        exit 1
    fi
}

##----------------------------------------------------------##
cloneTC
exports
trap sigint SIGINT
choices
compile
trap - SIGINT
BUILD_END=$(date +"%s")
DIFF=$(($BUILD_END - $BUILD_START))
echo -e "$blue Build completed in $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) seconds.$nocol"
if [ $(ls $ANYKERNEL3_DIR 2>/dev/null | wc -l) -ne 0 ]; then
    zipping
else
    echo -e "$red#############################################################################################"
    echo "# IN ORDER TO MAKE A KERNEL ZIP MAKE SURE THAT ANYKERNEL3 IS ADDED IN THE CORRECT DIRECTORY #"
    echo "#               REFER TO THE ANYKERNEL3_DIR VARIABLE FOR THE CORRECT DIRECTORY              #"
    echo -e "#############################################################################################$nocol"
    exit 1
fi
##----------------------------------------------------------##
