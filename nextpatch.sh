rm -rf KernelSU-Next
curl -LSs "https://raw.githubusercontent.com/rifsxd/KernelSU-Next/next/kernel/setup.sh" | bash -s v1.0.9
echo "Adding more manager support..."
cp -rf "ksun_more_manager.patch" KernelSU-Next && cd ./KernelSU-Next && patch -p1 -f < "ksun_more_manager.patch" && cd ..
echo "Done (KSU, MKSU, RKSU, Fork by backslashxx and SukiSU)"