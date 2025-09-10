/* BLE Thermostat Server
   - Advertises as "BLE_THERMOSTAT"
   - Stores two floats: set_temp and current_temp
   - When a client connects the server writes+notifies the two floats (packed as two 32-bit floats => 8 bytes)
*/

// matter includes
#include <Matter.h>
#include <MatterBLE.h>
#include <MatterThermostat.h>

// display and sensor includes
#include <Adafruit_Si7021.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

// display defines
#define TFT_CS (PA7)
#define TFT_RST (PD2)
#define TFT_DC (PB0)
#define TFT_MOSI (PC3)
#define TFT_SCLK (PC1)
#define DISP_UPDATE_INT 6000  // display switch interval ms

// display variables
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
unsigned long lastDisplayUpdate = 0;
bool showCurrent = true;

// BLE functions
static void ble_initialize_gatt_db();
static void ble_start_advertising();
static void send_temperature_data();

// display functions
void resetTFT();
void initTFT();
void dispTemp(float current, float setpoint);
void updateTFT();
void dispWelcome(const char *pairingCode);

// temp humidity onboard sensor
Adafruit_Si7021 temp_humidity_sensor;

static const uint8_t advertised_name[] = "BLE_THERMOSTAT";

static uint16_t thermostat_service_handle;
static uint16_t temps_characteristic_handle;
static uint8_t ble_connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;

MatterThermostat matter_thermostat;
static bool matter_commissioning = false;

float set_temp = 22.5f;  // dummy

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_BUILTIN_INACTIVE);
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("BLE Thermostat Server");

  // Init the Si7021 temperature and humidity sensor
  pinMode(PC9, OUTPUT);
  digitalWrite(PC9, HIGH);
  delay(50);
  if (!temp_humidity_sensor.begin()) {
    Serial.println("Temperature and humidity sensor initialization failed");
  } else {
    Serial.println("Temperature and humidity sensor initialization successful");
  }

  // init tft
  resetTFT();
  initTFT();
  Serial.println("TFT initialized");

  // matter commisioning
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("Matter device is not commissioned");
    Serial.println("Commission it to your Matter hub with the manual pairing code or QR code");
    const char * matter_pairing_code = Matter.getManualPairingCode().c_str();
    Serial.printf("Manual pairing code: %s\n", matter_pairing_code);
    dispWelcome(matter_pairing_code);
    Serial.printf("QR code URL: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
    matter_commissioning = true;
  }

  while (!Matter.isDeviceCommissioned()) {
    delay(200);
  }

  // init thread
  Serial.println("Waiting for Thread network...");
  while (!Matter.isDeviceThreadConnected()) {
    delay(200);
  }
  Serial.println("Connected to Thread network");

  Serial.println("Waiting for Matter device discovery...");
  while (!matter_thermostat.is_online()) {
    delay(200);
  }
  Serial.println("Matter device is now online");

  // Wait until the commissioning BLE device disconnects before interacting with the BLE stack
  if (matter_commissioning) {
    while (ble_connection_handle != SL_BT_INVALID_CONNECTION_HANDLE) {
      yield();
    }
  }
  matter_commissioning = false;

  // matter commissioning finished, init ble
  ble_initialize_gatt_db();
  ble_start_advertising();
  Serial.println("BLE advertisement started");
}

void loop() {
  // every 100ms update tft
  updateTFT();
  delay(100);
}

// !!!MATTER!!! ble stack event handler
void matter_ble_on_event(sl_bt_msg_t *evt) {

  // otherwhise same as sl ble stack
  sl_status_t sc;
  switch (SL_BT_MSG_ID(evt->header)) {

    case sl_bt_evt_connection_opened_id:
      ble_connection_handle = evt->data.evt_connection_opened.connection;
      Serial.println("Client connected -> sending temps");
      send_temperature_data();
      break;

    case sl_bt_evt_connection_closed_id:
      Serial.println("Connection closed, restarting advertising");
      ble_start_advertising();
      ble_connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;
      if (!matter_commissioning) {
        ble_start_advertising();
        Serial.println("BLE advertisement restarted");
      }
      break;

    default:
      break;
  }
}

// advertising
static void ble_start_advertising() {
  static uint8_t advertising_set_handle = 0xff;
  static bool init = true;
  sl_status_t sc;

  if (init) {
    sc = sl_bt_advertiser_create_set(&advertising_set_handle);
    app_assert_status(sc);

    // 100 ms interval (160 * 0.625 ms)
    sc = sl_bt_advertiser_set_timing(
      advertising_set_handle,
      160,
      160,
      0,
      0);
    app_assert_status(sc);
    init = false;
  }

  sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle, sl_bt_advertiser_general_discoverable);
  app_assert_status(sc);

  sc = sl_bt_legacy_advertiser_start(advertising_set_handle, sl_bt_advertiser_connectable_scannable);
  app_assert_status(sc);

  Serial.print("Started advertising as '");
  Serial.print((const char *)advertised_name);
  Serial.println("'...");
}

// GATT DB: Thermostat service + characteristic (8 bytes)
static void ble_initialize_gatt_db() {
  static uint16_t gattdb_session_id;
  static uint16_t generic_access_service_handle;
  static uint16_t name_characteristic_handle;
  sl_status_t sc;

  sc = sl_bt_gattdb_new_session(&gattdb_session_id);
  app_assert_status(sc);

  // Generic Access service (so the device name can be advertised)
  const uint8_t generic_access_service_uuid[] = { 0x00, 0x18 };
  sc = sl_bt_gattdb_add_service(gattdb_session_id,
                                sl_bt_gattdb_primary_service,
                                SL_BT_GATTDB_ADVERTISED_SERVICE,
                                sizeof(generic_access_service_uuid),
                                generic_access_service_uuid,
                                &generic_access_service_handle);
  app_assert_status(sc);

  // Device Name characteristic (0x2A00)
  const sl_bt_uuid_16_t device_name_characteristic_uuid = { .data = { 0x00, 0x2A } };
  sc = sl_bt_gattdb_add_uuid16_characteristic(gattdb_session_id,
                                              generic_access_service_handle,
                                              SL_BT_GATTDB_CHARACTERISTIC_READ,
                                              0x00,
                                              0x00,
                                              device_name_characteristic_uuid,
                                              sl_bt_gattdb_fixed_length_value,
                                              sizeof(advertised_name) - 1,
                                              sizeof(advertised_name) - 1,
                                              advertised_name,
                                              &name_characteristic_handle);
  app_assert_status(sc);
  sc = sl_bt_gattdb_start_service(gattdb_session_id, generic_access_service_handle);
  app_assert_status(sc);

  // Thermostat service (128-bit UUID - pick one)
  const uuid_128 thermostat_service_uuid = {
    .data = { 0xde, 0xad, 0xfa, 0xce, 0xbe, 0xef, 0xca, 0xfe, 0xba, 0xbe, 0x00, 0x00, 0x00, 0x00, 0xca, 0xfe }
  };
  sc = sl_bt_gattdb_add_service(gattdb_session_id,
                                sl_bt_gattdb_primary_service,
                                SL_BT_GATTDB_ADVERTISED_SERVICE,
                                sizeof(thermostat_service_uuid),
                                thermostat_service_uuid.data,
                                &thermostat_service_handle);
  app_assert_status(sc);

  // Temps characteristic - two floats (8 bytes) - READ + NOTIFY
  const uuid_128 temps_char_uuid = {
    .data = { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22, 0x33, 0x44, 0x00, 0x00, 0x00, 0x00, 0x55, 0x66 }
  };
  uint8_t init_val[8] = { 0 };  // placeholder initial value

  sc = sl_bt_gattdb_add_uuid128_characteristic(gattdb_session_id,
                                               thermostat_service_handle,
                                               SL_BT_GATTDB_CHARACTERISTIC_READ | SL_BT_GATTDB_CHARACTERISTIC_NOTIFY,
                                               0x00,
                                               0x00,
                                               temps_char_uuid,
                                               sl_bt_gattdb_fixed_length_value,
                                               sizeof(init_val),
                                               sizeof(init_val),
                                               init_val,
                                               &temps_characteristic_handle);
  app_assert_status(sc);

  sc = sl_bt_gattdb_start_service(gattdb_session_id, thermostat_service_handle);
  app_assert_status(sc);

  sc = sl_bt_gattdb_commit(gattdb_session_id);
  app_assert_status(sc);

  Serial.println("GATT DB initialized (thermostat)");
}

/* Write + notify the two float values to connected client(s) */
static void send_temperature_data() {
  sl_status_t sc;
  // read temp to send
  float current_temp = temp_humidity_sensor.readTemperature();
  float values[2] = { set_temp, current_temp };
  uint8_t payload[sizeof(values)];
  memcpy(payload, values, sizeof(values));

  // Update the local attribute value (so read works) and then notify
  sc = sl_bt_gatt_server_write_attribute_value(temps_characteristic_handle, 0, sizeof(payload), payload);
  app_assert_status(sc);

  // Notify all connected peers (server small example). You can notify a specific connection with sl_bt_gatt_server_notify.
  sc = sl_bt_gatt_server_notify_all(temps_characteristic_handle, sizeof(payload), payload);
  // Depending on the SDK version function name may be sl_bt_gatt_server_notify or sl_bt_gatt_server_notify_all.
  app_assert_status(sc);

  Serial.print("Sent floats: set_temp=");
  Serial.print(values[0]);
  Serial.print(" current_temp=");
  Serial.println(values[1]);
}

void initTFT() {
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW);
  delay(100);
  digitalWrite(TFT_RST, HIGH);
  delay(100);

  tft.initR(INITR_MINI160x80_PLUGIN);
  tft.fillScreen(ST77XX_BLACK);

  tft.setRotation(45);
}

void resetTFT() {
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, LOW);
  delay(100);
  digitalWrite(TFT_RST, HIGH);
  delay(100);
}

// display temperature in a disgusting way
// ennel sokkal rosszabb dolog a heten mar nem nagyon tortenhet velem. max ha el kell olvassam ezt a szart megegyszer.
void dispTemp(const char *label, float value, uint16_t color) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  // --- Header (small) ---
  tft.setTextSize(2);
  tft.setTextColor(color);
  int16_t bx, by;
  uint16_t bw, bh;
  tft.getTextBounds(label, 0, 0, &bx, &by, &bw, &bh);
  int16_t headerX = (tft.width() - bw) / 2;
  const int16_t headerY = 8;
  tft.setCursor(headerX, headerY);
  tft.print(label);

  // --- Number (big) ---
  char numStr[16];
  snprintf(numStr, sizeof(numStr), "%.1f", value);  // only the numeric part

  const uint8_t sizeNum = 3;  // big size for the number
  tft.setTextSize(sizeNum);
  tft.getTextBounds(numStr, 0, 0, &bx, &by, &bw, &bh);
  uint16_t wNum = bw;
  uint16_t hNum = bh;

  // --- 'C' char (slightly smaller) ---
  const uint8_t sizeC = (sizeNum > 1) ? (sizeNum - 1) : 1;
  tft.setTextSize(sizeC);
  int16_t bxC, byC;
  uint16_t wC, hC;
  tft.getTextBounds("C", 0, 0, &bxC, &byC, &wC, &hC);

  // --- degree circle size & spacings ---
  uint16_t degDiameter = hNum / 3;
  if (degDiameter < 4) degDiameter = 4;
  if (degDiameter > 12) degDiameter = 12;
  const uint8_t spacingNumDeg = 4;
  const uint8_t spacingDegC = 4;

  // total width to center: [number][space][degree circle][space][C]
  int16_t totalWidth = (int16_t)wNum + spacingNumDeg + (int16_t)degDiameter + spacingDegC + (int16_t)wC;
  int16_t startX = (tft.width() - totalWidth) / 2;

  // value vertical position: place it below header, roughly centered in remaining area
  int16_t valueY = headerY + bh + 8 + ((tft.height() - (headerY + bh + 8) - (int16_t)hNum) / 2);

  // --- draw the numeric value ---
  tft.setTextSize(sizeNum);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(startX, valueY);
  tft.print(numStr);

  // --- draw the degree as a small filled circle (superscript-like) ---
  int16_t circleCenterX = startX + wNum + spacingNumDeg + degDiameter / 2;
  // put the circle a bit above the vertical center of the number
  int16_t circleCenterY = valueY + (hNum / 4);
  // this is fucking diabolical. fucking fucker celsius fuck you
  tft.fillCircle(circleCenterX, circleCenterY, degDiameter / 2, ST77XX_WHITE);
  tft.fillCircle(circleCenterX, circleCenterY, degDiameter / 2.5f, ST77XX_BLACK);

  // --- draw the 'C' to the right of the circle ---
  int16_t cX = startX + wNum + spacingNumDeg + degDiameter + spacingDegC;
  // vertically align C roughly to the number baseline/center
  int16_t cY = valueY + (hNum - hC) / 2;
  tft.setTextSize(sizeNum);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(cX, cY);
  tft.print("C");
}

// welcome screen
void dispWelcome(const char *pairingCode) {
  // Hello.
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  const char *helloStr = "Hello.";
  int16_t bx, by;
  uint16_t bw, bh;
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);

  tft.getTextBounds(helloStr, 0, 0, &bx, &by, &bw, &bh);
  int16_t helloX = (tft.width() - bw) / 2;
  int16_t helloY = (tft.height() - bh) / 2 - 10;
  tft.setCursor(helloX, helloY);
  tft.print(helloStr);

  const char *subStr = "THERMOnkey";
  tft.setTextSize(2);
  tft.getTextBounds(subStr, 0, 0, &bx, &by, &bw, &bh);
  int16_t subX = (tft.width() - bw) / 2;
  int16_t subY = helloY + bh + 10;
  tft.setCursor(subX, subY);
  tft.print(subStr);

  delay(3000); // 3 sec

  tft.fillScreen(ST77XX_BLACK);

  const char *headerStr = "Pairing Code:";
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.getTextBounds(headerStr, 0, 0, &bx, &by, &bw, &bh);
  int16_t headerX = (tft.width() - bw) / 2;
  const int16_t headerY = 8;
  tft.setCursor(headerX, headerY);
  tft.print(headerStr);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.getTextBounds(pairingCode, 0, 0, &bx, &by, &bw, &bh);
  int16_t codeX = (tft.width() - bw) / 2;
  int16_t codeY = headerY + bh + (tft.height() - (headerY + bh) - bh) / 2;
  tft.setCursor(codeX, codeY);
  tft.print(pairingCode);
}


void updateTFT() {
  if (millis() - lastDisplayUpdate >= DISP_UPDATE_INT) {
    lastDisplayUpdate = millis();
    float current_temp = temp_humidity_sensor.readTemperature();

    if (showCurrent) {
      dispTemp("Current Temp", current_temp, ST77XX_CYAN);
    } else {
      dispTemp("Set Temp", set_temp, ST77XX_YELLOW);
    }

    showCurrent = !showCurrent;  // toggle for next cycle
  }
}
