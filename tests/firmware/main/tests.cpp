#include <array>
#include <vector>
#include <cstdint>
#include "unity.h"
#include "master_controller.h"
#include "slave_controller.h"
#include "button_manager.h"
#include "led_manager.h"
#include "device_config.h"
using namespace quiz;

namespace {
class FakeRadio final : public Transport {
public:
    bool available = true;
    std::vector<QuizMessage> sent;
    bool trySend(const QuizMessage& m) override {
        if (!available) return false;
        TEST_ASSERT_TRUE(validMessage(m));
        sent.push_back(m);
        return true;
    }
};
QuizMessage status(uint8_t id, uint32_t seq, uint64_t boot = 1) {
    QuizMessage m{};
    m.command = Command::STATUS; m.senderID = id; m.deviceBootID = boot;
    m.sequence = seq; m.flags = FLAG_SYNC_REQUEST;
    return m;
}
QuizMessage press(uint8_t id, uint32_t seq, uint16_t round = 1, uint64_t session = 10) {
    auto m = status(id, seq);
    m.command = Command::BUTTON_PRESSED; m.flags = 0; m.sessionID = session; m.roundID = round;
    return m;
}
QuizMessage control(Command command, uint32_t seq, uint16_t round = 1, uint8_t target = 1) {
    QuizMessage m{};
    m.command = command; m.targetID = target; m.sequence = seq; m.roundID = round;
    m.sessionID = 10; m.deviceBootID = 1;
    if (command == Command::WINNER) m.winnerID = target;
    return m;
}
void test_codec_and_invalid_frames() {
    auto m = control(Command::RESET_ROUND, 0x12345678, 65535);
    m.sessionID = 0x0102030405060708ULL;
    m.deviceBootID = 0x1112131415161718ULL;
    auto bytes = encodeMessage(m);
    TEST_ASSERT_EQUAL_UINT(42, bytes.size());
    TEST_ASSERT_EQUAL_HEX8(0x51, bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x78, bytes[8]);
    TEST_ASSERT_EQUAL_HEX8(0x08, bytes[12]);
    QuizMessage decoded{};
    TEST_ASSERT_TRUE(decodeMessage(bytes.data(), bytes.size(), decoded));
    TEST_ASSERT_TRUE(m.sessionID == decoded.sessionID);
    TEST_ASSERT_TRUE(m.deviceBootID == decoded.deviceBootID);
    TEST_ASSERT_EQUAL_HEX32(m.sequence, decoded.sequence);
    for (size_t i = 0; i < bytes.size(); ++i) {
        auto bad = bytes; bad[i] ^= 1;
        TEST_ASSERT_FALSE(decodeMessage(bad.data(), bad.size(), decoded));
    }
    TEST_ASSERT_FALSE(decodeMessage(nullptr, 42, decoded));
    TEST_ASSERT_FALSE(decodeMessage(bytes.data(), 41, decoded));
    TEST_ASSERT_FALSE(decodeMessage(bytes.data(), 43, decoded));
    m.protocolVersion = 2; bytes = encodeMessage(m);
    TEST_ASSERT_FALSE(decodeMessage(bytes.data(), bytes.size(), decoded));
    m = control(Command::WINNER, 1); m.targetID = 2;
    TEST_ASSERT_FALSE(validMessage(m));
    m = press(1, 1); m.targetID = 2;
    TEST_ASSERT_FALSE(validMessage(m));
}
void test_first_packet_wins_and_duplicates_do_not_change_result() {
    FakeRadio radio;
    MasterController master(radio, 10); master.begin();
    master.onMessage(status(1, 1), 0); master.onMessage(status(2, 1), 0);
    TEST_ASSERT_TRUE(master.onMessage(press(2, 2), 10));
    TEST_ASSERT_EQUAL(2, master.winnerID());
    TEST_ASSERT_FALSE(master.acceptButtons());
    TEST_ASSERT_EQUAL(0, radio.sent.size()); // LOCKED antes do envio secundario.
    TEST_ASSERT_FALSE(master.onMessage(press(1, 2), 10));
    TEST_ASSERT_FALSE(master.onMessage(press(2, 2), 11));
    TEST_ASSERT_FALSE(master.onMessage(press(1, 3), 12));
    master.tick(12);
    TEST_ASSERT_EQUAL(Command::WINNER, radio.sent.front().command);
    TEST_ASSERT_EQUAL(2, radio.sent.front().targetID);
    TEST_ASSERT_EQUAL(2, master.winnerID());
}
void test_each_slave_can_win_without_id_priority() {
    for (uint8_t id = 1; id <= MAX_SLAVES; ++id) {
        FakeRadio radio; MasterController master(radio, 10); master.begin();
        for (uint8_t peer = 1; peer <= MAX_SLAVES; ++peer) master.onMessage(status(peer, 1), 0);
        TEST_ASSERT_TRUE(master.onMessage(press(id, 2), 1));
        for (uint8_t peer = 1; peer <= MAX_SLAVES; ++peer) master.onMessage(press(peer, 3), 1);
        TEST_ASSERT_EQUAL(id, master.winnerID());
    }
}
void test_reset_old_round_and_old_boot_are_rejected() {
    FakeRadio radio; MasterController master(radio, 10); master.begin();
    master.onMessage(status(1, 1), 0); master.onMessage(press(1, 2), 1);
    TEST_ASSERT_TRUE(master.newRound());
    TEST_ASSERT_EQUAL(2, master.roundID()); TEST_ASSERT_EQUAL(0, master.winnerID());
    TEST_ASSERT_TRUE(master.acceptButtons());
    TEST_ASSERT_FALSE(master.onMessage(press(1, 3, 1), 2));
    TEST_ASSERT_FALSE(master.onMessage(press(1, 3, 2, 9), 2));
    master.onMessage(status(1, 1, 2), 3);
    TEST_ASSERT_FALSE(master.onMessage(press(1, 4, 2), 4));
    auto current = press(1, 2, 2); current.deviceBootID = 2;
    TEST_ASSERT_TRUE(master.onMessage(current, 5));
}
void test_manual_arm_and_round_wrap() {
    FakeRadio radio; MasterController master(radio, 10); master.begin(false);
    master.onMessage(status(1, 1), 0);
    TEST_ASSERT_EQUAL(MasterState::INITIALIZING, master.state());
    TEST_ASSERT_FALSE(master.onMessage(press(1, 2, 0), 0));
    for (uint32_t i = 0; i < 65536; ++i) TEST_ASSERT_TRUE(master.newRound());
    TEST_ASSERT_EQUAL(0, master.roundID());
    TEST_ASSERT_EQUAL(1, master.roundGeneration());
    auto old = press(1, 3, 0);
    TEST_ASSERT_FALSE(master.onMessage(old, 1));
    old.roundGeneration = 1;
    TEST_ASSERT_TRUE(master.onMessage(old, 2));
}
void test_slave_only_master_confirms_winner_and_led_resets() {
    FakeRadio radio; SlaveController slave(radio, 1, 1); LedManager led;
    TEST_ASSERT_FALSE(slave.onButton(0));
    TEST_ASSERT_FALSE(slave.onMessage(control(Command::WINNER, 1), 1));
    TEST_ASSERT_TRUE(slave.onMessage(control(Command::RESET_ROUND, 2), 2));
    TEST_ASSERT_TRUE(slave.onButton(3));
    TEST_ASSERT_EQUAL(Command::BUTTON_PRESSED, radio.sent.back().command);
    TEST_ASSERT_EQUAL(SlaveState::WAITING_MASTER, slave.state());
    auto wrong = control(Command::WINNER, 3, 1, 2);
    TEST_ASSERT_FALSE(slave.onMessage(wrong, 4));
    TEST_ASSERT_FALSE(slave.onMessage(control(Command::WINNER, 3, 2), 4));
    TEST_ASSERT_TRUE(slave.onMessage(control(Command::WINNER, 3), 5));
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), 5);
    TEST_ASSERT_TRUE(led.levels(5).button); TEST_ASSERT_TRUE(led.levels(5).external);
    TEST_ASSERT_FALSE(led.levels(5 + WINNER_BLINK_MS).button);
    TEST_ASSERT_FALSE(led.levels(5 + WINNER_BLINK_MS).external);
    TEST_ASSERT_TRUE(slave.onMessage(control(Command::RESET_ROUND, 4, 2), 6));
    TEST_ASSERT_EQUAL(SlaveState::READY, slave.state());
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), 6);
    TEST_ASSERT_TRUE(led.levels(6).button); TEST_ASSERT_TRUE(led.levels(6 + WINNER_BLINK_MS).button);
    TEST_ASSERT_FALSE(led.levels(6).external);
    TEST_ASSERT_FALSE(led.levels(6 + WINNER_BLINK_MS).external);
}
void test_repeated_reset_does_not_rearm_pressed_or_locked_round() {
    FakeRadio radio; SlaveController slave(radio, 1, 1);
    slave.onMessage(control(Command::RESET_ROUND, 1), 0);
    slave.onButton(1);
    TEST_ASSERT_TRUE(slave.onMessage(control(Command::RESET_ROUND, 2), 2));
    TEST_ASSERT_EQUAL(SlaveState::WAITING_MASTER, slave.state());
    TEST_ASSERT_FALSE(slave.onButton(3));
    slave.onMessage(control(Command::LOCK_ROUND, 3), 4);
    TEST_ASSERT_FALSE(slave.onMessage(control(Command::RESET_ROUND, 4), 5));
    TEST_ASSERT_EQUAL(SlaveState::LOCKED, slave.state());
}
void test_retries_new_sequences_and_ack_correlated() {
    FakeRadio radio; MasterController master(radio, 10); master.begin();
    master.onMessage(status(1, 1), 0); master.tick(0);
    const auto reset = radio.sent.back();
    master.tick(STATE_RETRY_MS);
    TEST_ASSERT_TRUE(newerSequence(radio.sent.back().sequence, reset.sequence));
    auto ack = press(1, 2); ack.command = Command::ACK;
    ack.ackCommand = Command::RESET_ROUND; ack.ackSequence = 500;
    TEST_ASSERT_FALSE(master.onMessage(ack, 100)); TEST_ASSERT_FALSE(master.confirmed(1));
    ack.sequence = 3; ack.ackSequence = reset.sequence;
    TEST_ASSERT_TRUE(master.onMessage(ack, 101)); TEST_ASSERT_TRUE(master.confirmed(1));
    master.newRound(); ack.sequence = 4;
    TEST_ASSERT_FALSE(master.onMessage(ack, 102)); TEST_ASSERT_FALSE(master.confirmed(1));
}
void test_winner_reboot_syncs_lock_before_winner() {
    FakeRadio radio; MasterController master(radio, 10); master.begin();
    master.onMessage(status(1, 1), 0); master.onMessage(press(1, 2), 1);
    master.onMessage(status(1, 1, 2), 2); master.tick(2);
    const auto lock = radio.sent.back();
    TEST_ASSERT_EQUAL(Command::LOCK_ROUND, lock.command);
    FakeRadio slaveRadio; SlaveController slave(slaveRadio, 1, 2);
    TEST_ASSERT_TRUE(slave.onMessage(lock, 3));
    slave.tick(3); TEST_ASSERT_EQUAL(Command::ACK, slaveRadio.sent.back().command);
    // O STATUS de registro ja usou sequence=1 neste boot.
    auto ack = slaveRadio.sent.back(); ack.sequence = 2;
    TEST_ASSERT_TRUE(master.onMessage(ack, 4)); master.tick(4);
    TEST_ASSERT_EQUAL(Command::WINNER, radio.sent.back().command);
    TEST_ASSERT_TRUE(slave.onMessage(radio.sent.back(), 5));
    TEST_ASSERT_EQUAL(SlaveState::WINNER, slave.state());
}
void test_master_reboot_and_old_control_cannot_roll_back() {
    FakeRadio radio; SlaveController slave(radio, 1, 2);
    auto old = control(Command::RESET_ROUND, 1);
    TEST_ASSERT_FALSE(slave.onMessage(old, 0)); // boot destinatario antigo
    old.deviceBootID = 2;
    slave.onMessage(old, 1);
    auto current = old; current.sessionID = 11; current.sequence = 1;
    TEST_ASSERT_TRUE(slave.onMessage(current, 2));
    old.sequence = 999;
    TEST_ASSERT_FALSE(slave.onMessage(old, 3));
    auto previousRound = current; previousRound.roundID = 0; previousRound.sequence = 100;
    TEST_ASSERT_FALSE(slave.onMessage(previousRound, 4));
}
void test_button_priority_busy_radio_and_finite_retries() {
    FakeRadio radio; SlaveController slave(radio, 1, 1);
    slave.onMessage(control(Command::RESET_ROUND, 1), 0);
    radio.available = false;
    TEST_ASSERT_TRUE(slave.onButton(1)); slave.tick(2);
    TEST_ASSERT_TRUE(radio.sent.empty());
    radio.available = true; slave.tick(3);
    TEST_ASSERT_EQUAL(Command::BUTTON_PRESSED, radio.sent.front().command);
    for (uint32_t now = 4; now < 3000; ++now) slave.tick(now);
    size_t presses = 0; uint32_t last = 0;
    for (const auto& m : radio.sent) {
        TEST_ASSERT_TRUE(newerSequence(m.sequence, last)); last = m.sequence;
        if (m.command == Command::BUTTON_PRESSED) ++presses;
    }
    TEST_ASSERT_EQUAL(BUTTON_MAX_ATTEMPTS, presses);
    TEST_ASSERT_NOT_EQUAL(SlaveState::WINNER, slave.state());
    TEST_ASSERT_FALSE(slave.onButton(4000));
}
void test_debounce_first_edge_hold_boot_and_time_wrap() {
    ButtonManager button(30); button.begin(false, 0);
    TEST_ASSERT_TRUE(button.update(true, 1));
    TEST_ASSERT_FALSE(button.update(false, 2));
    TEST_ASSERT_FALSE(button.update(true, 3));
    TEST_ASSERT_FALSE(button.update(true, 500));
    button.update(false, 501); button.update(false, 530);
    TEST_ASSERT_FALSE(button.update(true, 530));
    button.update(false, 531); button.update(false, 561);
    TEST_ASSERT_TRUE(button.update(true, 562));
    button.begin(true, 1000); TEST_ASSERT_FALSE(button.update(true, 1100));
    button.begin(false, 0xFFFFFFF0U);
    TEST_ASSERT_TRUE(button.update(true, 0xFFFFFFF1U));
    button.update(false, 0xFFFFFFF2U); button.update(false, 0x11);
    TEST_ASSERT_TRUE(button.update(true, 0x12));
}
void test_online_timeout_and_sequence_wrap() {
    FakeRadio radio; MasterController master(radio, 10); master.begin();
    master.onMessage(status(1, 0xFFFFFFFEU), 0);
    TEST_ASSERT_TRUE(master.online(1));
    auto next = status(1, 0xFFFFFFFFU); TEST_ASSERT_TRUE(master.onMessage(next, 1));
    next.sequence = 0; TEST_ASSERT_TRUE(master.onMessage(next, 2));
    next.sequence = 0xFFFFFFFFU; TEST_ASSERT_FALSE(master.onMessage(next, 3));
    master.tick(ONLINE_TIMEOUT_MS + 2); TEST_ASSERT_FALSE(master.online(1));
    TEST_ASSERT_TRUE(newerSequence(0, 0xFFFFFFFFU));
    TEST_ASSERT_FALSE(newerSequence(3, 3));
}
void test_configuration_table() {
    TEST_ASSERT_TRUE(validateDeviceTable());
    ConfigDeviceIdProvider provider;
    for (uint8_t id = 1; id <= MAX_SLAVES; ++id) {
        TEST_ASSERT_EQUAL(id, provider.slaveId(DEVICES[id].mac));
        TEST_ASSERT_TRUE(macMatches(id, DEVICES[id].mac));
    }
    const uint8_t unknown[6]{};
    TEST_ASSERT_EQUAL(INVALID_ID, provider.slaveId(unknown));
    TEST_ASSERT_FALSE(macMatches(MAX_SLAVES + 1, unknown));
}
void test_loss_reordering_and_duplicate_commands_converge() {
    FakeRadio masterRadio, firstRadio, secondRadio;
    MasterController master(masterRadio, 10);
    SlaveController first(firstRadio, 1, 1), second(secondRadio, 2, 1);
    master.begin();
    bool droppedReset = false, droppedWinner = false, droppedAck = false;
    auto step = [&](uint32_t now) {
        first.tick(now); second.tick(now);
        for (FakeRadio* radio : {&firstRadio, &secondRadio}) {
            const auto packets = radio->sent; radio->sent.clear();
            for (const auto& packet : packets) {
                if (packet.command == Command::ACK && !droppedAck) { droppedAck = true; continue; }
                master.onMessage(packet, now);
                master.onMessage(packet, now); // Duplicata de enlace.
            }
        }
        master.tick(now);
        const auto packets = masterRadio.sent; masterRadio.sent.clear();
        // Ordem invertida e mensagens criticas perdidas de forma deterministica.
        for (auto it = packets.rbegin(); it != packets.rend(); ++it) {
            if (it->command == Command::RESET_ROUND && !droppedReset) { droppedReset = true; continue; }
            if (it->command == Command::WINNER && !droppedWinner) { droppedWinner = true; continue; }
            if (it->targetID == 1) { first.onMessage(*it, now); first.onMessage(*it, now); }
            if (it->targetID == 2) { second.onMessage(*it, now); second.onMessage(*it, now); }
        }
    };
    for (uint32_t now = 0; now < 1000; now += 10) step(now);
    TEST_ASSERT_EQUAL(SlaveState::READY, first.state());
    TEST_ASSERT_EQUAL(SlaveState::READY, second.state());
    TEST_ASSERT_TRUE(first.onButton(1000)); TEST_ASSERT_TRUE(second.onButton(1000));
    for (uint32_t now = 1000; now < 2200; now += 10) step(now);
    TEST_ASSERT_EQUAL(1, master.winnerID());
    TEST_ASSERT_EQUAL(SlaveState::WINNER, first.state());
    TEST_ASSERT_EQUAL(SlaveState::LOCKED, second.state());
    TEST_ASSERT_TRUE(droppedReset && droppedWinner && droppedAck);
    TEST_ASSERT_TRUE(master.confirmed(1)); TEST_ASSERT_TRUE(master.confirmed(2));
    master.newRound();
    for (uint32_t now = 2200; now < 3200; now += 10) step(now);
    TEST_ASSERT_EQUAL(SlaveState::READY, first.state());
    TEST_ASSERT_EQUAL(SlaveState::READY, second.state());
    TEST_ASSERT_EQUAL(0, master.winnerID());
}
void test_timeout_resync_ack_does_not_rearm_same_round() {
    FakeRadio radio; SlaveController slave(radio, 1, 1);
    slave.onMessage(control(Command::RESET_ROUND, 1), 0);
    slave.onButton(1); slave.tick(DECISION_TIMEOUT_MS + 2);
    TEST_ASSERT_FALSE(slave.synchronized());
    slave.onMessage(control(Command::RESET_ROUND, 2), DECISION_TIMEOUT_MS + 3);
    radio.sent.clear(); slave.tick(DECISION_TIMEOUT_MS + 4);
    TEST_ASSERT_TRUE(slave.synchronized());
    TEST_ASSERT_EQUAL(Command::ACK, radio.sent.back().command);
    TEST_ASSERT_FALSE(slave.onButton(DECISION_TIMEOUT_MS + 5));
}
void test_loser_leds_stay_off_until_new_round() {
    FakeRadio radio; SlaveController slave(radio, 2, 1); LedManager led;
    slave.onMessage(control(Command::RESET_ROUND, 1, 1, 2), 0);
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), 0);
    TEST_ASSERT_TRUE(led.levels(0).button); TEST_ASSERT_FALSE(led.levels(0).external);
    auto lock = control(Command::LOCK_ROUND, 2, 1, 2); lock.winnerID = 1;
    slave.onMessage(lock, 10);
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), 10);
    for (uint32_t now = 10; now < 5010; now += 10) {
        TEST_ASSERT_FALSE(led.levels(now).button); TEST_ASSERT_FALSE(led.levels(now).external);
    }
    // RESET atrasado da mesma rodada nao pode fazer a luz de disponibilidade voltar.
    slave.onMessage(control(Command::RESET_ROUND, 3, 1, 2), 5010);
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), 5010);
    TEST_ASSERT_FALSE(led.levels(5010).button);
    slave.onMessage(control(Command::RESET_ROUND, 4, 2, 2), 5020);
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), 5020);
    TEST_ASSERT_TRUE(led.levels(5020).button); TEST_ASSERT_FALSE(led.levels(5020).external);
}
void test_winner_leds_share_phase_across_time_wrap() {
    LedManager led;
    const uint32_t start = 0xFFFFFFF0U;
    led.setMode(LedMode::WINNER, start);
    TEST_ASSERT_TRUE(led.levels(start).button);
    for (uint32_t delta = 0; delta < 1000; ++delta) {
        const uint32_t now = start + delta;
        led.setMode(LedMode::WINNER, now); // Repeticoes nao reiniciam a fase.
        const auto levels = led.levels(now);
        TEST_ASSERT_EQUAL(levels.button, levels.external);
        if (delta == WINNER_BLINK_MS || delta == 3 * WINNER_BLINK_MS)
            TEST_ASSERT_FALSE(levels.button);
        if (delta == 2 * WINNER_BLINK_MS) TEST_ASSERT_TRUE(levels.button);
    }
}
void test_master_leds_invert_for_round_and_reset() {
    FakeRadio radio; MasterController master(radio, 10); LedManager led;
    master.begin(false);
    led.setMode(LedManager::modeFor(master.state()), 0);
    TEST_ASSERT_TRUE(led.levels(0).button); TEST_ASSERT_FALSE(led.levels(0).external);
    master.newRound();
    led.setMode(LedManager::modeFor(master.state()), 1);
    TEST_ASSERT_FALSE(led.levels(1).button); TEST_ASSERT_TRUE(led.levels(1).external);
    master.onMessage(status(1, 1), 2); master.onMessage(press(1, 2), 3);
    led.setMode(LedManager::modeFor(master.state()), 3);
    TEST_ASSERT_TRUE(led.levels(3).button); TEST_ASSERT_FALSE(led.levels(3).external);
    TEST_ASSERT_TRUE(led.levels(2000).button); TEST_ASSERT_FALSE(led.levels(2000).external);
    master.newRound();
    led.setMode(LedManager::modeFor(master.state()), 2001);
    TEST_ASSERT_FALSE(led.levels(2001).button); TEST_ASSERT_TRUE(led.levels(2001).external);
}
void test_connection_indication_and_external_output_on_boot_error() {
    FakeRadio radio; SlaveController slave(radio, 1, 1); LedManager led;
    // Hardware em boot pode piscar somente o botao; nunca a fita externa.
    for (uint32_t now = 0; now < 1000; now += 10) TEST_ASSERT_FALSE(led.levels(now).external);
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), 0);
    TEST_ASSERT_FALSE(led.levels(0).button); TEST_ASSERT_FALSE(led.levels(0).external);
    slave.onMessage(control(Command::RESET_ROUND, 1), 0);
    slave.onButton(1);
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), 1);
    TEST_ASSERT_TRUE(led.levels(1).button); TEST_ASSERT_TRUE(led.levels(500).button);
    TEST_ASSERT_FALSE(led.levels(1).external);
    slave.tick(DECISION_TIMEOUT_MS + 2);
    led.setMode(LedManager::modeFor(slave.state(), slave.synchronized()), DECISION_TIMEOUT_MS + 2);
    TEST_ASSERT_FALSE(led.levels(DECISION_TIMEOUT_MS + 2).button);
    TEST_ASSERT_FALSE(led.levels(DECISION_TIMEOUT_MS + 2).external);
    // Uma vitoria ja confirmada continua sinalizada quando o link cai.
    led.setMode(LedManager::modeFor(SlaveState::WINNER, false), 3000);
    TEST_ASSERT_TRUE(led.levels(3000).button); TEST_ASSERT_TRUE(led.levels(3000).external);
    led.setMode(LedMode::ERROR, 4000);
    for (uint32_t now = 4000; now < 8000; now += 10) TEST_ASSERT_FALSE(led.levels(now).external);
    // O diagnostico do botao repete os dois pulsos, sem deslocar a fase entre ciclos.
    TEST_ASSERT_TRUE(led.levels(4000).button);
    TEST_ASSERT_TRUE(led.levels(4000 + ERROR_PATTERN_PERIOD_MS).button);
    TEST_ASSERT_FALSE(led.levels(4000 + ERROR_PATTERN_PERIOD_MS + BOOT_BLINK_MS).button);
    TEST_ASSERT_TRUE(led.levels(4000 + ERROR_PATTERN_PERIOD_MS + 2 * BOOT_BLINK_MS).button);
}
}
extern "C" void app_main() {
    UNITY_BEGIN();
    RUN_TEST(test_codec_and_invalid_frames);
    RUN_TEST(test_first_packet_wins_and_duplicates_do_not_change_result);
    RUN_TEST(test_each_slave_can_win_without_id_priority);
    RUN_TEST(test_reset_old_round_and_old_boot_are_rejected);
    RUN_TEST(test_manual_arm_and_round_wrap);
    RUN_TEST(test_slave_only_master_confirms_winner_and_led_resets);
    RUN_TEST(test_repeated_reset_does_not_rearm_pressed_or_locked_round);
    RUN_TEST(test_retries_new_sequences_and_ack_correlated);
    RUN_TEST(test_winner_reboot_syncs_lock_before_winner);
    RUN_TEST(test_master_reboot_and_old_control_cannot_roll_back);
    RUN_TEST(test_button_priority_busy_radio_and_finite_retries);
    RUN_TEST(test_debounce_first_edge_hold_boot_and_time_wrap);
    RUN_TEST(test_online_timeout_and_sequence_wrap);
    RUN_TEST(test_configuration_table);
    RUN_TEST(test_loss_reordering_and_duplicate_commands_converge);
    RUN_TEST(test_timeout_resync_ack_does_not_rearm_same_round);
    RUN_TEST(test_loser_leds_stay_off_until_new_round);
    RUN_TEST(test_winner_leds_share_phase_across_time_wrap);
    RUN_TEST(test_master_leds_invert_for_round_and_reset);
    RUN_TEST(test_connection_indication_and_external_output_on_boot_error);
    UNITY_END();
}
