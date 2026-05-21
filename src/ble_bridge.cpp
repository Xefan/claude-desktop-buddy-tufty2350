// BLE bridge — Nordic UART Service over btstack, running on the Tufty 2350's
// CYW43/RM2 wireless module. Replaces upstream's Arduino-ESP32 BLEDevice
// implementation. Public API is `ble_bridge.h`.
//
// Architecture:
//   - cyw43_arch_threadsafe_background drives btstack's run loop in IRQ
//     context, so main.cpp doesn't have to poll anything.
//   - att_server / nordic_spp_service_server provide the NUS GATT service
//     (TX notify + RX write, 6e400001/2/3 UUIDs).
//   - RX bytes from packet handler land in a 2KB ring buffer; bleRead/
//     bleAvailable drain it from the main loop.
//   - bleWrite enqueues into a TX ring; a can_send_now callback drains it
//     in MTU-sized chunks back to the desktop.

#include "ble_bridge.h"

#include <cstdio>
#include <cstring>

#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "ble/gatt-service/nordic_spp_service_server.h"

// Generated from src/nus_service.gatt by pico_btstack_make_gatt_header.
#include "nus_service.h"

namespace {

constexpr size_t RX_CAP = 2048;
constexpr size_t TX_CAP = 2048;

volatile uint8_t  rx_buf[RX_CAP];
volatile size_t   rx_head = 0;
volatile size_t   rx_tail = 0;

volatile uint8_t  tx_buf[TX_CAP];
volatile size_t   tx_head = 0;
volatile size_t   tx_tail = 0;

hci_con_handle_t  active_con   = HCI_CON_HANDLE_INVALID;
bool              notif_on     = false;
uint16_t          att_mtu      = ATT_DEFAULT_MTU;
HCI_STATE         hci_state    = HCI_STATE_OFF;

btstack_packet_callback_registration_t  hci_event_cb_reg;
btstack_context_callback_registration_t send_request;

uint8_t adv_data[31];
uint8_t adv_data_len = 0;
uint8_t scan_resp[31];
uint8_t scan_resp_len = 0;

// Build the advertisement payload. We put the COMPLETE local name (type 0x09)
// plus the NUS service UUID directly in adv_data, since some pickers ignore
// the shortened-name type (0x08) and never read scan response. The name is
// kept short ("Claude" / 6 chars) so this all fits comfortably in the 31-byte
// legacy adv limit (3 + 8 + 18 = 29 bytes used). MAC-suffix uniqueness is
// dropped here because there's no room — pickers identify by MAC anyway.
void build_adv_payloads(const char* name) {
    static const uint8_t NUS_UUID_LE[16] = {
        0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
        0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e,
    };
    size_t name_len = strlen(name);
    // adv budget for name = 31 - flags(3) - uuid(18) - record_header(2) = 8
    if (name_len > 8) name_len = 8;

    uint8_t* p = adv_data;
    *p++ = 0x02; *p++ = BLUETOOTH_DATA_TYPE_FLAGS; *p++ = 0x06;
    *p++ = (uint8_t)(name_len + 1);
    *p++ = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(p, name, name_len); p += name_len;
    *p++ = 0x11;
    *p++ = BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS;
    memcpy(p, NUS_UUID_LE, 16); p += 16;
    adv_data_len = p - adv_data;

    scan_resp_len = 0;
}

void rx_push_byte(uint8_t b) {
    size_t next = (rx_head + 1) % RX_CAP;
    if (next == rx_tail) return;          // full — drop
    rx_buf[rx_head] = b;
    rx_head = next;
}

bool tx_pop_byte(uint8_t* out) {
    if (tx_head == tx_tail) return false;
    *out = tx_buf[tx_tail];
    tx_tail = (tx_tail + 1) % TX_CAP;
    return true;
}

size_t tx_available() {
    return (tx_head + TX_CAP - tx_tail) % TX_CAP;
}

// Drain up to one MTU-sized chunk from the TX ring into a notification.
void can_send_cb(void* /*ctx*/) {
    if (active_con == HCI_CON_HANDLE_INVALID || !notif_on) return;
    if (tx_available() == 0) return;

    uint16_t max_payload = att_mtu - 3;   // ATT header overhead
    if (max_payload > 200) max_payload = 200;
    uint8_t chunk[200];
    uint16_t n = 0;
    while (n < max_payload && tx_pop_byte(&chunk[n])) n++;
    if (n == 0) return;

    nordic_spp_service_server_send(active_con, chunk, n);

    // If there's still data queued, ask for another can_send slot.
    if (tx_available() > 0) {
        nordic_spp_service_server_request_can_send_now(&send_request, active_con);
    }
}

// HCI events — wake-up notification, connection complete, advertising start.
void hci_event_handler(uint8_t type, uint16_t /*ch*/, uint8_t* packet, uint16_t /*size*/) {
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE: {
            hci_state = (HCI_STATE)btstack_event_state_get_state(packet);
            printf("[ble] hci state = %d\n", (int)hci_state);
            if (hci_state == HCI_STATE_WORKING) {
                bd_addr_t local;
                gap_local_bd_addr(local);
                printf("[ble] up: %s\n", bd_addr_to_str(local));
            }
            break;
        }
        default:
            break;
    }
}

// ATT events — MTU exchange notifies us of the negotiated link MTU so we
// can size TX chunks correctly.
void att_event_handler(uint8_t type, uint16_t /*ch*/, uint8_t* packet, uint16_t /*size*/) {
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
        case ATT_EVENT_MTU_EXCHANGE_COMPLETE:
            att_mtu = att_event_mtu_exchange_complete_get_MTU(packet);
            break;
        case ATT_EVENT_DISCONNECTED:
            active_con = HCI_CON_HANDLE_INVALID;
            notif_on = false;
            att_mtu = ATT_DEFAULT_MTU;
            break;
        default:
            break;
    }
}

// nordic_spp_service callbacks — connect/disconnect of the SPP "channel"
// (= client subscribed to TX notifications) and inbound RX data.
void spp_event_handler(uint8_t type, uint16_t /*ch*/, uint8_t* packet, uint16_t size) {
    switch (type) {
        case HCI_EVENT_PACKET:
            if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) break;
            switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
                    active_con = gattservice_subevent_spp_service_connected_get_con_handle(packet);
                    notif_on = true;
                    break;
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
                    active_con = HCI_CON_HANDLE_INVALID;
                    notif_on = false;
                    break;
                default: break;
            }
            break;
        case RFCOMM_DATA_PACKET:
            for (uint16_t i = 0; i < size; i++) rx_push_byte(packet[i]);
            break;
        default: break;
    }
}

} // anonymous namespace

void bleInit(const char* deviceName) {
    printf("[ble] bleInit start\n");
    int err = cyw43_arch_init();
    if (err) {
        printf("[ble] cyw43_arch_init failed: %d\n", err);
        hci_state = HCI_STATE_OFF;
        return;
    }
    printf("[ble] cyw43_arch_init ok\n");

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);   // no pairing UX yet

    att_server_init(profile_data, NULL, NULL);
    att_server_register_packet_handler(att_event_handler);

    nordic_spp_service_server_init(spp_event_handler);

    build_adv_payloads(deviceName);
    printf("[ble] advertising name: '%s'\n", deviceName);

    bd_addr_t null_addr; memset(null_addr, 0, sizeof(null_addr));
    gap_advertisements_set_params(
        /* adv_int_min */ 0x0030,
        /* adv_int_max */ 0x0030,
        /* adv_type    */ 0x00,                  // ADV_IND
        /* peer addr type */ 0x00,
        null_addr,
        /* channel map */ 0x07,
        /* filter policy */ 0x00);
    gap_advertisements_set_data(adv_data_len, adv_data);
    gap_scan_response_set_data(scan_resp_len, scan_resp);
    gap_advertisements_enable(1);

    hci_event_cb_reg.callback = &hci_event_handler;
    hci_add_event_handler(&hci_event_cb_reg);

    send_request.callback = &can_send_cb;
    send_request.context  = NULL;

    hci_power_control(HCI_POWER_ON);
    printf("[ble] init done, advertising as '%s'\n", deviceName);
}

bool bleConnected() { return active_con != HCI_CON_HANDLE_INVALID && notif_on; }
bool bleSecure()    { return false; }                 // pairing not wired yet
uint32_t blePasskey() { return 0; }
void bleClearBonds() {}                               // no bonds stored yet

size_t bleAvailable() {
    return (rx_head + RX_CAP - rx_tail) % RX_CAP;
}

int bleRead() {
    if (rx_tail == rx_head) return -1;
    int b = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) % RX_CAP;
    return b;
}

const char* bleHciState() {
    switch (hci_state) {
        case HCI_STATE_OFF:                    return "off";
        case HCI_STATE_INITIALIZING:           return "init";
        case HCI_STATE_WORKING:                return "work";
        case HCI_STATE_HALTING:                return "halt";
        case HCI_STATE_FALLING_ASLEEP:         return "fall";
        case HCI_STATE_SLEEPING:               return "slep";
        default:                                return "unkn";
    }
}

size_t bleWrite(const uint8_t* data, size_t len) {
    size_t written = 0;
    for (size_t i = 0; i < len; i++) {
        size_t next = (tx_head + 1) % TX_CAP;
        if (next == tx_tail) break;                   // full
        tx_buf[tx_head] = data[i];
        tx_head = next;
        written++;
    }
    if (written > 0 && bleConnected()) {
        nordic_spp_service_server_request_can_send_now(&send_request, active_con);
    }
    return written;
}
