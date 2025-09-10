/* BLE Thermostat Client (no sleep)
   - Periodically scans for "BLE_THERMOSTAT"
   - Connects, discovers service+characteristic, reads 8-byte payload (two floats)
   - Prints floats, then disconnects and waits 5s before repeating
*/

#include <ArduinoLowPower.h>

#define QUERY_INTERVAL 5000
#define BLE_TIMEOUT 10000

enum conn_state_t {
  ST_IDLE,
  ST_SCAN,
  ST_CONNECT,
  ST_SERVICE_DISCOVER,
  ST_CHAR_DISCOVER,
  ST_READY
};

conn_state_t connection_state = ST_IDLE;
uint8_t connection_handle = 0xFF;
uint32_t thermostat_service_handle = 0;
uint16_t temps_char_handle = 0;

const uint8_t advertised_name[] = "BLE_THERMOSTAT";

const uuid_128 thermostat_service_uuid = {
  .data = { 0xde,0xad,0xfa,0xce,0xbe,0xef,0xca,0xfe,0xba,0xbe,0x00,0x00,0x00,0x00,0xca,0xfe }
};
const uuid_128 temps_char_uuid = {
  .data = { 0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x11,0x22,0x33,0x44,0x00,0x00,0x00,0x00,0x55,0x66 }
};

bool cycle_done = false;
bool scanner_ready = false;

static unsigned long timeout_start;

static bool find_complete_local_name_in_advertisement(sl_bt_evt_scanner_legacy_advertisement_report_t * response);

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_BUILTIN_INACTIVE);
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("BLE Thermostat Client");
  timeout_start = millis();
}

void loop()
{
  // don't sleep until cycle finished you fucker
  if (cycle_done) {
    Serial.println("Cycle finished! Going to sleep...");
    cycle_done = false;

    LowPower.deepSleep(QUERY_INTERVAL);
  }

  // timeout check
  if (millis() - timeout_start > BLE_TIMEOUT)
  {
    cycle_done = false;
    Serial.println("BLE timed out! Going to sleep...");
    LowPower.deepSleep(QUERY_INTERVAL);
  }

  // let BLE events be handled asynchronously
  delay(10);
}

void sl_bt_on_event(sl_bt_msg_t * evt)
{
  sl_status_t sc;

  switch (SL_BT_MSG_ID(evt->header)) {

    case sl_bt_evt_system_boot_id:
      Serial.println("BLE stack booted (client)");
      sc = sl_bt_scanner_set_parameters(sl_bt_scanner_scan_mode_active, 16, 16);
      app_assert_status(sc);
      scanner_ready = true;
      // start first scan immediately
      sc = sl_bt_scanner_start(sl_bt_scanner_scan_phy_1m, sl_bt_scanner_discover_generic);
      app_assert_status(sc);
      connection_state = ST_SCAN;
      break;

    case sl_bt_evt_scanner_legacy_advertisement_report_id:
      if (find_complete_local_name_in_advertisement(
              &(evt->data.evt_scanner_legacy_advertisement_report))) {
        Serial.println("Found BLE_THERMOSTAT, connecting...");
        sc = sl_bt_scanner_stop();
        app_assert_status(sc);
        sc = sl_bt_connection_open(
            evt->data.evt_scanner_legacy_advertisement_report.address,
            evt->data.evt_scanner_legacy_advertisement_report.address_type,
            sl_bt_gap_phy_1m,
            NULL);
        app_assert_status(sc);
        connection_state = ST_CONNECT;
      }
      break;

    case sl_bt_evt_connection_opened_id:
      Serial.println("Connected");
      connection_handle = evt->data.evt_connection_opened.connection;
      digitalWrite(LED_BUILTIN, LED_BUILTIN_ACTIVE);
      sc = sl_bt_gatt_discover_primary_services_by_uuid(connection_handle,
                                                        sizeof(thermostat_service_uuid),
                                                        thermostat_service_uuid.data);
      app_assert_status(sc);
      connection_state = ST_SERVICE_DISCOVER;
      break;

    case sl_bt_evt_gatt_service_id:
      thermostat_service_handle = evt->data.evt_gatt_service.service;
      Serial.println("Service discovered");
      break;

    case sl_bt_evt_gatt_characteristic_id:
      temps_char_handle = evt->data.evt_gatt_characteristic.characteristic;
      Serial.println("Characteristic discovered");
      break;

    case sl_bt_evt_gatt_procedure_completed_id:
      if (connection_state == ST_SERVICE_DISCOVER) {
        sc = sl_bt_gatt_discover_characteristics_by_uuid(connection_handle,
                                                         thermostat_service_handle,
                                                         sizeof(temps_char_uuid.data),
                                                         temps_char_uuid.data);
        app_assert_status(sc);
        connection_state = ST_CHAR_DISCOVER;
      } else if (connection_state == ST_CHAR_DISCOVER) {
        sc = sl_bt_gatt_read_characteristic_value(connection_handle,
                                                  temps_char_handle);
        app_assert_status(sc);
        connection_state = ST_READY;
      }
      break;

    case sl_bt_evt_gatt_characteristic_value_id:
      if (evt->data.evt_gatt_characteristic_value.characteristic == temps_char_handle) {
        uint8_t *buf = evt->data.evt_gatt_characteristic_value.value.data;
        uint8_t len = evt->data.evt_gatt_characteristic_value.value.len;
        Serial.print("Received payload len=");
        Serial.println(len);
        if (len >= 8) {
          float values[2];
          memcpy(values, buf, 8);
          Serial.print("set_temp = ");
          Serial.println(values[0]);
          Serial.print("current_temp = ");
          Serial.println(values[1]);
        }
        sl_bt_connection_close(connection_handle);
      }
      break;

    case sl_bt_evt_connection_closed_id:
      Serial.println("Disconnected");
      digitalWrite(LED_BUILTIN, LED_BUILTIN_INACTIVE);
      connection_handle = 0xFF;
      cycle_done = true; // tell loop() to wait then rescan
      break;

    default:
      break;
  }
}

static bool find_complete_local_name_in_advertisement(sl_bt_evt_scanner_legacy_advertisement_report_t * response)
{
  int i = 0;
  while (i < (response->data.len - 1)) {
    uint8_t advertisement_length = response->data.data[i];
    if (advertisement_length == 0) break;
    uint8_t advertisement_type = response->data.data[i + 1];

    if (advertisement_type == 0x09) { // Complete Local Name
      uint8_t name_len = advertisement_length - 1;
      if (name_len == strlen((const char*)advertised_name) &&
          memcmp(response->data.data + i + 2, advertised_name, name_len) == 0) {
        return true;
      }
    }
    i += advertisement_length + 1;
  }
  return false;
}