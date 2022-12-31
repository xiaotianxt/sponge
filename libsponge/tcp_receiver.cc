#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    bool syn = seg.header().syn;
    bool fin = seg.header().fin;

    if (syn) {
        // first sequential number received
        _isn = seg.header().seqno;
        _syn_received = true;
        _fin_received = false;
    }
    if (fin) {
        _fin_received = true;
    }

    string payload = seg.payload().copy();
    uint64_t index = unwrap(seg.header().seqno, _isn, _seq);

    if (_syn_received) {
        _reassembler.push_substring(payload, index - (!syn), fin);
    }

    _seq = index;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_syn_received)
        return {};

    uint64_t stream_idx = static_cast<uint64_t>(_reassembler.stream_out().bytes_written());
    uint64_t absolute_seq = stream_idx;
    if (_syn_received)
        absolute_seq++;
    if (_fin_received && _reassembler.unassembled_bytes() == 0)
        absolute_seq++;
    return wrap(absolute_seq, _isn);
}

size_t TCPReceiver::window_size() const {
    size_t buffer_size = _reassembler.stream_out().buffer_size();
    return _capacity - buffer_size;
}
