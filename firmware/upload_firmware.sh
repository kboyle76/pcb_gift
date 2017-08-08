avrdude -C ./avrdude.conf -pattiny44 -cstk500v1 -P/dev/cu.usbmodem1411 -b19200 -Ulfuse:w:0xe2:m
avrdude -C ./avrdude.conf -pattiny44 -cstk500v1 -P/dev/cu.usbmodem1411 -b19200 -Uhfuse:w:0xdf:m
avrdude -C ./avrdude.conf -pattiny44 -cstk500v1 -P/dev/cu.usbmodem1411 -b19200 -U flash:w:main.elf
