**Arduino library deps**

- PID - https://playground.arduino.cc/Code/PIDLibrary
- U8g2
- PacketSerial

Search them with libray manager and install.



**nodejs script installatoin**

- need node >= 8, please refer to 

```shell
# Using Ubuntu
curl -sL https://deb.nodesource.com/setup_8.x | sudo -E bash -
sudo apt-get install -y nodejs

# Using Debian, as root
curl -sL https://deb.nodesource.com/setup_8.x | bash -
apt-get install -y nodejs
```

- npm install
- node index.js
- add it to startup manager, e.g. supervisor