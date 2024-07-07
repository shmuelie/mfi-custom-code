#!/bin/bash

NO_FORMAT="\033[0m"
C_GREEN="\033[38;5;2m"

echo -e "${C_GREEN}Load buildroot configuration${NO_FORMAT}"
make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br mfi_defconfig
echo -e "${C_GREEN}Start buildroot configuration editor${NO_FORMAT}"
make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br nconfig
echo -e "${C_GREEN}Save buildroot configuration${NO_FORMAT}"
make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br savedefconfig
