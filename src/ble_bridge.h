#pragma once
#include <stdint.h>
#include <stddef.h>

// Nordic UART Service over BLE — the wire-protocol the Claude desktop
// apps speak. We expose a small line-buffered API so the rest of the
// firmware can treat it like a serial port:
//
//   Service       6e400001-b5a3-f393-e0a9-e50e24dcca9e
//   RX (write)    6e400002-...      desktop -> device
//   TX (notify)   6e400003-...      device  -> desktop
//
// Implementation is built on btstack's nordic_spp_service_server, which
// uses these exact UUIDs and characteristic semantics.

// Bring BLE up: starts cyw43_arch BT-only, registers the NUS service, and
// kicks off advertising with the given name + NUS service UUID in the
// advertisement payload. Name is what the Claude picker shows; conventionally
// "Claude-XXXX" with XXXX = last 2 BT MAC bytes for uniqueness.
void bleInit(const char* deviceName);

// True between SPP_SERVICE_CONNECTED and SPP_SERVICE_DISCONNECTED — i.e.
// the desktop has subscribed to TX notifications and we have a live link.
bool bleConnected();

// One-word ground-truth status from btstack itself — distinguishes
// "cyw43 never came up" from "advertising but no one connected".
// One of: "off", "init", "halt", "fall", "work", "slep", "sus_", "unkn".
// "work" = HCI_STATE_WORKING = we should be visible.
const char* bleHciState();

// LE Secure Connections bonding state. Stubbed false until we add pairing.
bool bleSecure();

// Non-zero while a 6-digit pairing passkey should be on screen. Stubbed 0
// until we add pairing.
uint32_t blePasskey();

// Erase all stored bonds. No-op stub until we add pairing.
void bleClearBonds();

// RX bytes the desktop has written to us, FIFO. The Claude desktop sends
// newline-delimited JSON; consumers accumulate until '\n' then parse.
size_t bleAvailable();
int    bleRead();    // -1 if empty

// Queue bytes for TX notification to the desktop. Returns bytes accepted
// (may be less than len if the queue is full).
size_t bleWrite(const uint8_t* data, size_t len);
