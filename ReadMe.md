Fan-control 
==============

A tool to control fan speed by temperature automatically for ROCK5B.

Features
--------------
1. Control fan speed by temperature.
2. set fan speed manually

Build & install
==============
```shell
make package
dpkg -i fan-control*.deb
```

Usage
==============
```shell
systemctl enable fan-control
systemctl start fan-control
```
  
License
===============
MIT License


