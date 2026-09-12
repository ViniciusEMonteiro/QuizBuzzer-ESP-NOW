#pragma once
#include "protocol.h"

namespace quiz {
class MasterController {
public:
    MasterController(Transport& transport, uint64_t bootID);
    void begin(bool autoArm = AUTO_ARM_ON_BOOT);
    bool newRound();
    bool onMessage(const QuizMessage& message, uint32_t now);
    void tick(uint32_t now);
    MasterState state() const { return state_; }
    uint8_t winnerID() const { return winnerID_; }
    uint16_t roundID() const { return round_.roundID; }
    uint32_t roundGeneration() const { return round_.roundGeneration; }
    bool acceptButtons() const { return state_ == MasterState::ARMED; }
    bool online(uint8_t id) const { return id > 0 && id <= MAX_SLAVES && peers_[id].online; }
    bool confirmed(uint8_t id) const { return id > 0 && id <= MAX_SLAVES && !peers_[id].pending; }
private:
    struct Attempt { uint32_t sequence = 0; Command command = Command::ACK; bool valid = false; };
    struct Peer {
        uint64_t bootID = 0;
        uint32_t lastSequence = 0;
        uint32_t lastSeen = 0;
        uint32_t lastSent = 0;
        bool hasSequence = false;
        bool online = false;
        bool synchronized = false;
        bool pending = true;
        uint8_t attempts = 0;
        size_t historyIndex = 0;
        std::array<Attempt, ACK_HISTORY_SIZE> history{};
    };
    void invalidateState();
    bool sendState(uint8_t id, uint32_t now);
    Command desiredCommand(uint8_t id) const;
    MessageSender sender_;
    QuizMessage round_{};
    MasterState state_ = MasterState::INITIALIZING;
    uint8_t winnerID_ = 0;
    std::array<Peer, MAX_SLAVES + 1> peers_{};
    uint8_t stateCursor_ = 1;
    uint8_t pingCursor_ = 1;
    uint32_t lastPing_ = 0;
};
}  // namespace quiz
