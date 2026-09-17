// nfc_scanner.h
#pragma once
#include <stdint.h>
#include <stdbool.h>

void nfc_scanner_start(void);

// Shared state (protected by a mutex in real use; simple globals OK for lab)
extern uint8_t  g_current_uid[10];
extern uint8_t  g_current_uid_len;
extern bool     g_tag_present;
extern bool     g_learning_mode;   // true when waiting for a new target