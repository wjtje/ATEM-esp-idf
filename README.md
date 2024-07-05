# ATEM Communication for ESP-IDF

> **THIS CODE COMES WITH ABSOLUTELY NO WARRANTY*

## Notice

ATEM, ATEM CONTROL and all other references to Blackmagic Design are trademarks of Blackmagic Design Pty. Ltd. 

This code is based on the reverse engineering of the ATEM protocol by SKAARHOJ ([source](https://web.archive.org/web/20221007194524/https://www.skaarhoj.com/discover/blackmagic-atem-switcher-protocol)). [CC BY-SA 2.0]

This list contains other notable projects that where used during the making of the library.

- [~martijnbraam/pyatem](https://git.sr.ht/~martijnbraam/pyatem)
- [peschuster/wireshark-atem-dissector](https://github.com/peschuster/wireshark-atem-dissector)

## How can I use this?

This code is designed for an ESP32 with an LAN8720 chip, but it should work on just a normal ESP32. There is a `linux-port` branch on this repo, its a modified version of this code that can be compiled and run on _any_ linux device.

There are two examples inside the example directory:
 - basic-preview-switcher
 - atem-console

You can use doxygen to generate documentation, just run `doxygen Doxyfile`.

## License

The MIT License (MIT) - Copyright (c) 2023 Wouter van der Wal
