#pragma once

#include <QtNetwork/QUdpSocket>
#include <QtNetwork/QNetworkInterface>

// Multicast membership is a receive setting, independent of the command destination.
inline QString configureNmeaUdpSocket(QUdpSocket *socket, quint16 port, const QString &groupText)
{
    socket->close();
    QHostAddress group;
    if (!groupText.isEmpty() && (!group.setAddress(groupText) ||
        group.protocol() != QAbstractSocket::IPv4Protocol || !group.isMulticast())) {
        return QObject::tr("Enter an IPv4 multicast group (224.0.0.0 through 239.255.255.255), or leave it blank.");
    }
    if (!socket->bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        return QObject::tr("Cannot open NMEA UDP port: %1").arg(socket->errorString());
    }
    if (groupText.isEmpty()) return {};
    bool joined = false;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            !(iface.flags() & QNetworkInterface::IsRunning) ||
            !(iface.flags() & QNetworkInterface::CanMulticast)) continue;
        bool ipv4 = false;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) ipv4 = true;
        }
        if (ipv4 && socket->joinMulticastGroup(group, iface)) joined = true;
    }
    if (!joined) {
        const QString error = QObject::tr("Cannot join NMEA multicast group %1: %2").arg(groupText, socket->errorString());
        socket->close();
        return error;
    }
    return {};
}
