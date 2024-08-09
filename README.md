# AstraFWU
Astra device firmware upgrader for Windows (MinGW-W64)

## Required
- OrbbecSDK 1.10.11 or later version
- MinGW-W64

## M$VC issue
- OrbbecSDK C++ class has `_imp__XXYYZZ` symbol issues.
- MinGW-W64 g++ not able to find some M$VC styled implementataton symbols.
- AstraFWU updated for using C styled method of Orbbec, but atually it is not standard of C, Orbbec seems don't understand what is C or `stdcall`, it's really poor.

## Recommend to use 
- MSYS2, not WSL2 - you can't access USB device through WSL2.

## How to build ?
- Make a new directory or folder as M$ style.
- Download Orbbec SDK and extract into new directory.
- Clone this repository into new directory.
- So you can see ...
   ```
    +NEWDIR\OrbbecSDK_v1.10.11
            \AstraFWU
   ```
- Get into AstraFWU.
- Check Makefile to where is OrbbecSDK, you can change path in here.
- And then, just do `make`

## Deploying
- M$ Windows is not works like POSIX.
- Run `script/deploy.sh` in your MSYS2 - yes cmd is not an option.
- You can see a new directory(folder) `deploy` and all files to run a program will be included.

