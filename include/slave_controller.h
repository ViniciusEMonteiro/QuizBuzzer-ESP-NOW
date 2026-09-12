#pragma once
#include "protocol.h"
namespace quiz {
class SlaveController {
public:
    SlaveController(Transport& transport, uint8_t id, uint64_t bootID);
    bool onButton(uint32_t now);
    bool onMessage(const QuizMessage& message, uint32_t now);
    void tick(uint32_t now);
    SlaveState state() const { return state_; }
    uint16_t roundID() const { return round_.roundID; }
    bool synchronized() const { return synchronized_; }
private:
    QuizMessage makeMessage(Command command) const;
    bool sendButton(uint32_t now);
    void requestSync();
    MessageSender sender_;
    uint8_t id_;
    uint64_t bootID_;
    QuizMessage round_{};
    SlaveState state_ = SlaveState::INITIALIZING;
    bool hasRound_ = false;
    bool synchronized_ = false;
    bool pressedThisRound_ = false;
    bool roundLocked_ = false;
    bool hasMasterSequence_ = false;
    uint32_t masterSequence_ = 0;
    uint64_t minimumSession_ = 0;
    uint32_t lastMaster_ = 0;
    uint32_t pressedAt_ = 0;
    uint32_t buttonSentAt_ = 0;
    uint8_t buttonAttempts_ = 0;
    bool decisionTimedOut_ = false;
    bool ackPending_ = false;
    QuizMessage ack_{};
    bool statusPending_ = true;
    bool syncRequested_ = true;
    uint32_t lastStatus_ = 0;
};
}  // namespace quiz
