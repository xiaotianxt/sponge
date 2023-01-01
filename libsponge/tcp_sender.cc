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
    _rto = _initial_retransmission_timeout;

    cout << "Initialized with retx-timeout=" << _initial_retransmission_timeout << endl;
}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _next_ackno; }

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
    _next_seqno += seq_length;
    return outSegment;
}

void TCPSender::fill_window() {
    if (stream_in().eof() && next_seqno_absolute() == stream_in().bytes_written() + 2) {
        return;
    }

    // send syn only
    if (next_seqno_absolute() == 0) {
        send_segment(true, false);
    }

    // send fin only
    if (_stream.eof() && _window) {
        send_segment(false, true);
    }

    // fill window with data
    while (_window && !_stream.eof() && _stream.buffer_size()) {
        size_t read_size = min(TCPConfig::MAX_PAYLOAD_SIZE, min(_stream.buffer_size(), _window));
        std::string payload = _stream.read(read_size);
        cout << "read payload: \"" << payload << "\", now stream_in().bytes_written(): " << stream_in().bytes_written()
             << endl;
        send_segment(false, _stream.eof() && payload.size() < _window, payload);
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, next_seqno_absolute());

    // ignore impossible ack
    if (abs_ackno > next_seqno_absolute()) {
        return;
    }

    // should remove some segments
    if (_segments_outstanding.size() && _segments_outstanding.front().fully_ack(abs_ackno)) {
        while (_segments_outstanding.size()) {
            auto segment = _segments_outstanding.front();
            if (segment.fully_ack(abs_ackno)) {
                _segments_outstanding.pop_front();
                _zero_window = false;
                cout << "segment " << segment.tcp_segment().header().seqno << " deleted" << endl;
            } else {
                break;
            }
        }
        _retx_cnt = 0;
        _rto = _initial_retransmission_timeout;
        _sent_time = _timer;
    }

    _next_ackno = max(abs_ackno, _next_ackno);

    // recalculate the capacity of receiver
    _window = window_size - bytes_in_flight();
    if (window_size == 0) {
        _window = 1;
        _zero_window = true;
    }
    fill_window();

    cout << "After this ACK, eof: " << _stream.eof() << ", next_seqno_absolute(): " << next_seqno_absolute()
         << ", stream_in().bytes_written() + 2: " << stream_in().bytes_written() + 2
         << ", bytes in flight: " << bytes_in_flight() << endl;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _timer += ms_since_last_tick;
    // check timeout segment
    if (_sent_time + _rto <= _timer && _segments_outstanding.size()) {
        auto segment = _segments_outstanding.front();
        _segments_out.push(segment.tcp_segment());
        _sent_time = _timer;
        _rto <<= 1 - _zero_window;
        _retx_cnt++;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retx_cnt; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    _segments_out.push(segment);
}

TCPSender::OutStandingSegment::OutStandingSegment(TCPSender &parent, TCPSegment segment)
    : _parent(parent), _segment(segment) {
    _ackno = unwrap(parent.next_seqno() + segment.length_in_sequence_space(), parent._isn, parent._next_seqno);
}
