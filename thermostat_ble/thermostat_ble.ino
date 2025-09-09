/* BLE Thermostat Server
   - Advertises as "BLE_THERMOSTAT"
   - Stores two floats: set_temp and current_temp
   - When a client connects the server writes+notifies the two floats (packed as two 32-bit floats => 8 bytes)
*/

static void ble_initialize_gatt_db();
static void ble_start_advertising();
static void send_temperature_data();

static const uint8_t advertised_name[] = "BLE_THERMOSTAT";

static uint16_t thermostat_service_handle;
static uint16_t temps_characteristic_handle;
static uint8_t ble_connection_handle = SL_BT_INVALID_CONNECTION_HANDLE;

float set_temp = 22.5f;      // dummy
float current_temp = 21.3f;  // dummy

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_BUILTIN_INACTIVE);
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("BLE Thermostat Server");

  ble_initialize_gatt_db();
  ble_start_advertising();
  Serial.println("BLE advertisement started");
}

void loop() {
  // nothing to do here, BLE stack drives event handling
}

/* Bluetooth stack event handler */
void sl_bt_on_event(sl_bt_msg_t *evt) {
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
        ble_start_advertising();
        Serial.println("BLE advertisement restarted");
      break;

    default:
      break;
  }
}

/*** advertising (same pattern as original example) ***/
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

/*** GATT DB: Thermostat service + characteristic (8 bytes) ***/
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
