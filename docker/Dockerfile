# Use Ubuntu 22.04 as the base image
FROM ubuntu:22.04

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive

# Install necessary packages
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl wget git build-essential zip ccache bc bison flex \
    libssl-dev libncurses5-dev libncursesw5-dev zlib1g-dev p7zip-full \
    python3 python3-pip python3-venv ca-certificates \
    clang lld gcc-aarch64-linux-gnu gcc-arm-linux-gnueabi g++-arm-linux-gnueabi \
    g++-aarch64-linux-gnu file jq perl tar sudo unzip && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

# Set up Android NDK
ENV ANDROID_NDK=/opt/android-ndk/android-ndk-r26b
ENV ANDROID_CLANG_PATH=$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64
ENV PATH=$ANDROID_CLANG_PATH/bin:$PATH
ENV ARCH=arm64
ENV SUBARCH=arm64
ENV CROSS_COMPILE=$ANDROID_CLANG_PATH/bin/aarch64-linux-android-
ENV CROSS_COMPILE_ARM32=$ANDROID_CLANG_PATH/bin/arm-linux-androideabi-
ENV CC=$ANDROID_CLANG_PATH/bin/aarch64-linux-android34-clang
ENV CLANG_TRIPLE=aarch64-linux-gnu-
ENV AR=$ANDROID_CLANG_PATH/bin/llvm-ar
ENV AS=$ANDROID_CLANG_PATH/bin/llvm-as
ENV NM=$ANDROID_CLANG_PATH/bin/llvm-nm
ENV OBJCOPY=$ANDROID_CLANG_PATH/bin/llvm-objcopy
ENV OBJDUMP=$ANDROID_CLANG_PATH/bin/llvm-objdump
ENV STRIP=$ANDROID_CLANG_PATH/bin/llvm-strip
ENV LD=$ANDROID_CLANG_PATH/bin/ld.lld

# Download Android NDK
RUN mkdir -p /opt/android-ndk && \
    curl -Lo android-ndk.zip https://dl.google.com/android/repository/android-ndk-r26b-linux.zip && \
    unzip android-ndk.zip -d /opt/android-ndk && \
    rm -f android-ndk.zip

# Clone the kernel repository
WORKDIR /kernel
RUN git clone https://github.com/Rohail33/Realking_kernel_sm8250.git .
RUN git submodule init && git submodule update

RUN sed -i '/gcc-wrapper/d' Makefile

# Add build script
COPY build_kernel.sh /kernel/
RUN chmod +x /kernel/build_kernel.sh

# Entry point to make building easy
CMD ["bash"]

