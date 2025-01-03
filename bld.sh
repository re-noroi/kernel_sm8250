export ARCH=arm64
export SUBARCH=ARM64
export KBUILD_BUILD_USER="aliza"
export KBUILD_BUILD_HOST="aliza"
export PATH="/home/aliza/clang/bin:$PATH"
export LD_LIBRARY_PATH="~/clang/lib64:$LD_LIBRARY_PATH}"
rm -rf out
make O=out vendor/alioth_defconfig PATH="~/clang/bin:$PATH"
make O=out CC=clang -j$(nproc --all) PATH="~/clang/bin:$PATH" CROSS_COMPILE=aarch64-linux-gnu- LLVM=1 LLVM_IAS=1
