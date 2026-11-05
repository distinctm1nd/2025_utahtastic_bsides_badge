#include "configuration.h"

#include "BadgePositions.h"

#include "BadgeMesh.h"
#include "NodeDB.h"
#include "gps/RTC.h"
#include <algorithm>
#include <math.h>
#include <stdio.h>

namespace badge
{

bool posMine(int32_t &lat, int32_t &lon)
{
    if (!nodeDB)
        return false;
    const meshtastic_NodeInfoLite *me = nodeDB->getMeshNode(nodeDB->getNodeNum());
    if (!me || !me->has_position || (!me->position.latitude_i && !me->position.longitude_i))
        return false;
    lat = me->position.latitude_i;
    lon = me->position.longitude_i;
    return true;
}

int posList(NodePos *out, int max)
{
    if (!nodeDB)
        return 0;
    int32_t mlat = 0, mlon = 0;
    bool mine = posMine(mlat, mlon);
    uint32_t now = getValidTime(RTCQualityDevice);
    int n = 0;
    for (size_t i = 0; i < nodeDB->getNumMeshNodes() && n < max; i++) {
        const meshtastic_NodeInfoLite *nd = nodeDB->getMeshNodeByIndex(i);
        if (!nd || nd->num == nodeDB->getNodeNum() || !nd->has_position ||
            (!nd->position.latitude_i && !nd->position.longitude_i))
            continue;
        NodePos &p = out[n++];
        p.num = nd->num;
        meshNodeName(nd->num, p.name, sizeof(p.name));
        p.lat = nd->position.latitude_i;
        p.lon = nd->position.longitude_i;
        p.ageS = (now && nd->last_heard && now > nd->last_heard) ? now - nd->last_heard : 0;
        p.hops = nd->has_hops_away ? nd->hops_away : -1;
        p.distM = -1;
        p.bearing = 0;
        if (mine) { // flat-earth approximation: fine at mesh distances
            const double R = 6371000.0, k = M_PI / 180e7;
            double dy = (p.lat - mlat) * k * R;
            double dx = (p.lon - mlon) * k * R * cos(mlat * k);
            p.distM = (float)sqrt(dx * dx + dy * dy);
            double b = atan2(dx, dy) * 180 / M_PI;
            p.bearing = (float)(b < 0 ? b + 360 : b);
        }
    }
    std::sort(out, out + n, [](const NodePos &a, const NodePos &b) { return a.distM < b.distM; });
    return n;
}

const char *posCompass(float bearing)
{
    static const char *const P[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return P[(int)((bearing + 22.5f) / 45) % 8];
}

} // namespace badge
