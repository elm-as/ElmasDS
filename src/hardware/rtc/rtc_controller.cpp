#include "hardware/rtc/rtc_controller.hpp"

namespace elmasds::hardware::rtc {

namespace {

constexpr u8 kBitSio = 1U << 0U;
constexpr u8 kBitSck = 1U << 1U;
constexpr u8 kBitCs = 1U << 2U;
constexpr u8 kBitSioWrite = 1U << 4U;

}  // namespace

RtcController::RtcController() noexcept {
    reset();
}

void RtcController::reset() noexcept {
    status1_ = 0x02U;
    status2_ = 0x00U;
    clock_adjust_ = 0x00U;
    free_register_ = 0x00U;
    alarm1_.fill(u8{0});
    alarm2_.fill(u8{0});
    io_reg_ = 0U;
    phase_ = Phase::Idle;
    active_reg_ = 0U;
    shift_byte_ = 0U;
    bit_count_ = 0U;
    byte_index_ = 0U;
    transfer_length_ = 0U;
    transfer_buffer_.fill(u8{0});
}

void RtcController::set_date_time(const RtcDateTime& date_time) noexcept {
    date_time_ = date_time;
}

u8 RtcController::read8(Address address) {
    if (address == kRtcMmioStart) {
        return io_reg_;
    }
    return 0U;
}

u16 RtcController::read16(Address address) {
    if ((address & ~1U) == kRtcMmioStart) {
        return static_cast<u16>(io_reg_);
    }
    return 0U;
}

u32 RtcController::read32(Address address) {
    if ((address & ~3U) == kRtcMmioStart) {
        return static_cast<u32>(io_reg_);
    }
    return 0U;
}

void RtcController::write8(Address address, u8 value) {
    if (address != kRtcMmioStart) {
        return;
    }

    const bool prev_cs = (io_reg_ & kBitCs) != 0U;
    const bool next_cs = (value & kBitCs) != 0U;

    if (!next_cs) {
        phase_ = Phase::Idle;
        io_reg_ = static_cast<u8>(value & 0x7FU);
        return;
    }

    if (!prev_cs) {
        phase_ = Phase::Command;
        shift_byte_ = 0U;
        bit_count_ = 0U;
        byte_index_ = 0U;
        transfer_length_ = 0U;
    }

    handle_clock_edge(value);
}

void RtcController::write16(Address address, u16 value) {
    if ((address & ~1U) == kRtcMmioStart) {
        write8(kRtcMmioStart, static_cast<u8>(value & 0xFFU));
    }
}

void RtcController::write32(Address address, u32 value) {
    if ((address & ~3U) == kRtcMmioStart) {
        write8(kRtcMmioStart, static_cast<u8>(value & 0xFFU));
    }
}

void RtcController::handle_clock_edge(u8 new_reg) noexcept {
    const bool prev_sck = (io_reg_ & kBitSck) != 0U;
    const bool next_sck = (new_reg & kBitSck) != 0U;
    const bool falling_edge = prev_sck && !next_sck;
    const bool rising_edge = !prev_sck && next_sck;

    u8 next_io = static_cast<u8>(new_reg & 0x7FU);

    if (phase_ == Phase::ReadData && (new_reg & kBitSioWrite) == 0U) {
        if (falling_edge && byte_index_ < transfer_length_) {
            const u8 bit = static_cast<u8>((transfer_buffer_[byte_index_] >> bit_count_) & 1U);
            next_io = static_cast<u8>((next_io & ~kBitSio) | bit);
            ++bit_count_;
            if (bit_count_ == 8U) {
                bit_count_ = 0U;
                ++byte_index_;
            }
        } else {
            // Preserve previously driven SIO bit when clock is not falling.
            next_io = static_cast<u8>((next_io & ~kBitSio) | (io_reg_ & kBitSio));
        }
        io_reg_ = next_io;
        return;
    }

    if (rising_edge && (new_reg & kBitSioWrite) != 0U) {
        const u8 bit = static_cast<u8>(new_reg & kBitSio);
        if (phase_ == Phase::Command) {
            shift_in_command_bit(bit);
        } else if (phase_ == Phase::WriteData) {
            shift_in_write_bit(bit);
        }
    }

    io_reg_ = next_io;
}

void RtcController::shift_in_command_bit(u8 bit) noexcept {
    shift_byte_ = static_cast<u8>((shift_byte_ << 1U) | bit);
    ++bit_count_;
    if (bit_count_ == 8U) {
        decode_command(shift_byte_);
    }
}

void RtcController::shift_in_write_bit(u8 bit) noexcept {
    if (byte_index_ >= transfer_length_) {
        return;
    }
    shift_byte_ = static_cast<u8>(shift_byte_ | (bit << bit_count_));
    ++bit_count_;
    if (bit_count_ < 8U) {
        return;
    }
    transfer_buffer_[byte_index_++] = shift_byte_;
    shift_byte_ = 0U;
    bit_count_ = 0U;
    if (byte_index_ == transfer_length_) {
        commit_write_buffer();
        phase_ = Phase::Idle;
    }
}

void RtcController::decode_command(u8 raw_command) noexcept {
    u8 cmd = raw_command;
    if ((cmd & 0x0FU) == 0x06U) {
        cmd = reverse_bits8(cmd);
    }

    if ((cmd & 0xF0U) != 0x60U) {
        phase_ = Phase::Idle;
        return;
    }

    active_reg_ = static_cast<u8>((cmd >> 1U) & 0x07U);
    const bool is_read = (cmd & 1U) != 0U;
    transfer_length_ = register_byte_length(active_reg_);
    byte_index_ = 0U;
    bit_count_ = 0U;
    shift_byte_ = 0U;

    if (is_read) {
        load_read_buffer(active_reg_);
        phase_ = Phase::ReadData;
    } else {
        phase_ = Phase::WriteData;
    }
}

void RtcController::load_read_buffer(u8 reg_index) noexcept {
    transfer_buffer_.fill(u8{0});
    switch (reg_index) {
        case 0U:
            transfer_buffer_[0U] = status1_;
            break;
        case 1U:
            transfer_buffer_[0U] = status2_;
            break;
        case 2U:
            transfer_buffer_[0U] = to_bcd(date_time_.year);
            transfer_buffer_[1U] = to_bcd(date_time_.month);
            transfer_buffer_[2U] = to_bcd(date_time_.day);
            transfer_buffer_[3U] = to_bcd(date_time_.weekday);
            transfer_buffer_[4U] = to_bcd(date_time_.hour);
            transfer_buffer_[5U] = to_bcd(date_time_.minute);
            transfer_buffer_[6U] = to_bcd(date_time_.second);
            break;
        case 3U:
            transfer_buffer_[0U] = to_bcd(date_time_.hour);
            transfer_buffer_[1U] = to_bcd(date_time_.minute);
            transfer_buffer_[2U] = to_bcd(date_time_.second);
            break;
        case 4U:
            transfer_buffer_[0U] = alarm1_[0U];
            transfer_buffer_[1U] = alarm1_[1U];
            transfer_buffer_[2U] = alarm1_[2U];
            break;
        case 5U:
            transfer_buffer_[0U] = alarm2_[0U];
            transfer_buffer_[1U] = alarm2_[1U];
            transfer_buffer_[2U] = alarm2_[2U];
            break;
        case 6U:
            transfer_buffer_[0U] = clock_adjust_;
            break;
        case 7U:
            transfer_buffer_[0U] = free_register_;
            break;
        default:
            break;
    }
}

void RtcController::commit_write_buffer() noexcept {
    switch (active_reg_) {
        case 0U:
            if ((transfer_buffer_[0U] & 1U) != 0U) {
                reset();
                return;
            }
            status1_ = static_cast<u8>(transfer_buffer_[0U] & 0x0EU);
            break;
        case 1U:
            status2_ = transfer_buffer_[0U];
            break;
        case 2U:
            date_time_.year = from_bcd(transfer_buffer_[0U]);
            date_time_.month = from_bcd(transfer_buffer_[1U]);
            date_time_.day = from_bcd(transfer_buffer_[2U]);
            date_time_.weekday = from_bcd(transfer_buffer_[3U]);
            date_time_.hour = from_bcd(static_cast<u8>(transfer_buffer_[4U] & 0x3FU));
            date_time_.minute = from_bcd(transfer_buffer_[5U]);
            date_time_.second = from_bcd(transfer_buffer_[6U]);
            break;
        case 3U:
            date_time_.hour = from_bcd(static_cast<u8>(transfer_buffer_[0U] & 0x3FU));
            date_time_.minute = from_bcd(transfer_buffer_[1U]);
            date_time_.second = from_bcd(transfer_buffer_[2U]);
            break;
        case 4U:
            alarm1_ = {transfer_buffer_[0U], transfer_buffer_[1U], transfer_buffer_[2U]};
            break;
        case 5U:
            alarm2_ = {transfer_buffer_[0U], transfer_buffer_[1U], transfer_buffer_[2U]};
            break;
        case 6U:
            clock_adjust_ = transfer_buffer_[0U];
            break;
        case 7U:
            free_register_ = transfer_buffer_[0U];
            break;
        default:
            break;
    }
}

u8 RtcController::reverse_bits8(u8 value) noexcept {
    u8 result = 0U;
    for (u8 bit = 0U; bit < 8U; ++bit) {
        result = static_cast<u8>((result << 1U) | ((value >> bit) & 1U));
    }
    return result;
}

std::size_t RtcController::register_byte_length(u8 reg_index) noexcept {
    switch (reg_index) {
        case 2U:
            return 7U;
        case 3U:
        case 4U:
        case 5U:
            return 3U;
        default:
            return 1U;
    }
}

}  // namespace elmasds::hardware::rtc
