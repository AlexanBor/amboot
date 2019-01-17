# amboot
Write armbian boot images for amlogic S9xxx TV-boxes to removable drive. This utility writes to disk under root account so it may be destructive.

Images are provided and discussed at
https://forum.armbian.com/topic/2419-armbian-for-amlogic-s905-and-s905x-ver-544/ and
https://forum.armbian.com/topic/7930-armbian-for-amlogic-s9xxx-kernel-41x-ver-555/

Each of these images tends to occupy all space on USB flash drive or memory card.
This utility allows to write some images to USB drive and (relatively easily) choose which one use to boot TV-box.

# Usage
Compile program and put it on destination device. Program was compiled and tested in termux environment on TV-box, in armbian environments on TV-box and in x86 Linux environment.

Fetch and unpack required images to any system with this utility. Edit supplied files list.txt and noexpand.sh.

Run utility in preview mode to check space usage, drive availability etc. Then run in build mode.

Run noexpand.sh script to instruct OSes in images written to not expand partitions. This step is mandatory, otherwise each image on its first boot will expand itself to whole drive and thus will destroy its neighbors.

Now you can attach drive to TV-box and boot it. Boot process explained in https://github.com/150balbes/Amlogic_s905/wiki/s905_multi_boot.

Run utility to choose boot image in select mode.

# Assumptions
Image consists of two partitions: boot and root. Flag for OS to not mangle partitions on first boot is file /var/lib/armbian/resize_second_stage.
