#!/bin/bash

TEST_PATCHELF=`which patchelf`
ORBBEC_SO_DIR_S=`grep LIBORBBECSDK_PATH Makefile`
ORBBEC_SO_DIR_A=( $ORBBEC_SO_DIR_S )
ORBBEC_DIR_S=${ORBBEC_SO_DIR_A[0]}
export $ORBBEC_DIR_S

SRC=astrafwu
SRC_BIN=bin/$SRC
DST_PATH=deploy

if [[ "$TEST_PATCHELF" == "" ]];then
    echo -e "\033[31mError: patchelf required.\033[0m"
    exit 1
fi

if [ ! -e $ORBBEC_DIR ];then
    echo -e "\033[31mError: $ORBBEC_DIR not found.\033[0m"
    exit 1
fi

if [ ! -e $SRC_BIN ];then
    echo -e "\033[31mError: $SRC_BIN not found.\033[0m"
    exit 1
fi

if [ ! -e $DST_PATH ];then
    mkdir -p $DST_PATH
fi

if [ ! -e $DST_PATH ];then
    echo -e "\033[31mError: deploying directory access failure.\033[0m"
    exit 1
fi

echo -en "\033[34mCopying ELF image ... \033[0m"
cp -rf $SRC_BIN $DST_PATH > /dev/null
echo -e "\033[36mDone\033[0m"

echo -en "\033[34mCopying Orbbec SO images ... \033[0m"
cp -rf $LIBORBBECSDK_PATH/lib/* $DST_PATH > /dev/null
echo -e "\033[36mDone\033[0m"

echo -en "\033[34mPatching ELF reference path ... \033[0m"
cd $DST_PATH
patchelf --set-rpath ./ $SRC
cd ..
echo -e "\033[36mDone\033[0m"

exit 0
