#!/bin/bash

NO_FORMAT="\033[0m"
C_GREEN="\033[38;5;2m"

echo -e "${C_GREEN}Start uClibc configuration editor${NO_FORMAT}"
make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br uclibc-menuconfig
echo -e "${C_GREEN}Save uClibc configuration${NO_FORMAT}"
make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br uclibc-update-config
