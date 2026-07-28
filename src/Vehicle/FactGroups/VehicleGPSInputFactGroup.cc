/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "VehicleGPSInputFactGroup.h"
#include "Vehicle.h"
#include "QGCGeo.h"

#include <QtPositioning/QGeoCoordinate>

#include <cmath>
#include <cstdint>

void VehicleGPSInputFactGroup::handleMessage(Vehicle *vehicle, const mavlink_message_t &message)
{
    Q_UNUSED(vehicle);

    switch (message.msgid) {
    case MAVLINK_MSG_ID_GPS_INPUT:
        _handleGpsInput(message);
        break;
    default:
        break;
    }
}

void VehicleGPSInputFactGroup::_handleGpsInput(const mavlink_message_t &message)
{
    mavlink_gps_input_t gpsInput{};
    mavlink_msg_gps_input_decode(&message, &gpsInput);

    static constexpr uint16_t ignoreHdop = 1u << 1;
    static constexpr uint16_t ignoreVdop = 1u << 2;
    static constexpr uint16_t ignoreHorizontalAccuracy = 1u << 6;
    static constexpr uint16_t ignoreVerticalAccuracy = 1u << 7;

    lat()->setRawValue(gpsInput.lat * 1e-7);
    lon()->setRawValue(gpsInput.lon * 1e-7);
    mgrs()->setRawValue(QGCGeo::convertGeoToMGRS(QGeoCoordinate(gpsInput.lat * 1e-7, gpsInput.lon * 1e-7, gpsInput.alt)));
    count()->setRawValue((gpsInput.satellites_visible == UINT8_MAX) ? 0 : gpsInput.satellites_visible);
    hdop()->setRawValue((gpsInput.ignore_flags & ignoreHdop) ? qQNaN() : gpsInput.hdop);
    vdop()->setRawValue((gpsInput.ignore_flags & ignoreVdop) ? qQNaN() : gpsInput.vdop);
    eph()->setRawValue(((gpsInput.ignore_flags & ignoreHorizontalAccuracy) || std::isnan(gpsInput.horiz_accuracy)) ? qQNaN() : gpsInput.horiz_accuracy);
    epv()->setRawValue(((gpsInput.ignore_flags & ignoreVerticalAccuracy) || std::isnan(gpsInput.vert_accuracy)) ? qQNaN() : gpsInput.vert_accuracy);
    vel()->setRawValue(std::isnan(gpsInput.vn) || std::isnan(gpsInput.ve) ? qQNaN() : std::sqrt((gpsInput.vn * gpsInput.vn) + (gpsInput.ve * gpsInput.ve)));
    yaw()->setRawValue((gpsInput.yaw == 0) ? qQNaN() : (gpsInput.yaw / 100.0));
    lock()->setRawValue(gpsInput.fix_type);

    _setTelemetryAvailable(true);
}
