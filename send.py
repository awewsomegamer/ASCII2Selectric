import sys
import serial
import time

XON = chr(0x11)
XOFF = chr(0x13)

port = serial.Serial(
        port="/dev/ttyACM0",
        baudrate=1200,
)

if (port.is_open == False):
    print("Port not open")

time.sleep(2)

xon_xoff = 0; # XOFF

with open(sys.argv[1], "rb") as f:
    byte = f.read(1)
    port_val = 0
    tab = 0

    while byte != b"" or port_val != b"":
        port_val = port.read(1).decode("utf-8")[0]

        if port_val == XOFF:
            xon_xoff = 0
        elif port_val == XON:
            xon_xoff = 1
        else:
            print(port_val, end='')
        
        if byte.decode("utf-8") == '\t' and tab == 0:
            byte = f.read(1)
            tab = 8

        if (xon_xoff):
            if tab > 0:
                port.write(b' ')
                tab = tab - 1
                continue

            port.write(byte)
            byte = f.read(1)


