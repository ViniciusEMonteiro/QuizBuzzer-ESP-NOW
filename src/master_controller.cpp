#include "master_controller.h"
#include <limits>

namespace quiz {
MasterController::MasterController(Transport& transport, uint64_t bootID) : sender_(transport) {
    round_.sessionID = bootID;
}
void MasterController::begin(bool autoArm) {
    if (autoArm) newRound();
}
void MasterController::invalidateState() {
    for (uint8_t id = 1; id <= MAX_SLAVES; ++id) {
        peers_[id].pending = true;
        peers_[id].attempts = 0;
        peers_[id].history = {};
        peers_[id].historyIndex = 0;
    }
}
bool MasterController::newRound() {
    if (!round_.sessionID) return false;
    if (round_.roundID == std::numeric_limits<uint16_t>::max()) {
        if (round_.roundGeneration == std::numeric_limits<uint32_t>::max()) return false;
        ++round_.roundGeneration;
    }
    ++round_.roundID; // uint16_t: 65535 -> 0, distinguido por roundGeneration.
    winnerID_ = 0;
    state_ = MasterState::ARMED;
    for (auto& peer : peers_) peer.synchronized = false;
    invalidateState();
    return true;
}
Command MasterController::desiredCommand(uint8_t id) const {
    if (state_ == MasterState::ARMED) return Command::RESET_ROUND;
    if (winnerID_ == id && peers_[id].synchronized) return Command::WINNER;
    return Command::LOCK_ROUND;
}
bool MasterController::onMessage(const QuizMessage& m, uint32_t now) {
    if (!validMessage(m) || m.targetID != MASTER_ID || m.senderID == MASTER_ID) return false;
    Peer& peer = peers_[m.senderID];
    if (m.deviceBootID < peer.bootID) return false;
    if (m.deviceBootID > peer.bootID) {
        // So STATUS registra um boot novo; BUTTON nunca muda a identidade da bancada.
        if (m.command != Command::STATUS) return false;
        peer = Peer{};
        peer.bootID = m.deviceBootID;
    }
    if (m.command != Command::STATUS && !sameRound(m, round_)) return false;
    if (peer.hasSequence && !newerSequence(m.sequence, peer.lastSequence)) return false;
    peer.hasSequence = true;
    peer.lastSequence = m.sequence;
    peer.online = true;
    peer.lastSeen = now;
    if (m.command == Command::BUTTON_PRESSED) {
        if (state_ != MasterState::ARMED) return false;
        // BUTTON pode ultrapassar o ACK de RESET, pois tem prioridade no SLAVE.
        // O boot registrado e a rodada correta ja comprovam a sincronizacao.
        peer.synchronized = true;
        // Unico escritor: tarefa do jogo. Travamento ANTES de qualquer envio/log.
        winnerID_ = m.senderID;
        state_ = MasterState::LOCKED;
        invalidateState();
        return true;
    }
    if (m.command == Command::STATUS) {
        if ((m.flags & FLAG_SYNC_REQUEST) || !sameRound(m, round_)) {
            peer.synchronized = false;
            peer.pending = true;
            peer.attempts = 0;
            peer.history = {};
        }
        return true;
    }
    if (m.command != Command::ACK || !peer.pending) return false;
    bool matches = false;
    for (const auto& attempt : peer.history) {
        if (attempt.valid && attempt.sequence == m.ackSequence && attempt.command == m.ackCommand) {
            matches = true;
            break;
        }
    }
    if (!matches) return false;
    const Command expected = desiredCommand(m.senderID);
    if (m.ackCommand != expected) return false;
    peer.synchronized = true;
    // SLAVE que reiniciou recebe LOCK da rodada antes da confirmacao WINNER.
    peer.pending = desiredCommand(m.senderID) != m.ackCommand;
    peer.attempts = 0;
    peer.history = {};
    return true;
}
bool MasterController::sendState(uint8_t id, uint32_t now) {
    Peer& peer = peers_[id];
    if (!peer.pending || !peer.bootID) return false;
    const uint32_t interval = peer.attempts < STATE_FAST_ATTEMPTS ? STATE_RETRY_MS : STATE_SLOW_RETRY_MS;
    if (peer.attempts && !elapsed(now, peer.lastSent, interval)) return false;
    QuizMessage m = round_;
    m.targetID = id;
    m.deviceBootID = peer.bootID;
    m.command = desiredCommand(id);
    m.winnerID = winnerID_;
    if (!sender_.send(m)) return false;
    peer.history[peer.historyIndex] = {m.sequence, m.command, true};
    peer.historyIndex = (peer.historyIndex + 1) % ACK_HISTORY_SIZE;
    if (peer.attempts < STATE_FAST_ATTEMPTS) ++peer.attempts;
    peer.lastSent = now;
    return true;
}
void MasterController::tick(uint32_t now) {
    for (uint8_t id = 1; id <= MAX_SLAVES; ++id) {
        if (peers_[id].online && elapsed(now, peers_[id].lastSeen, ONLINE_TIMEOUT_MS))
            peers_[id].online = false;
    }
    // A decisao unicast sai antes de LOCK e da supervisao.
    if (winnerID_ && sendState(winnerID_, now)) return;
    for (uint8_t count = 0; count < MAX_SLAVES; ++count) {
        const uint8_t id = stateCursor_;
        stateCursor_ = static_cast<uint8_t>(stateCursor_ % MAX_SLAVES + 1);
        if (id != winnerID_ && sendState(id, now)) return;
    }
    if (!elapsed(now, lastPing_, SUPERVISION_POLL_MS)) return;
    // PING de descoberta continua mesmo com supervisao desabilitada.
    for (uint8_t count = 0; count < MAX_SLAVES; ++count) {
        const uint8_t id = pingCursor_;
        pingCursor_ = static_cast<uint8_t>(pingCursor_ % MAX_SLAVES + 1);
        if (!SUPERVISION_ENABLED && peers_[id].bootID) continue;
        QuizMessage ping = round_;
        ping.command = Command::PING; ping.targetID = id;
        ping.deviceBootID = peers_[id].bootID; ping.winnerID = winnerID_;
        if (sender_.send(ping)) lastPing_ = now;
        break;
    }
}
}  // namespace quiz
