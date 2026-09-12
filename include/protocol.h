#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "config.h"

namespace quiz {
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint16_t PROTOCOL_MAGIC = 0x4251;
constexpr size_t WIRE_MESSAGE_SIZE = 42;
enum class DeviceRole { MASTER, SLAVE };
enum class Command : uint8_t {
    BUTTON_PRESSED = 1, WINNER, RESET_ROUND, LOCK_ROUND, ACK, PING, STATUS
};
enum class MasterState { INITIALIZING, ARMED, LOCKED };
enum class SlaveState { INITIALIZING, READY, WAITING_MASTER, WINNER, LOCKED };
constexpr uint8_t FLAG_SYNC_REQUEST = 1;

// Estrutura logica: serializacao explicita little-endian, sem depender de padding.
struct QuizMessage {
    uint8_t protocolVersion = PROTOCOL_VERSION;
    Command command = Command::STATUS;
    uint8_t senderID = MASTER_ID;
    uint8_t targetID = MASTER_ID;
    uint16_t roundID = 0;
    uint32_t sequence = 0;
    uint64_t sessionID = 0;       // Contador persistente de boot do MASTER.
    uint64_t deviceBootID = 0;    // Boot do SLAVE; MASTER ecoa o boot destinatario.
    uint32_t roundGeneration = 0; // Incrementa no wrap de roundID.
    uint32_t ackSequence = 0;
    uint8_t winnerID = 0;
    uint8_t flags = 0;
    Command ackCommand = Command::ACK;
};
using WireMessage = std::array<uint8_t, WIRE_MESSAGE_SIZE>;
WireMessage encodeMessage(const QuizMessage& message);
bool decodeMessage(const uint8_t* data, size_t size, QuizMessage& message);
bool validMessage(const QuizMessage& message);
bool sameRound(const QuizMessage& a, const QuizMessage& b);
int compareRound(const QuizMessage& a, const QuizMessage& b);
bool newerSequence(uint32_t candidate, uint32_t previous);

class Transport {
public:
    virtual ~Transport() = default;
    // false = ocupado/erro; nunca aguarda radio e nunca enfileira atras de STATUS.
    virtual bool trySend(const QuizMessage& message) = 0;
};
class MessageSender {
public:
    explicit MessageSender(Transport& transport) : transport_(transport) {}
    bool send(QuizMessage& message) {
        message.sequence = sequence_ + 1U;
        if (!transport_.trySend(message)) return false;
        sequence_ = message.sequence;
        return true;
    }
private:
    Transport& transport_;
    uint32_t sequence_ = 0;
};
}  // namespace quiz
