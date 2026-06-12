/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "VehicleGPSFactGroup.h"

class VehicleGPSInputFactGroup : public VehicleGPSFactGroup
{
    Q_OBJECT

public:
    explicit VehicleGPSInputFactGroup(QObject *parent = nullptr)
        : VehicleGPSFactGroup(parent) {}

    // Overrides from VehicleGPSFactGroup
    void handleMessage(Vehicle *vehicle, const mavlink_message_t &message) final;

private:
    void _handleGpsInput(const mavlink_message_t &message);
};
