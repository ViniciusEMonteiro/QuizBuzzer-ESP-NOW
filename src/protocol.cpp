#include "protocol.h"

namespace quiz {
namespace {
void put(uint8_t* p, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
uint64_t get(const uint8_t* p, size_t bytes) {
    uint64_t value = 0;
    for (size_t i = 0; i < bytes; ++i) value |= uint64_t{p[i]} << (8 * i);
    return value;
}
uint16_t crc16(const uint8_t* data, size_t size) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}
bool known(Command command) {
    return command >= Command::BUTTON_PRESSED && command <= Command::STATUS;
}
}
bool validMessage(const QuizMessage& m) {
    if (m.protocolVersion != PROTOCOL_VERSION || !known(m.command) ||
        m.senderID > MAX_SLAVES || m.targetID > MAX_SLAVES ||
        m.winnerID > MAX_SLAVES || (m.flags & ~FLAG_SYNC_REQUEST) != 0 ||
        !known(m.ackCommand)) return false;
    const bool fromMaster = m.senderID == MASTER_ID;
    if (fromMaster == (m.targetID == MASTER_ID)) return false;
    if (fromMaster) {
        if (!m.sessionID || m.flags || m.ackSequence || m.ackCommand != Command::ACK)
            return false;
        if (m.command != Command::PING && !m.deviceBootID) return false;
        if (m.command == Command::RESET_ROUND) return m.winnerID == 0;
        if (m.command == Command::WINNER) return m.winnerID == m.targetID;
        return m.command == Command::LOCK_ROUND || m.command == Command::PING;
    }
    if (!m.deviceBootID || m.winnerID) return false;
    if (m.command == Command::STATUS)
        return m.ackSequence == 0 && m.ackCommand == Command::ACK;
    if (!m.sessionID || m.flags) return false;
    if (m.command == Command::BUTTON_PRESSED)
        return m.ackSequence == 0 && m.ackCommand == Command::ACK;
    return m.command == Command::ACK &&
           (m.ackCommand == Command::RESET_ROUND || m.ackCommand == Command::WINNER ||
            m.ackCommand == Command::LOCK_ROUND);
}
WireMessage encodeMessage(const QuizMessage& m) {
    WireMessage data{};
    put(&data[0], PROTOCOL_MAGIC, 2);
    data[2] = m.protocolVersion; data[3] = static_cast<uint8_t>(m.command);
    data[4] = m.senderID; data[5] = m.targetID;
    put(&data[6], m.roundID, 2); put(&data[8], m.sequence, 4);
    put(&data[12], m.sessionID, 8); put(&data[20], m.deviceBootID, 8);
    put(&data[28], m.roundGeneration, 4); put(&data[32], m.ackSequence, 4);
    data[36] = m.winnerID; data[37] = m.flags;
    data[38] = static_cast<uint8_t>(m.ackCommand); // 39 reservado, sempre zero.
    put(&data[40], crc16(data.data(), 40), 2);
    return data;
}
bool decodeMessage(const uint8_t* data, size_t size, QuizMessage& m) {
    if (!data || size != WIRE_MESSAGE_SIZE || get(data, 2) != PROTOCOL_MAGIC ||
        data[39] != 0 || get(data + 40, 2) != crc16(data, 40)) return false;
    m.protocolVersion = data[2]; m.command = static_cast<Command>(data[3]);
    m.senderID = data[4]; m.targetID = data[5];
    m.roundID = static_cast<uint16_t>(get(data + 6, 2));
    m.sequence = static_cast<uint32_t>(get(data + 8, 4));
    m.sessionID = get(data + 12, 8); m.deviceBootID = get(data + 20, 8);
    m.roundGeneration = static_cast<uint32_t>(get(data + 28, 4));
    m.ackSequence = static_cast<uint32_t>(get(data + 32, 4));
    m.winnerID = data[36]; m.flags = data[37]; m.ackCommand = static_cast<Command>(data[38]);
    return validMessage(m);
}
int compareRound(const QuizMessage& a, const QuizMessage& b) {
    if (a.sessionID != b.sessionID) return a.sessionID > b.sessionID ? 1 : -1;
    if (a.roundGeneration != b.roundGeneration)
        return a.roundGeneration > b.roundGeneration ? 1 : -1;
    if (a.roundID != b.roundID) return a.roundID > b.roundID ? 1 : -1;
    return 0;
}
bool sameRound(const QuizMessage& a, const QuizMessage& b) { return compareRound(a, b) == 0; }
bool newerSequence(uint32_t candidate, uint32_t previous) {
    const uint32_t delta = candidate - previous;
    return delta != 0 && delta < 0x80000000U;
}
}  // namespace quiz
