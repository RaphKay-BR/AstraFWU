# AstraFWU
Astra device firmware upgrader for Linux

## Required
- OrbbecSDK 1.10.11 or later version
- Debian Linux ( also Ubunut ) amd64 or aarch64
- patchelf: to fix reference directory for libOrbecSDK.so.

## How to build ?
- Make a new directory.
- Download Orbbec SDK and extract into new directory.
- Clone this repository into new directory.
- So you can see ...
   ```
    +NEWDIR/OrbbecSDK_v1.10.1
           /AstraFWU
   ```
- Get into AstraFWU.
- Check Makefile to where is OrbbecSDK, you can change path in here.
- And then, just do `make`

## Deploying
- Run `script/deploy.sh` in your shell.
- You can see a new directory(folder) `deploy` and all files to run a program will be included.

