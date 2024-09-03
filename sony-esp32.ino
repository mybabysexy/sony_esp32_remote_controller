/*
   Sony Camera BLE control using ESP32
   I don't really know what I'm doing, but it works!
   If you have any suggestions or improvement, please let me know!
   I used ESP32C3 Super Mini and Sony ZV-E10 for this project.

   Sources:
    BLE client setup:
    https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLETests/Arduino/BLE_client/BLE_client.ino

    BLE pairing with security:
    https://www.esp32.com/viewtopic.php?t=10257

    BLE codes:
    https://gregleeds.com/reverse-engineering-sony-camera-bluetooth/
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

int scanTime = 5; // In seconds
int actionGapTime = 250; // In milliseconds, typical debounce time
int recoverTime = 1000; // In milliseconds, time to wait before next action/loop
int triggerPin = 20;
int shutterPin = 21;

// todo: add more camera models
int cameraModelsSize = 1;
String cameraModels[] = {
  "ZV-E10",
};

static BLEScan *pBLEScan;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEClient *pClient;
static BLEAddress *pServerAddress;

bool doConnect = false;
bool paired = false;
bool connected = false;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    // get name
    String name = advertisedDevice.getName().c_str();

    // check if the device is a camera in cameraModels list
    for (int i = 0; i < cameraModelsSize; i++)
    {
      if (name.indexOf(cameraModels[i]) != -1)
      {
        pServerAddress = new BLEAddress(advertisedDevice.getAddress());

        // stop scan
        pBLEScan->stop();
        Serial.println("Device found!");

        doConnect = true;
      }
    }
  }
};

class MySecurity : public BLESecurityCallbacks
{

  bool onConfirmPIN(uint32_t pin)
  {
    Serial.println("onConfirmPIN");
    return true;
  }

  uint32_t onPassKeyRequest()
  {
    Serial.println("onPassKeyRequest");
    ESP_LOGI(LOG_TAG, "PassKeyRequest");
    return 133700;
  }

  void onPassKeyNotify(uint32_t pass_key)
  {
    Serial.println("onPassKeyNotify");
    ESP_LOGI(LOG_TAG, "On passkey Notify number:%d", pass_key);
  }

  bool onSecurityRequest()
  {
    Serial.println("onSecurityRequest");
    ESP_LOGI(LOG_TAG, "On Security Request");
    return true;
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl)
  {
    Serial.println("onAuthenticationComplete");
    ESP_LOGI(LOG_TAG, "Starting BLE work!");
    if (cmpl.success)
    {
      Serial.println("onAuthenticationComplete -> success");
      uint16_t length;
      esp_ble_gap_get_whitelist_size(&length);
      ESP_LOGD(LOG_TAG, "size: %d", length);
      paired = true;
    }
    else
    {
      Serial.println("onAuthenticationComplete -> fail");
    }
  }
};

void focus()
{
  Serial.println("Focusing...");
  pRemoteCharacteristic->writeValue((uint8_t *)"\x01\x07", 2, true);
  delay(actionGapTime);
  pRemoteCharacteristic->writeValue((uint8_t *)"\x01\x06", 2, true);
  delay(actionGapTime);
  Serial.println("Focused!");
}

void takePhoto()
{
  Serial.println("Taking photo...");
  pRemoteCharacteristic->writeValue((uint8_t *)"\x01\x07", 2, true);
  delay(actionGapTime);
  pRemoteCharacteristic->writeValue((uint8_t *)"\x01\x09", 2, true);
  delay(actionGapTime);
  pRemoteCharacteristic->writeValue((uint8_t *)"\x01\x08", 2, true);
  delay(actionGapTime);
  pRemoteCharacteristic->writeValue((uint8_t *)"\x01\x06", 2, true);
  delay(actionGapTime);
  Serial.println("Photo taken!");
}

bool connectToServer(BLEAddress pAddress) {
  pClient = BLEDevice::createClient();
  pClient->connect(pAddress);
  Serial.println("Connected to device!");
  Serial.println("Waiting for services...");

  // get services uuid
  BLERemoteService *pRemoteService = pClient->getService("8000ff00-ff00-ffff-ffff-ffffffffffff");
  if (pRemoteService == nullptr)
  {
    Serial.println("Failed to find our service UUID");
    pClient->disconnect();
    return false;
  }
  Serial.println("Found our service");

  // get characteristics uuid
  pRemoteCharacteristic = pRemoteService->getCharacteristic("0000ff01-0000-1000-8000-00805f9b34fb");
  if (pRemoteCharacteristic == nullptr)
  {
    Serial.println("Failed to find our characteristic UUID");
    pClient->disconnect();
    return false;
  }
  Serial.println("Found our characteristic");

  return true;
}

void setup()
{
  pinMode(triggerPin, INPUT);
  pinMode(shutterPin, INPUT);

  Serial.begin(115200);

  BLEDevice::init("");
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
  BLEDevice::setSecurityCallbacks(new MySecurity());
  pBLEScan = BLEDevice::getScan(); // create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); // active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // less or equal setInterval value
}

void loop()
{
  // if device is found, connect to it
  if (doConnect == true && connectToServer(*pServerAddress) && paired)
  {
      connected = true;
      doConnect = false;
  }

  if (connected) {
    if (digitalRead(triggerPin) == HIGH)
    {
      focus();
      delay(recoverTime);
    }
    
    if (digitalRead(shutterPin) == HIGH)
    {
      takePhoto();
      delay(recoverTime);
    }

    // check if the camera is still connected
    if (!pClient->isConnected())
    {
      Serial.println("Camera disconnected!");
      connected = false;
      doConnect = false;

      pClient->disconnect();
      Serial.println("Disconnected from device!");
      Serial.println("Restarting scan...");

      return;
    }
  }
  else
  {
    Serial.println("Scanning...");
    pBLEScan->start(scanTime, false);
    Serial.println("Not found...");
    pBLEScan->clearResults();
  }
}