# basic_serial
Basic (and unfinished) serial driver [BeagleBone Black Wireless]

1) More or less inspired by...
- linux kernel labs (Bootlin)
- linux serial drivers (Bootlin)
- drivers/tty/serial/atmel_serial.c
- drivers/tty/serial/8250/8250_port.c

2) Userspace

insmod serial_jp.ko

echo toto > /dev/ttyJP0

stty -F /dev/ttyJP0 raw [-echo -echoe -echok -echoctl -echoke]
cat /dev/ttyJP0

rmmod serial_jp.ko
