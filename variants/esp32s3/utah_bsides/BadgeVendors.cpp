#include "BadgeVendors.h"

#include <string.h>

// generated/vendors.S (layout in tools/make_vendors.py)
extern "C" const uint8_t badge_vendors_start[];
extern "C" const uint8_t badge_vendors_end[];

namespace badge
{

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t le24(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16);
}

struct Table {
    const uint8_t *oui = nullptr, *ids = nullptr;
    const char *names = nullptr;
    uint32_t nOui = 0, nIds = 0, namesLen = 0;
};

static const Table &table()
{
    static Table t;
    static bool done = false;
    if (done)
        return t;
    done = true;
    const uint8_t *d = badge_vendors_start;
    size_t len = badge_vendors_end - badge_vendors_start;
    if (len < 12 || memcmp(d, "BVN3", 4) != 0)
        return t;
    uint32_t nOui = le32(d + 4), nIds = le32(d + 8);
    size_t namesAt = 12 + (size_t)nOui * 6 + (size_t)nIds * 5;
    if (namesAt > len)
        return t;
    t.oui = d + 12;
    t.ids = t.oui + nOui * 6;
    t.names = (const char *)d + namesAt;
    t.namesLen = len - namesAt;
    t.nOui = nOui;
    t.nIds = nIds;
    return t;
}

static const char *nameAt(const Table &t, uint32_t off)
{
    return off < t.namesLen ? t.names + off : nullptr;
}

const char *vendorFromAddress(const uint8_t mac[6])
{
    const Table &t = table();
    uint32_t key = ((uint32_t)mac[0] << 16) | (mac[1] << 8) | mac[2];
    uint32_t lo = 0, hi = t.nOui;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t *r = t.oui + mid * 6;
        uint32_t k = ((uint32_t)r[0] << 16) | (r[1] << 8) | r[2];
        if (k == key)
            return nameAt(t, le24(r + 3));
        if (k < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}

const char *vendorFromCompanyId(uint16_t id)
{
    const Table &t = table();
    uint32_t lo = 0, hi = t.nIds;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t *r = t.ids + mid * 5;
        uint16_t k = r[0] | (r[1] << 8);
        if (k == id)
            return nameAt(t, le24(r + 2));
        if (k < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return nullptr;
}

} // namespace badge
