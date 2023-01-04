#include "tcp_connection.hh"
#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define ACK 0x10
#include <iostream>
#include <sstream>

using namespace std;

string segment_description(const TCPSegment &seg) {
    std::ostringstream o;
    o << "(";
    o << (seg.header().ack ? "A=1," : "A=0,");
    o << (seg.header().rst ? "R=1," : "R=0,");
    o << (seg.header().syn ? "S=1," : "S=0,");
    o << (seg.header().fin ? "F=1," : "F=0,");
    o << "ackno=" << seg.header().ackno << ",";
    o << "win=" << seg.header().win << ",";
    o << "seqno=" << seg.header().seqno << ",";
    o << "payload_size=" << seg.payload().size() << ",";
    o << "...)";
    return o.str();
}

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().buffer_size(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_recv; }

void TCPConnection::connect() {
    // Active Open, Send SYN. CLOSED -> SYN-SENT
    _active = true;
    if (_sender.next_seqno_absolute() != 0) {
        return;
    }
    _sender.fill_window();
    send_segments();
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    // kill the connection if RST is set
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _linger_after_streams_finish = false;
        // TODO: kill the connection
        return;
    }

    // check for syn
    if (seg.header().syn) {
        if (_active) {  // ACK sender
            // SYN-SENT -> SYN-RECEIVED
            _sender.send_segment(ACK);
        } else if (seg.header().ack) {
            // _active, receive SYN + ACK
            // transision: SYN-SENT -> ESTABLISH
            _sender.send_segment(ACK);
        } else {
            // LISTEN -> SYN-RECEIVED
            _sender.send_segment(SYN | ACK);
        }
    }

    // check for fin
    auto _state = state();
    if (seg.header().fin) {
        cout << "Received a FIN from segment " << segment_description(seg) << endl;
        cout << "Current state: " << _state.name() << endl;
        if (_state == TCPState::State::ESTABLISHED) {
            // ESTABLISHED -> CLOSE-WAIT
            _sender.send_segment(0);
            _linger_after_streams_finish = false;
        } else if (_state == TCPState::State::CLOSE_WAIT) {
            // CLOSE_WAIT, received 2nd FIN
            _sender.send_segment(0);  // send a ACK segment
        } else if (_state == TCPState::State::LAST_ACK) {
            // LAST-ACK -> CLOSING
            _sender.send_segment(0);
        } else if (_state == TCPState::State::FIN_WAIT_1) {
            // FIN-WAIT-1 -> CLOSING
            //? the device doesn't receive an ACK for its own FIN
            //? but receives a FIN from the other device
            _sender.send_segment(ACK);
        } else if (_state == TCPState::State::FIN_WAIT_2) {
            // FIN-WAIT-2 -> TIME-WAIT
            //? the device receives a FIN from the other device
            _sender.send_segment(ACK);
        } else if (_state == TCPState::State::TIME_WAIT) {
            // TIME-WAIT + FIN -> Send ACK (second ACK for second FIN)
            _sender.send_segment(ACK);
        }
    }

    if (_state == TCPState::State::CLOSING && seg.header().ackno == _sender.next_seqno()) {
        // CLOSING -> TIME-WAIT
    }

    if (_state == TCPState::State::LAST_ACK && seg.header().ackno == _sender.next_seqno()) {
        // LAST-ACK -> CLOSED
        _active = false;
    }

    _time_recv = 0;  // reset the timer

    // send to receiver to parse the segment
    if (_state != TCPState::State::TIME_WAIT && _state != TCPState::State::CLOSE_WAIT) {
        _receiver.segment_received(seg);
    }

    // update sender info about the ack and window size
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }

    // send update segment for keep-alives
    if (_receiver.ackno().has_value() && seg.length_in_sequence_space() == 0 &&
        seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
    }

    send_segments();
}

bool TCPConnection::active() const {
    if (!_linger_after_streams_finish) {
        return _active;
    }
    return !(_sender.stream_in().error() || _receiver.stream_out().error()) and
           (!_sender.stream_in().eof() || !_receiver.stream_out().eof() || _cfg.rt_timeout * 10 > _time_recv);
}

size_t TCPConnection::write(const string &data) {
    auto size_written = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segments();
    return size_written;
}

void TCPConnection::send_segments() {
    cout << "Send " << _sender.segments_out().size() << " segments" << endl;
    while (_sender.segments_out().size()) {
        auto segment = _sender.segments_out().front();
        cout << "segment: " << segment_description(segment) << endl;
        segment.header().win = _receiver.window_size();
        if (_receiver.ackno().has_value()) {
            segment.header().ackno = _receiver.ackno().value();
            segment.header().ack = true;
        }
        _segments_out.push(segment);
        _sender.segments_out().pop();
    }
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    _time_recv += ms_since_last_tick;

    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        _sender.send_segment(RST);
    }

    send_segments();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            _sender.send_segment(RST);
            send_segments();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}