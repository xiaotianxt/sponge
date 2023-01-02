#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().buffer_size(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return {}; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_recv = 0;  // reset the timer

    // kill the connection if RST is set
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        // TODO: kill the connection
        return;
    }

    // send to receiver to parse the segment
    _receiver.segment_received(seg);

    // update sender info about the ack and window size
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }

    // send update segment for keep-alives
    if (_receiver.ackno().has_value() && seg.length_in_sequence_space() == 0 &&
        seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
    }
}

bool TCPConnection::active() const { return {}; }

size_t TCPConnection::write(const string &data) {
    auto size_written = _sender.stream_in().write(data);
    _sender.fill_window();
    return size_written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _sender.tick(ms_since_last_tick);
    _time_recv += ms_since_last_tick;
}

void TCPConnection::end_input_stream() { _sender.stream_in().end_input(); }

void TCPConnection::connect() { _sender.fill_window(); }

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}