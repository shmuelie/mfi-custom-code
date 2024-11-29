#!/bin/bash

make -C ./buildroot/ BR2_EXTERNAL=../br2 O=../build/br uclibc-menuconfig
