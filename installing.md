# Installing

## How to build libartnet from source

```sh
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

```
sudo apt-get update
sudo apt-get install libftdi-dev
```