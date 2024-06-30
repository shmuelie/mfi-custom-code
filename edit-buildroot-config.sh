#!/bin/sh

make -C buildroot BR2_EXTERNAL=../br2 O=../build/br nconfig
