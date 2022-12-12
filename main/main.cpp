
/** NimBLE_Server Demo:
 *
 *  Demonstrates many of the available features of the NimBLE client library.
 *  
 *  Created: on March 24 2020
 *      Author: H2zero
 * 
 */
#include <NimBLEDevice.h>

#include "driver/gpio.h"

extern "C" {void app_main(void);}

static NimBLEAdvertisedDevice* advDevice;

static bool doConnect = false;
static uint32_t scanTime = 0; /** scan time in milliseconds, 0 = scan forever */
static constexpr size_t RECV_GPIO = 21;
static int pin_level = 0;

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */  
class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) {
    printf("Connected\n");
    /** After connection we should change the parameters if we don't need fast response times.
     *  These settings are 150ms interval, 0 latency, 450ms timout.
     *  Timeout should be a multiple of the interval, minimum is 100ms.
     *  I find a multiple of 3-5 * the interval works best for quick response/reconnect.
     *  Min interval: 120 * 1.25ms = 150, Max interval: 120 * 1.25ms = 150, 0 latency, 45 * 10ms = 450ms timeout
     */
    // pClient->updateConnParams(120,120,0,45);
    // pClient->setConnectionParams(6,6,0,15);
    pClient->setConnectionParams(6,6,0,15);
  }

  void onDisconnect(NimBLEClient* pClient, int reason) {
    printf("%s Disconnected, reason = %d - Starting scan\n",
           pClient->getPeerAddress().toString().c_str(), reason);
    NimBLEDevice::getScan()->start(scanTime);
  }
    
  /********************* Security handled here **********************
   ****** Note: these are the same return values as defaults ********/
  uint32_t onPassKeyRequest(){
    printf("Client Passkey Request\n");
    /** return the passkey to send to the server */
    return 123456;
  }

  bool onConfirmPIN(uint32_t pass_key){
    printf("The passkey YES/NO number: %d\n", (int)pass_key);
    /** Return false if passkeys don't match. */
    return true;
  }

  /** Pairing process complete, we can check the results in connInfo */
  void onAuthenticationComplete(NimBLEConnInfo& connInfo){
    if(!connInfo.isEncrypted()) {
      printf("Encrypt connection failed - disconnecting\n");
      /** Find the client with the connection handle provided in desc */
      NimBLEDevice::getClientByID(connInfo.getConnHandle())->disconnect();
      return;
    }
  }
};


/** Define a class to handle the callbacks when advertisments are received */
class scanCallbacks: public NimBLEScanCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
    printf("Advertised Device found: %s\n", advertisedDevice->toString().c_str());
    if(advertisedDevice->isAdvertisingService(NimBLEUUID("1812")))
      {
        printf("Found Our Service\n");
        /** stop scan before connecting */
        NimBLEDevice::getScan()->stop();
        /** Save the device reference in a global for the client to use*/
        advDevice = advertisedDevice;
        /** Ready to connect now */
        doConnect = true;
      }
  }

  /** Callback to process the results of the completed scan or restart it */
  void onScanEnd(NimBLEScanResults results) {
    printf("Scan Ended\n");
  }
};


/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify){
  // std::string str = (isNotify == true) ? "Notification" : "Indication";
  // str += " from ";
  // str += pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString();
  // str += ": Service = " + pRemoteCharacteristic->getRemoteService()->getUUID().toString();
  // str += ", Characteristic = " + pRemoteCharacteristic->getUUID().toString();
  // str += ", Value = " + std::string((char*)pData, length);
  // printf("%s\n", str.c_str());
  printf("\x1B[1A"); // go up a line
  printf("\x1B[2K\r"); // erase the line
  printf("Got notification, length = %d B\n", length);
  // toogle the pin
  pin_level = pin_level ? 0 : 1;
  gpio_set_level((gpio_num_t)RECV_GPIO, pin_level);
}


/** Create a single global instance of the callback class to be used by all clients */
static ClientCallbacks clientCB;

/** Handles the provisioning of clients and connects / interfaces with the server */
bool connectToServer() {
  NimBLEClient* pClient = nullptr;
    
  /** Check if we have a client we should reuse first **/
  if(NimBLEDevice::getClientListSize()) {
    /** Special case when we already know this device, we send false as the
     *  second argument in connect() to prevent refreshing the service database.
     *  This saves considerable time and power.
     */
    pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
    if(pClient){
      if(!pClient->connect(advDevice, false)) {
        printf("Reconnect failed\n");
        return false;
      }
      printf("Reconnected client\n");
    }
    /** We don't already have a client that knows this device,
     *  we will check for a client that is disconnected that we can use.
     */
    else {
      pClient = NimBLEDevice::getDisconnectedClient();
    }
  }
    
  /** No client to reuse? Create a new one. */
  if(!pClient) {
    if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
      printf("Max clients reached - no more connections available\n");
      return false;
    }
        
    pClient = NimBLEDevice::createClient();
        
    printf("New client created\n");
    
    pClient->setClientCallbacks(&clientCB);
    /** Set initial connection parameters: These settings are 15ms interval, 0 latency, 120ms timout.
     *  These settings are safe for 3 clients to connect reliably, can go faster if you have less
     *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
     *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 12 * 10ms = 120ms timeout
     */
    // pClient->setConnectionParams(6,6,0,15);
    /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
    // pClient->setConnectTimeout(5);
        

    if (!pClient->connect(advDevice)) {
      /** Created a client but failed to connect, don't need to keep it as it has no data */
      NimBLEDevice::deleteClient(pClient);
      printf("Failed to connect, deleted client\n");
      return false;
    }
  }
    
  if(!pClient->isConnected()) {
    if (!pClient->connect(advDevice)) {
      printf("Failed to connect\n");
      return false;
    }
  }
    
  printf("Connected to: %s RSSI: %d\n",
         pClient->getPeerAddress().toString().c_str(),
         pClient->getRssi());
    
  /** Now we can read/write/subscribe the charateristics of the services we are interested in */
  NimBLERemoteService* pSvc = nullptr;
  NimBLERemoteCharacteristic* pChr = nullptr;
  NimBLERemoteDescriptor* pDsc = nullptr;
    
  auto services = pClient->getServices(true);
  auto num_srv = services->size();
  printf("got %d services!\n", num_srv);
  for (int i=0; i< num_srv; i++) {
    auto s = (*services)[i];
    auto characteristics = s->getCharacteristics(true);
    auto num_chars = characteristics->size();
    printf("got service %s, with %d characteristics\n", s->getUUID().toString().c_str(), num_chars);
    for (int j=0; j<num_chars; j++) {
      auto c = (*characteristics)[j];
      auto descriptors = c->getDescriptors(true);
      auto num_desc = descriptors->size();
      printf("Got characteristic: %s, with %d descriptors\n", c->getUUID().toString().c_str(), num_desc);
      for (int k=0; k<num_desc; k++) {
        auto d = (*descriptors)[k];
        printf("Got descriptor: %s\n", d->getUUID().toString().c_str());
      }
      if(c->canNotify()) {
        printf("subscribing (notifications)\n");
        if(!c->subscribe(true, notifyCB)) {
          printf("couldn't subscribe (notifications)!\n");
        }
      } else if(c->canIndicate()) {
        printf("subscribing (indication)\n");
        /** Send false as first argument to subscribe to indications instead of notifications */
        if(!c->subscribe(false, notifyCB)) {
          printf("couldn't subscribe (indication)!\n");
        }
      }

    }
  }

  pSvc = pClient->getService("1812");
  if(pSvc) {     /** make sure it's not null */
    pChr = pSvc->getCharacteristic("2A4D");
  } else {
    printf("Could not get service 0x1812\n");
  }

  if(pChr) {     /** make sure it's not null */
    if(pChr->canRead()) {
      printf("%s Value: %s\n",
             pChr->getUUID().toString().c_str(),
             pChr->readValue().c_str());
    }
    // if(pChr->canWrite()) {
    //   if(pChr->writeValue("No tip!")) {
    //     printf("Wrote new value to: %s\n", pChr->getUUID().toString().c_str());
    //   }
    //   else {
    //     printf("couldn't write!\n");
    //   }
    //   if(pChr->canRead()) {
    //     printf("The value of: %s is now: %s\n",
    //            pChr->getUUID().toString().c_str(),
    //            pChr->readValue().c_str());
    //   }
    // }
  } else {
    printf("2A4D service not found.\n");
  }

  printf("Done with this device!\n");
  return true;
}

void connectTask (void * parameter){
  /** Loop here until we find a device we want to connect to */
  for(;;) {
    if(doConnect) {
      doConnect = false;
      /** Found a device we want to connect to, do it now */
      if(connectToServer()) {
        printf("Success! we should now be getting notifications!\n");
      } else {
        printf("Failed to connect, starting scan\n");
        // NimBLEDevice::getScan()->start(scanTime);
      }
    }
    vTaskDelay(10/portTICK_PERIOD_MS);
  }
    
  vTaskDelete(NULL);
}

void app_main (void){
  printf("Starting NimBLE Client\n");
  /** Initialize NimBLE, no device name spcified as we are not advertising */
  NimBLEDevice::init("");

  // set up the gpio we'll toggle every time we get an input report
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
    
  /** Set the IO capabilities of the device, each option will trigger a different pairing method.
   *  BLE_HS_IO_KEYBOARD_ONLY    - Passkey pairing
   *  BLE_HS_IO_DISPLAY_YESNO   - Numeric comparison pairing
   *  BLE_HS_IO_NO_INPUT_OUTPUT - DEFAULT setting - just works pairing
   */
  //NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY); // use passkey
  //NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO); //use numeric comparison
  
  /** 2 different ways to set security - both calls achieve the same result.
   *  no bonding, no man in the middle protection, secure connections.
   *
   *  These are the default values, only shown here for demonstration.
   */
  NimBLEDevice::setSecurityAuth(false, false, false); // NOTE: last was true
  NimBLEDevice::setSecurityAuth(/*BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM |*/ BLE_SM_PAIR_AUTHREQ_SC);
  
  /** Optional: set the transmit power, default is -3db */
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** 12db */
    
  /** Optional: set any devices you don't want to get advertisments from */
  // NimBLEDevice::addIgnored(NimBLEAddress ("aa:bb:cc:dd:ee:ff"));
  
  /** create new scan */
  NimBLEScan* pScan = NimBLEDevice::getScan();
    
  /** create a callback that gets called when advertisers are found */
  pScan->setScanCallbacks (new scanCallbacks());
    
  /** Set scan interval (how often) and window (how long) in milliseconds */
  pScan->setInterval(100);
  pScan->setWindow(99);
    
  /** Active scan will gather scan response data from advertisers
   *  but will use more energy from both devices
   */
  pScan->setActiveScan(true);
  /** Start scanning for advertisers for the scan time specified (in seconds) 0 = forever
   *  Optional callback for when scanning stops.
   */
  pScan->start(scanTime);
    
  printf("Scanning for peripherals\n");
    
  xTaskCreate(connectTask, "connectTask", 5000, NULL, 1, NULL);
}

