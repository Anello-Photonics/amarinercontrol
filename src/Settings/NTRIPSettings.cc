/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "NTRIPSettings.h"

DECLARE_SETTINGGROUP(NTRIP, "NTRIP") {}

DECLARE_SETTINGSFACT(NTRIPSettings, enabled)
DECLARE_SETTINGSFACT(NTRIPSettings, host)
DECLARE_SETTINGSFACT(NTRIPSettings, port)
DECLARE_SETTINGSFACT(NTRIPSettings, mountpoint)
DECLARE_SETTINGSFACT(NTRIPSettings, username)
DECLARE_SETTINGSFACT(NTRIPSettings, password)
DECLARE_SETTINGSFACT(NTRIPSettings, sendGGA)
DECLARE_SETTINGSFACT(NTRIPSettings, ggaInterval)
