#include "slave_controller.h"
namespace quiz {
SlaveController::SlaveController(Transport& transport, uint8_t id, uint64_t bootID)
    : sender_(transport), id_(id), bootID_(bootID) {}
QuizMessage SlaveController::makeMessage(Command command) const {
    QuizMessage m{};
    m.command = command; m.senderID = id_; m.targetID = MASTER_ID;
    m.deviceBootID = bootID_; m.sessionID = round_.sessionID;
    m.roundID = round_.roundID; m.roundGeneration = round_.roundGeneration;
    return m;
}
void SlaveController::requestSync() {
    synchronized_ = false;
    syncRequested_ = true;
    statusPending_ = true;
    ackPending_ = false;
    if (state_ == SlaveState::READY) state_ = SlaveState::INITIALIZING;
}
bool SlaveController::onButton(uint32_t now) {
    if (!synchronized_ || state_ != SlaveState::READY || pressedThisRound_) return false;
    pressedThisRound_ = true;
    state_ = SlaveState::WAITING_MASTER;
    pressedAt_ = now;
    buttonAttempts_ = 0;
    decisionTimedOut_ = false;
    // Nenhum log, LED ou flash neste caminho. Se radio ocupado, tick tenta primeiro.
    sendButton(now);
    return true;
}
bool SlaveController::sendButton(uint32_t now) {
    if (!synchronized_ || state_ != SlaveState::WAITING_MASTER || roundLocked_ ||
        buttonAttempts_ >= BUTTON_MAX_ATTEMPTS ||
        (buttonAttempts_ && !elapsed(now, buttonSentAt_, BUTTON_RETRY_MS))) return false;
    QuizMessage m = makeMessage(Command::BUTTON_PRESSED);
    if (!sender_.send(m)) return false;
    ++buttonAttempts_;
    buttonSentAt_ = now;
    return true;
}
bool SlaveController::onMessage(const QuizMessage& m, uint32_t now) {
    if (!validMessage(m) || m.senderID != MASTER_ID || m.targetID != id_ ||
        m.sessionID < minimumSession_) return false;
    if (m.command == Command::PING) {
        // PING nao autoriza rodada; boot diferente so solicita nova sincronizacao.
        if (m.sessionID > minimumSession_) {
            minimumSession_ = m.sessionID;
            hasMasterSequence_ = false;
            requestSync();
        }
        if (hasMasterSequence_ && !newerSequence(m.sequence, masterSequence_)) return false;
        masterSequence_ = m.sequence; hasMasterSequence_ = true;
        lastMaster_ = now;
        statusPending_ = true;
        if (!hasRound_ || !sameRound(m, round_)) requestSync();
        return true;
    }
    if (m.deviceBootID != bootID_ || compareRound(m, round_) < 0) return false;
    const bool newSession = m.sessionID > minimumSession_;
    if (!newSession && hasMasterSequence_ && !newerSequence(m.sequence, masterSequence_)) {
        // Repeticao exata do ultimo comando critico: reenvia ACK, sem mudar estado.
        if (m.sequence == masterSequence_ && ack_.ackSequence == m.sequence &&
            ack_.ackCommand == m.command && sameRound(m, round_)) ackPending_ = true;
        return false;
    }
    const bool newRound = !hasRound_ || compareRound(m, round_) > 0;
    if (m.command == Command::WINNER) {
        // WINNER jamais sincroniza por conta propria uma rodada desconhecida.
        if (newRound || !synchronized_ || m.winnerID != id_) return false;
        state_ = SlaveState::WINNER;
        roundLocked_ = true;
    } else if (m.command == Command::RESET_ROUND || m.command == Command::LOCK_ROUND) {
        if (newRound) {
            round_ = m;
            hasRound_ = true;
            pressedThisRound_ = false;
            roundLocked_ = false;
            buttonAttempts_ = 0;
            decisionTimedOut_ = false;
            ackPending_ = false;
        }
        if (m.command == Command::RESET_ROUND) {
            // Retransmissao do RESET nao rearma um botao ja enviado ou uma rodada travada.
            if (roundLocked_) return false;
            state_ = pressedThisRound_ ? SlaveState::WAITING_MASTER : SlaveState::READY;
        } else {
            roundLocked_ = true;
            if (state_ != SlaveState::WINNER || newRound) state_ = SlaveState::LOCKED;
        }
        synchronized_ = true;
        syncRequested_ = false;
        statusPending_ = false;
    } else return false;
    minimumSession_ = m.sessionID;
    masterSequence_ = m.sequence; hasMasterSequence_ = true;
    lastMaster_ = now;
    ack_ = makeMessage(Command::ACK);
    ack_.ackSequence = m.sequence; ack_.ackCommand = m.command;
    ackPending_ = true;
    return true;
}
void SlaveController::tick(uint32_t now) {
    if (SUPERVISION_ENABLED && synchronized_ && elapsed(now, lastMaster_, ONLINE_TIMEOUT_MS))
        requestSync();
    if (state_ == SlaveState::WAITING_MASTER && !roundLocked_ && !decisionTimedOut_ &&
        elapsed(now, pressedAt_, DECISION_TIMEOUT_MS)) {
        // Nunca declara vitoria nem permite outro acionamento da mesma rodada.
        buttonAttempts_ = BUTTON_MAX_ATTEMPTS;
        decisionTimedOut_ = true;
        requestSync();
    }
    if (sendButton(now)) return;
    // Nao transmite supervisao se o primeiro BUTTON ainda aguarda o radio.
    if (synchronized_ && state_ == SlaveState::WAITING_MASTER && buttonAttempts_ == 0) return;
    if (ackPending_) {
        if (sender_.send(ack_)) ackPending_ = false;
        return;
    }
    if (syncRequested_ && elapsed(now, lastStatus_, SYNC_INTERVAL_MS)) statusPending_ = true;
    if (statusPending_) {
        QuizMessage status = makeMessage(Command::STATUS);
        status.flags = syncRequested_ ? FLAG_SYNC_REQUEST : 0;
        if (sender_.send(status)) { statusPending_ = false; lastStatus_ = now; }
    }
}
}  // namespace quiz
