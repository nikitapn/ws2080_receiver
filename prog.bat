avrdude -p m8 -P COM3 -c avr109 -b 19200 -U flash:w:build/ws_receiver.hex:i