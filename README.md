[![Build Status](https://github.com/DigitalPrice/DigitalPrice/workflows/DigitalPrice%20auto%20release/badge.svg)](https://github.com/DigitalPrice/DigitalPrice/actions)
[![Version](https://img.shields.io/github/v/release/DigitalPrice/DigitalPrice)](https://github.com/DigitalPrice/DigitalPrice/releases)
[![Issues](https://img.shields.io/github/issues-raw/DigitalPrice/DigitalPrice)](https://github.com/DigitalPrice/DigitalPrice/issues)
[![PRs](https://img.shields.io/github/issues-pr-closed/DigitalPrice/DigitalPrice)](https://github.com/DigitalPrice/DigitalPrice/pulls)
[![Commits](https://img.shields.io/github/commit-activity/y/DigitalPrice/DigitalPrice)](https://github.com/DigitalPrice/DigitalPrice/commits/dev)
[![Contributors](https://img.shields.io/github/contributors/DigitalPrice/DigitalPrice)](https://github.com/DigitalPrice/DigitalPrice/graphs/contributors)
[![Last Commit](https://img.shields.io/github/last-commit/DigitalPrice/DigitalPrice)](https://github.com/DigitalPrice/DigitalPrice/graphs/commit-activity)

[![gitstars](https://img.shields.io/github/stars/DigitalPrice/DigitalPrice?style=social)](https://github.com/DigitalPrice/DigitalPrice/stargazers)
[![twitter](https://img.shields.io/twitter/follow/digitalpriceorg?style=social)](https://twitter.com/digitalpriceorg)
[![discord](https://img.shields.io/discord/364887984501161994)](https://discord.gg/cZznuMG)

---
![DigitalPrice Logo](https://github.com/DigitalPrice/DP-Wallet/blob/static/src/qt/res/icons/komodo.png "DigitalPrice Logo")


## Komodo

This is the official DigitalPrice sourcecode repository based on https://github.com/KomodoPlatform/komodo.

## Development Resources

- Whitepaper: [DigitalPrice Whitepaper](https://github.com/DigitalPrice/Documents-and-Scripts/blob/master/Documents/DigitalPrice_Whitepaper_The_Real_Thing.pdf)
- DigitalPrice Website: [https://digitalprice.org](https://digitalprice.org/)
- DigitalPrice Blockexplorer: [https://dp.explorer.dexstats.info](https://dp.explorer.dexstats.info/)
- Knowledgebase & How-to: [https://support.komodoplatform.com/en/support/solutions](https://support.komodoplatform.com/en/support/solutions)
- API references & Dev Documentation: [https://developers.komodoplatform.com](https://developers.komodoplatform.com/)


## Tech Specification
- Max Supply: 100 million DP (reached at block 6884282)
- Block Time: 120 seconds
- Block Reward: 6.46 DP
- Mining Algorithm: Equihash

## About this Project
DigitalPrice is built on Komodo technology and uses a dual mode of PoS/PoW for block creation, offering the best of both worlds.
Using Komodo's runtime fork technology allows DigitalPrice to both be an independent chain (with its own advancements) and "lean on" Komodo development at the same time.

## Getting started

### Dependencies

```shell
#The following packages are needed:
sudo apt-get install build-essential pkg-config libc6-dev m4 g++-multilib autoconf libtool ncurses-dev unzip git python python-zmq zlib1g-dev wget libcurl4-gnutls-dev bsdmainutils automake curl libsodium-dev
```

### Build DigitalPrice

This software is based on zcash and considered experimental and is continously undergoing heavy development.

The dev branch is considered the bleeding edge codebase while the master-branch is considered tested (unit tests, runtime tests, functionality). At no point of time do the DigitalPrice developers take any responsbility for any damage out of the usage of this software.
DigitalPrice builds for all operating systems out of the same codebase. Follow the OS specific instructions from below.

#### Linux
```shell
git clone https://github.com/DigitalPrice/DigitalPrice --branch master --single-branch
cd DigitalPrice
./zcutil/fetch-params.sh
./zcutil/build.sh -j$(expr $(nproc) - 1)
#This can take some time.
```


#### OSX
Ensure you have [brew](https://brew.sh) and Command Line Tools installed.
```shell
# Install brew
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
# Install Xcode, opens a pop-up window to install CLT without installing the entire Xcode package
xcode-select --install
# Update brew and install dependencies
brew update
brew upgrade
brew tap discoteq/discoteq; brew install flock
brew install autoconf autogen automake
brew update && brew install gcc@8
brew install binutils
brew install protobuf
brew install coreutils
brew install wget
# Clone the DigitalPrice repo
git clone https://github.com/DigitalPrice/DigitalPrice --branch master --single-branch
# Change master branch to other branch you wish to compile
cd DigitalPrice
./zcutil/fetch-params.sh
./zcutil/build-mac.sh -j$(expr $(sysctl -n hw.ncpu) - 1)
# This can take some time.
```

#### Windows
Use a debian cross-compilation setup with mingw for windows and run:
```shell
sudo apt-get install build-essential pkg-config libc6-dev m4 g++-multilib autoconf libtool ncurses-dev unzip git python python-zmq zlib1g-dev wget libcurl4-gnutls-dev bsdmainutils automake curl cmake mingw-w64 libsodium-dev libevent-dev
curl https://sh.rustup.rs -sSf | sh
source $HOME/.cargo/env
rustup target add x86_64-pc-windows-gnu

sudo update-alternatives --config x86_64-w64-mingw32-gcc
# (configure to use POSIX variant)
sudo update-alternatives --config x86_64-w64-mingw32-g++
# (configure to use POSIX variant)

git clone https://github.com/DigitalPrice/DigitalPrice --branch master --single-branch
cd DigitalPrice
./zcutil/fetch-params.sh
./zcutil/build-win.sh -j$(expr $(nproc) - 1)
#This can take some time.
```
**DigitalPrice is experimental and a work-in-progress.** Use at your own risk.

To reset the DigitalPrice blockchain change into the *~/.komodo/DP* data directory and delete the corresponding files by running `rm -rf blocks chainstate debug.log komodostate db.log`

#### Create DP.conf (auto-created under most circumstances)

Create a DP.conf file:

```
mkdir ~/.komodo/DP
cd ~/.komodo/DP
touch DP.conf

#Add the following lines to the komodo.conf file:
rpcuser=yourrpcusername
rpcpassword=yoursecurerpcpassword
rpcbind=127.0.0.1
txindex=1

```

**DigitalPrice is based on Komodo which is unfinished and highly experimental.** Use at your own risk.

License
-------
For license information see the file [COPYING](../../tree/master/LEGAL/COPYING).

---


Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
