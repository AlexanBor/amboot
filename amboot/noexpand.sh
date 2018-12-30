#!/bin/bash
HDD=/dev/sdb
PART=${HDD}2
IMAGESCOUNT=2
FILEDIR=/var/lib/armbian
FILEFLAG=${FILEDIR}/resize_second_stage

temp_dir=$(mktemp -d)

for IMAGE in `seq 1 ${IMAGESCOUNT}` ; do
  amboot s ${HDD} ${IMAGE}
  mount ${PART} $temp_dir
  mkdir ${temp_dir}${FILEDIR}
  touch ${temp_dir}${FILEFLAG}
  ls -l ${temp_dir}${FILEFLAG}
  umount ${temp_dir}
done

rm -R $temp_dir
