# Installing

## How to build libartnet from source

```sh
sudo apt-get update
sudo apt-get install git libtool pkg-config
git clone https://github.com/Quphoria/libartnet
cd libartnet
autoreconf -i
./configure
make
sudo make install
```

### Check LD_LIBRARY_PATH is set

```sh
echo $LD_LIBRARY_PATH
# if this is empty, set the default value with the below commands
LD_LIBRARY_PATH=/usr/local/lib
export LD_LIBRARY_PATH
```

## Install libftdi

```sh
sudo apt-get update
sudo apt-get install libftdi-dev
```

## Install ArtnetOpenRDMNode

```sh
git clone https://github.com/Quphoria/ArtnetOpenRDMNode
cd ArtnetOpenRDMNode
autoreconf -i
./configure
make
sudo make install
```