#include "sdkconfig.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string.h>

#include "esp_attr.h"
#include "esp_err.h"
#include "driver/gpio.h"

#include "format.hpp"
#include "task.hpp"

#include "NimBLEDevice.h"

using namespace std::chrono_literals;

// The remote service we wish to connect to.
static BLEUUID serviceUUID("180A");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

static std::atomic<bool> should_connect = false;
static std::atomic<bool> connected = false;
static std::atomic<bool> should_scan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

static void notifyCallback(
                           BLERemoteCharacteristic* pBLERemoteCharacteristic,
                           uint8_t* pData,
                           size_t length,
                           bool isNotify) {
  fmt::print("Notify callback for characteristic {} of data length {} data: {}\n",
             pBLERemoteCharacteristic->getUUID().toString().c_str(),
             length,
             (char*)pData);
}

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */
class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) {
    fmt::print("Connected\n");
    /** After connection we should change the parameters if we don't need fast response times.
     *  These settings are 150ms interval, 0 latency, 450ms timout.
     *  Timeout should be a multiple of the interval, minimum is 100ms.
     *  I find a multiple of 3-5 * the interval works best for quick response/reconnect.
     *  Min interval: 120 * 1.25ms = 150, Max interval: 120 * 1.25ms = 150, 0 latency, 45 * 10ms = 450ms timeout
     */
    pClient->updateConnParams(120,120,0,45);
  }

  void onDisconnect(NimBLEClient* pClient, int reason) {
    fmt::print("{} Disconnected, reason = {} - Starting scan\n",
               pClient->getPeerAddress().toString().c_str(), reason);
    NimBLEDevice::getScan()->start(5*1000);
  }

  /********************* Security handled here **********************
   ****** Note: these are the same return values as defaults ********/
  uint32_t onPassKeyRequest(){
    fmt::print("Client Passkey Request\n");
    /** return the passkey to send to the server */
    return 123456;
  }

  bool onConfirmPIN(uint32_t pass_key){
    fmt::print("The passkey YES/NO number: {}\n", pass_key);
    /** Return false if passkeys don't match. */
    return true;
  }

  /** Pairing process complete, we can check the results in connInfo */
  void onAuthenticationComplete(NimBLEConnInfo& connInfo){
    if(!connInfo.isEncrypted()) {
      fmt::print("Encrypt connection failed - disconnecting\n");
      /** Find the client with the connection handle provided in desc */
      NimBLEDevice::getClientByID(connInfo.getConnHandle())->disconnect();
      return;
    }
  }
};

/** Create a single global instance of the callback class to be used by all clients */
static ClientCallbacks clientCB;

bool connect_to_server() {
  if (!myDevice) {
    fmt::print("no device, cannot connect\n");
    return false;
  }
  fmt::print("Forming a connection to {}\n", myDevice->getAddress().toString().c_str());

  BLEClient*  pClient  = BLEDevice::createClient();
  fmt::print(" - Created client\n");

  pClient->setClientCallbacks(&clientCB, false);
  /** Set initial connection parameters: These settings are 15ms interval, 0 latency, 120ms timout.
   *  These settings are safe for 3 clients to connect reliably, can go faster if you have less
   *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
   *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 12 * 10ms = 120ms timeout
   */
  pClient->setConnectionParams(6,6,0,15);
  /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
  pClient->setConnectTimeout(5);

  // Connect to the remove BLE Server.
  if (!pClient->connect(myDevice)) {
    NimBLEDevice::deleteClient(pClient);
    fmt::print("Failed to connect, deleted client!\n");
    return false;
  }
  fmt::print(" - Connected to server\n");

  fmt::print("Connected to: {} RSSI: {}\n",
             pClient->getPeerAddress().toString().c_str(),
             pClient->getRssi());

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    fmt::print("Failed to find our service UUID: {}\n", serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  fmt::print(" - Found our service\n");


  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    fmt::print("Failed to find our characteristic UUID: {}\n", charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  fmt::print(" - Found our characteristic\n");

  // Read the value of the characteristic.
  if(pRemoteCharacteristic->canRead()) {
    std::string value = pRemoteCharacteristic->readValue();
    fmt::print("The characteristic value was: {}\n", value.c_str());
  }
  /** registerForNotify() has been deprecated and replaced with subscribe() / unsubscribe().
   *  Subscribe parameter defaults are: notifications=true, notifyCallback=nullptr, response=false.
   *  Unsubscribe parameter defaults are: response=false.
   */
  if(pRemoteCharacteristic->canNotify()) {
    //pRemoteCharacteristic->registerForNotify(notifyCallback);
    pRemoteCharacteristic->subscribe(true, notifyCallback);
  }

  connected = true;
  return true;
}


/** Define a class to handle the callbacks when advertisments are received */
class scanCallbacks: public NimBLEScanCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    fmt::print("Advertised Device found: {}\n", advertisedDevice->toString().c_str());
    if(advertisedDevice->isAdvertisingService(NimBLEUUID("DEAD")))
      {
        fmt::print("Found Our Service\n");
        /** stop scan before connecting */
        NimBLEDevice::getScan()->stop();
        /** Save the device reference in a global for the client to use*/
        myDevice = advertisedDevice;
        /** Ready to connect now */
        should_connect = true;
      }
  }

  /** Callback to process the results of the completed scan or restart it */
  void onScanEnd(NimBLEScanResults results) {
    fmt::print("Scan Ended\n");
  }
};

extern "C" void app_main(void) {
  esp_err_t err;
  espp::Logger logger({.tag = "BLE HID Host", .level = espp::Logger::Verbosity::INFO});
  logger.info("Bootup");

  // set up the gpio we'll toggle every time we get an input report
  static constexpr size_t RECV_GPIO = 21;
  static int pin_level = 0;
  gpio_config_t io_conf;
  memset(&io_conf, 0, sizeof(io_conf));
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.pin_bit_mask = (1<<RECV_GPIO);
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_config(&io_conf);
  // now set the initial level to low
  gpio_set_level((gpio_num_t)RECV_GPIO, pin_level);

  auto connect_task_fn = [](auto &m, auto &cv) {
    if (should_connect) {
      if (connect_to_server()) {
        fmt::print("We are now connected to the BLE server.\n");
      } else {
        fmt::print("We could not connect to the BLE server, we will do nothing more.\n");
      }
      should_connect = false;
    }
    if (connected) {
      // do something while connected?
    } else if (should_scan) {
      // this is just eample to start scan after disconnect, most likely there
      // is better way to do it
      BLEDevice::getScan()->start(0);
    }
    {
      std::unique_lock<std::mutex> lk(m);
      cv.wait_for(lk, 1s);
    }
  };
  logger.info("Creating connect task");
  auto connect_task = espp::Task::make_unique({
      .name = "Connect Task",
      .callback = connect_task_fn,
      .stack_size_bytes = 6*1024,
      .core_id = 1
    });

  // now that we're good let's scan for devices, connect, and handle input
  // events
  logger.info("Setting up NimBLE scan");
  //NimBLEDevice::setSecurityAuth(false, false, true);
  NimBLEDevice::setSecurityAuth(/*BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM |*/ BLE_SM_PAIR_AUTHREQ_SC);
  /** Optional: set the transmit power, default is -3db */
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** 12db */
  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks (new scanCallbacks());
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->setActiveScan(true);
  logger.info("Starting connect task");
  connect_task->start();

  pScan->start(5 * 1000);
  NimBLEUUID serviceUuid("");
  NimBLEClient *pClient = nullptr;
  NimBLEAdvertisedDevice device;

  while (true) {
    // // scan for 5 seconds
    // logger.info("Starting scan");
    // NimBLEScanResults results = pScan->getResults(5 * 1000, false);
    // // pClient = nullptr;
    // logger.info("Found {} devices", results.getCount());
    // for(int i = 0; i < results.getCount(); i++) {
    //   device = results.getDevice(i);
    //   if (device.isAdvertisingService(serviceUuid)) {
    //     pClient = NimBLEDevice::createClient();
    //     break;
    //   }
    // }
    // // if we couldn't find a client, try scanning again...
    // if (!pClient) continue;

    // // now try to connect
    // if(!pClient->connect(&device)) {
    //   // failed to connect, free memory
    //   NimBLEDevice::deleteClient(pClient);
    //   continue;
    // }

    // NimBLERemoteService *pService = pClient->getService(serviceUuid);

    // if (pService != nullptr) {
    //   NimBLERemoteCharacteristic *pCharacteristic = pService->getCharacteristic("1234");

    //   if (pCharacteristic != nullptr) {
    //     std::string value = pCharacteristic->readValue();
    //     // print or do whatever you need with the value
    //   }
    // }
    pScan->clearResults();
    std::this_thread::sleep_for(1s);
  }
}
