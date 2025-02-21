rm -rf KernelSU-Next
curl -LSs "https://raw.githubusercontent.com/rifsxd/KernelSU-Next/next/kernel/setup.sh" | bash -s next
cp -rf "KernelSU-Next-Implement-SUSFS-v1.5.5-Universal.patch" KernelSU-Next && cd ./KernelSU-Next && patch -p1 -f < "KernelSU-Next-Implement-SUSFS-v1.5.5-Universal.patch" && cd ..