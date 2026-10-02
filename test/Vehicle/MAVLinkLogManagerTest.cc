/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "MAVLinkLogManagerTest.h"
#include "MAVLinkLogManager.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <QtCore/QStandardPaths>
#include <QtCore/QTemporaryDir>
#include <QtCore/QDir>
#include <QtTest/QTest>

void MAVLinkLogManagerTest::_testInitMAVLinkLogManager()
{
    _connectMockLinkNoInitialConnectSequence();

    MultiVehicleManager *const vehicleMgr = MultiVehicleManager::instance();
    Vehicle *const vehicle = vehicleMgr->activeVehicle();
    MAVLinkLogManager *const mavlinkLogManager = new MAVLinkLogManager(vehicle, this);
    QVERIFY(mavlinkLogManager);
}

void MAVLinkLogManagerTest::_testStreamReassembly()
{
    _connectMockLinkNoInitialConnectSequence();
    MAVLinkLogManager manager(MultiVehicleManager::instance()->activeVehicle());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.path() + QString::fromUtf8("/host-\xc3\xa9");
    QVERIFY(QDir().mkpath(path));
    MAVLinkLogProcessor processor;
    QVERIFY(processor.create(&manager, path, 1));
    const QByteArray header = QByteArray::fromHex("554c6f67011235010000000000000000");
    const QByteArray record = QByteArray::fromHex("04004401020304");
    QVERIFY(processor.processStreamData(65534, 0, header + record.left(4)));
    QVERIFY(processor.processStreamData(65535, 255, record.mid(4, 1)));
    QVERIFY(processor.processStreamData(65535, 255, record.mid(4, 1))); // Duplicate.
    QVERIFY(processor.processStreamData(0, 2, record.right(2) + record)); // Wrap.
    QVERIFY(processor.processStreamData(65534, 0, record)); // Reordered.
    QVERIFY(processor.processStreamData(2, 1, QByteArray(1, 'x') + record)); // Gap.
    processor.close();
    QFile file(processor.fileName());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), header + record + record + QByteArray::fromHex("02004f0a00") + record);

    MAVLinkLogProcessor headerProcessor;
    QVERIFY(headerProcessor.create(&manager, path, 2));
    QVERIFY(headerProcessor.processStreamData(0, 0, header + record, true));
    QVERIFY(headerProcessor.processStreamData(2, 0, record, true));
    QVERIFY(headerProcessor.close());
    QFile headerFile(headerProcessor.fileName());
    QVERIFY(headerFile.open(QIODevice::ReadOnly));
    QCOMPARE(headerFile.readAll(), header + record + record); // No dropout records inside the header section.
}

void MAVLinkLogManagerTest::_testHostDestinationFailure()
{
    _connectMockLinkNoInitialConnectSequence();
    MAVLinkLogManager manager(MultiVehicleManager::instance()->activeVehicle());
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    manager.startHostLogging(directory.path() + "/missing");
    QVERIFY(!manager.logRunning());
    QVERIFY(!manager.hostLogStatus().isEmpty());
    MAVLinkLogProcessor processor;
    QVERIFY(!processor.create(&manager, directory.path() + "/missing", 1));
}
