#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {
    _next_seqno = unwrap(_isn, _isn, 0);
}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

TCPSender::OutStandingSegment TCPSender::send_segment(const bool syn,
                                                      const bool fin,
                                                      std::optional<std::string> payload) {
    TCPSegment tcpSegment;
    tcpSegment.header().syn = syn;
    tcpSegment.header().fin = fin;
    tcpSegment.header().seqno = next_seqno();

    if (payload.has_value()) {
        tcpSegment.payload() = Buffer(std::move(payload.value()));
    }

    OutStandingSegment outSegment(*this, tcpSegment);
    _segments_out.push(tcpSegment);
    _segments_outstanding.push_back(outSegment);

    auto seq_length = tcpSegment.length_in_sequence_space();

    _window -= seq_length;
    _bytes_in_flight += seq_length;
    _next_seqno += seq_length;
    return outSegment;
}

void TCPSender::fill_window() {
    if (next_seqno_absolute() == 0) {
        send_segment(true, false);
    }
    while (_window && !_stream.eof() && _stream.buffer_size()) {
        TCPSegment tcpSegment;
        size_t max_payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, _window);
        size_t read_size = min(_stream.buffer_size(), max_payload_size);

        send_segment(false, false, _stream.read(read_size));
    }

    if (_stream.eof() && _window) {
        // first send a syn segment
        send_segment(false, true);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, next_seqno_absolute());

    // try to clear oustanding segments
    while (_segments_outstanding.size()) {
        auto segment = _segments_outstanding.front();
        if (segment.ack(abs_ackno)) {
            auto tcpSegment = segment.tcp_segment();
            _bytes_in_flight -= tcpSegment.length_in_sequence_space();
            cout << "segment " << segment.tcp_segment().header().seqno << " deleted" << endl;
            _segments_outstanding.pop_front();
            _retx_cnt = 0;
        } else {
            break;
        }
    }

    // recalculate the capacity of receiver
    _window = window_size - bytes_in_flight();
    if (_window) {
        fill_window();
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _timer += ms_since_last_tick;
    // check timeout segment
    for (auto &segment : _segments_outstanding) {
        if (segment.timeout()) {
            _segments_out.push(segment.tcp_segment());
            segment.update();
            _retx_cnt++;
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retx_cnt; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    _segments_out.push(segment);
}

TCPSender::OutStandingSegment::OutStandingSegment(TCPSender &parent, TCPSegment segment)
    : _parent(parent), _segment(segment) {
    _sent_time = parent._timer;
    _ackno = unwrap(parent.next_seqno() + segment.length_in_sequence_space(), parent._isn, parent._next_seqno);
    _rto = parent._initial_retransmission_timeout;
}

void TCPSender::OutStandingSegment::update() {
    _rto *= 2;
    _sent_time = _parent._timer;
}
