/*
 * hci_transport_cyw43.h — BTstack HCI transport for CYW43439
 *
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * CYW43 ドライバの cyw43_bluetooth_hci_read/write/init を
 * BTstack の hci_transport_t インタフェースにブリッジする。
 */

#ifndef HCI_TRANSPORT_CYW43_H
#define HCI_TRANSPORT_CYW43_H

#include "hci_transport.h"

const hci_transport_t *hci_transport_cyw43_instance(void);

#endif /* HCI_TRANSPORT_CYW43_H */
