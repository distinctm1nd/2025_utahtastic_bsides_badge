// Maker names from the IEEE address-prefix (OUI) list and the Bluetooth SIG company IDs,
// built from tools/data by tools/make_vendors.py.
#pragma once
#include <stdint.h>

namespace badge
{

const char *vendorFromAddress(const uint8_t mac[6]); // nullptr if the prefix isn't registered
const char *vendorFromCompanyId(uint16_t id);        // nullptr if the ID isn't assigned

} // namespace badge
