#!/bin/bash

make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br mfi_defconfig
make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br nconfig
make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br savedefconfig
