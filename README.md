# NodeMCU-Wixel-Arduino
NodeMCU Wixel - Arduino C Version

This application is based on the NodeMCU-Wixel code written by MrPsi https://github.com/MrPsi/NodeMCU-Wixel
It has been ported from LUA into Arduino, so it was easier to modify and understand!

* This version is capable of buffering 200 (or more/less) values, so if any client such as xDrip requires old data, it capable of providing it.
 * This version allows you to use DHCP or Static IP, and also allows you to easily define which 802.11 mode you want to use, based on your modem requirements, or the distance the signal needs to go. 802.11b will go further than 802.11n for example.
 * This was based on using the NodeMCU, however other versions of the ESP8266 microcontrollers could be used.
 * This version uses TX from the Wixel, into the RX of the NodeMCU - it does not need TX out of the NodeMCU. 
 * This version uses the standard Serial port, not Serial2 (Swapped Serial), and without TX being used the USB is free to be used for Debugging.
 * This can also be run on a 4D Systems gen4-IoD display module, so the wixel is wired to that, so the gen4-IoD acts in place of the NodeMCU, but also provides a display if information is wanting to be displayed. This will be expanded on in the future, so BG readings can be displayed. Possibly pulling the filtered/calibrated values via Nighscout, or incorporating the algorithm into here directly.
 * MrPsi's Monitor application for seeing which Nodes are online, and when they got the last data, works on this version also. When the Monitor requests information, it requests 200 values, and it also requests another parameter from the NodeMCU, which is Uptime. Requests from the monitor vs requests from xDrip, are easily discernible, and additional functionality can be added if required.
 
## Acknowledgements
 
Big THANK YOU to Peter - Mr PSI, for doing the original LUA code which works very well, and also to Paul Clements for helping me port this code over and getting it working.

## Requirements

This is designed to be compiled in the Arduino IDE, using the ESP8266 JSON board addition which is available from: http://arduino.esp8266.com/stable/package_esp8266com_index.json and also requires the Ticker library, which comes with it https://github.com/esp8266/Arduino/tree/master/libraries/Ticker

### NOTES / DISCLAIMER

No responsibility. 
Use at your own Risk. 
Work in Progress. 
Please contribute! 
Please find bugs!
Enjoy!
