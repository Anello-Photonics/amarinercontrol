#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QElapsedTimer>

// Observes a copy of incoming bytes; never consumes the position parser's data.
class NmeaReceiveTracker
{
public:
    void reset() { _line.clear(); _lastValid.invalidate(); _received = false; _count = 0; }
    void feed(const QByteArray &bytes) {
        if (!bytes.isEmpty()) _received = true;
        for (const char ch : bytes) {
            if (ch == '$' || ch == '!') {
                _line = QByteArray(1, ch);
            } else if (ch == '\r' || ch == '\n') {
                if (validSentence(_line)) { _lastValid.start(); ++_count; }
                _line.clear();
            } else if (!_line.isEmpty()) {
                if (_line.size() >= 1024 || ch < 0x20 || ch > 0x7e) _line.clear();
                else _line.append(ch);
            }
        }
    }
    bool receiving() const { return _lastValid.isValid() && _lastValid.elapsed() < 5000; }
    bool everValid() const { return _lastValid.isValid(); }
    bool receivedBytes() const { return _received; }
    quint64 count() const { return _count; }
    static bool validSentence(const QByteArray &line) {
        if (line.size() < 9 || (line.front() != '$' && line.front() != '!')) return false;
        const auto star = line.indexOf('*');
        const auto comma = line.indexOf(',');
        if (star < 0 || star != line.size() - 3 || comma < 5 || comma > star) return false;
        for (int i = 1; i < comma; ++i) {
            const char ch = line.at(i);
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) return false;
        }
        auto hex = [](char ch) {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            return -1;
        };
        const int high = hex(line.at(star + 1)), low = hex(line.at(star + 2));
        if (high < 0 || low < 0) return false;
        unsigned char checksum = 0;
        for (int i = 1; i < star; ++i) {
            const char ch = line.at(i);
            if (ch < 0x20 || ch > 0x7e || ch == '$' || ch == '!') return false;
            checksum ^= static_cast<unsigned char>(ch);
        }
        return checksum == ((high << 4) | low);
    }
private:
    QByteArray _line;
    QElapsedTimer _lastValid;
    bool _received = false;
    quint64 _count = 0;
};
