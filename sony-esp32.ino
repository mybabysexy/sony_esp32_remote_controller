/*
   Sony Camera BLE control using ESP32
   Enhanced version with fixed BLE pairing security & Address Type handling.
   Compatible with ESP32-C3 Super Mini and Sony ZV-E10 / A7 / A6x00 series.
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_gap_ble_api.h>

const int scanTime = 5;          // In seconds
const int actionGapTime = 150;   // In milliseconds
const int recoverTime = 800;     // In milliseconds

// Default Pins (ESP32-C3)
const int triggerPin = 20;       // GPIO 20: AF Focus
const int shutterPin = 21;       // GPIO 21: Shutter / Take Photo
const int bootButtonPin = 9;     // GPIO 9: Onboard BOOT button (Active LOW)

// Supported Sony camera models
const int cameraModelsSize = 5;
const String cameraModels[] = {
  "ZV-E10",
  "ZV-",
  "ILCE",
  "ILME",
  "Sony"
};

// Sony GATT UUIDs
static const char* SONY_SERVICE_UUID = "8000ff00-ff00-ffff-ffff-ffffffffffff";
static const char* SONY_CHAR_UUID    = "0000ff01-0000-1000-8000-00805f9b34fb";

static BLEScan *pBLEScan = nullptr;
static BLERemoteCharacteristic *pRemoteCharacteristic = nullptr;
static BLEClient *pClient = nullptr;
static BLEAdvertisedDevice *pTargetDevice = nullptr;

bool doConnect = false;
bool paired = false;
bool connected = false;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice) override
  {
    String name = advertisedDevice.getName().c_str();

    for (int i = 0; i < cameraModelsSize; i++)
    {
      if (name.indexOf(cameraModels[i]) != -1)
      {
        pTargetDevice = new BLEAdvertisedDevice(advertisedDevice);
        pBLEScan->stop();
        Serial.printf("Found Sony camera: %s [%s]\n", name.c_str(), advertisedDevice.getAddress().toString().c_str());
        doConnect = true;
        break;
      }
    }
  }
};

class MySecurity : public BLESecurityCallbacks
{
  bool onConfirmPIN(uint32_t pin) override
  {
    Serial.printf("onConfirmPIN: %06lu\n", (unsigned long)pin);
    return true;
  }

  uint32_t onPassKeyRequest() override
  {
    Serial.println("onPassKeyRequest");
    return 133700;
  }

  void onPassKeyNotify(uint32_t pass_key) override
  {
    Serial.printf("onPassKeyNotify: %06lu\n", (unsigned long)pass_key);
  }

  bool onSecurityRequest() override
  {
    Serial.println("onSecurityRequest -> Accepted");
    return true;
  }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override
  {
    if (cmpl.success)
    {
      Serial.println("Pairing / Authentication SUCCESS!");
      paired = true;
    }
    else
    {
      Serial.printf("Authentication FAILED, reason: 0x%x\n", cmpl.fail_reason);
    }
  }
};

void focus()
{
  if (!pRemoteCharacteristic) return;
  Serial.println("Focusing...");
  pRemoteCharacteristic->writeValue((uint8_t *)"\x01\x07", 2, true);
  delay(actionGapTime);
  pRemoteCharacteristic->writeValue((uint8_t *)"\x01\x06", 2, true);
  delay(actionGapTime);
  Serial.println("Focused!");
}

void takePhoto()
{
  if (!pRemoteCharacteristic) return;
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

bool connectToServer(BLEAdvertisedDevice* device)
{
  Serial.printf("Connecting to camera %s (AddrType: %d)...\n", 
                device->getAddress().toString().c_str(), device->getAddressType());
  
  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
  }
  
  if (!pClient->connect(device)) {
    Serial.println("Failed to connect.");
    return false;
  }
  Serial.println("Connected to camera physical layer!");

  BLERemoteService *pRemoteService = pClient->getService(SONY_SERVICE_UUID);
  if (pRemoteService == nullptr)
  {
    Serial.println("Failed to find Sony Remote service UUID");
    pClient->disconnect();
    return false;
  }

  pRemoteCharacteristic = pRemoteService->getCharacteristic(SONY_CHAR_UUID);
  if (pRemoteCharacteristic == nullptr)
  {
    Serial.println("Failed to find Sony Remote characteristic UUID");
    pClient->disconnect();
    return false;
  }

  Serial.println("Ready to send commands!");
  return true;
}

void setup()
{
  pinMode(triggerPin, INPUT_PULLDOWN);
  pinMode(shutterPin, INPUT_PULLDOWN);
  pinMode(bootButtonPin, INPUT_PULLUP);

  Serial.begin(115200);

  BLEDevice::init("ESP32-Sony-Remote");
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
  BLEDevice::setSecurityCallbacks(new MySecurity());

  // Configure Bluedroid Security Parameters for Sony Camera SMP Bonding
  esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
  esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
  uint8_t key_size = 16;
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop()
{
  if (doConnect == true && pTargetDevice != nullptr)
  {
    if (connectToServer(pTargetDevice))
    {
      connected = true;
    }
    doConnect = false;
  }

  if (connected && pClient && pClient->isConnected())
  {
    // Onboard BOOT Button
    if (digitalRead(bootButtonPin) == LOW)
    {
      takePhoto();
      delay(recoverTime);
    }

    // External Trigger (Focus)
    if (digitalRead(triggerPin) == HIGH)
    {
      focus();
      delay(recoverTime);
    }

    // External Shutter (Photo)
    if (digitalRead(shutterPin) == HIGH)
    {
      takePhoto();
      delay(recoverTime);
    }
  }
  else
  {
    if (connected)
    {
      Serial.println("Camera disconnected! Restarting scan...");
      connected = false;
      if (pClient) pClient->disconnect();
    }
    Serial.println("Scanning for Sony camera in pairing mode...");
    pBLEScan->start(scanTime, false);
    pBLEScan->clearResults();
    delay(500);
  }
}
