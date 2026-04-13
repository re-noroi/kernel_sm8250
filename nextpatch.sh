rm -rf KernelSU
rm -rf drivers/kernelsu
curl -LSs https://raw.githubusercontent.com/ReSukiSU/ReSukiSU/main/kernel/setup.sh -o /tmp/resuki_setup.sh
bash /tmp/resuki_setup.sh || { echo "ReSukiSU inject failed!"; exit 1; }
