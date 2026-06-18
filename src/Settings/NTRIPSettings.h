/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtQmlIntegration/QtQmlIntegration>

#include "SettingsGroup.h"

class NTRIPSettings : public SettingsGroup
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
public:
    explicit NTRIPSettings(QObject* parent = nullptr);
    DEFINE_SETTING_NAME_GROUP()

    DEFINE_SETTINGFACT(enabled)
    DEFINE_SETTINGFACT(host)
    DEFINE_SETTINGFACT(port)
    DEFINE_SETTINGFACT(mountpoint)
    DEFINE_SETTINGFACT(username)
    DEFINE_SETTINGFACT(password)
    DEFINE_SETTINGFACT(sendGGA)
    DEFINE_SETTINGFACT(ggaInterval)
};
