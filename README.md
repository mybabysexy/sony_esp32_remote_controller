# Sony Camera Bluetooth Remote

This project is inspired by coral's [freemote](https://github.com/coral/freemote) project, which implements sony BLE remote on NRF52840.
I wanted to do the same thing using ESP32 with my ZV-E10.

I am using ESP32C3 Super Mini, and only needs focusing and triggering photo.

### Flow

1. Clone this repo
2. Change or add your models to `cameraModels` variable
3. Change the button pins, currently GPIO20 and GPIO21
4. Upload the sketch
5. On your camera, turn on Bluetooth Remote Control, turn on Bluetooth and enter Pairing mode
6. ESP32 should start scanning and binding with the camera, accept connection on your camera
7. Happy tinkering!

## Additional Info
Thanks to the following posts:
- [BLE codes](https://gregleeds.com/reverse-engineering-sony-camera-bluetooth/)
- [BLE client setup](https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLETests/Arduino/BLE_client/BLE_client.ino)
- [BLE pairing with security](https://www.esp32.com/viewtopic.php?t=10257)

## Contribution
I don't really know what I'm doing, but it works! If you have any suggestions or improvement, please let me know!
