#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _unassembled(), _next(0), _eof(SIZE_MAX), _size(0) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (_next > data.size() + index) {
        return;
    }
    size_t allow_bytes = _capacity - unassembled_bytes() - _output.buffer_size();
    if (allow_bytes < data.size()) {
        _unassembled.push(make_pair(index + data.size() - allow_bytes, data.substr(data.size() - allow_bytes)));
        _size += allow_bytes;
    } else {
        _unassembled.push(make_pair(index, data));
        _size += data.size();
    }
    if (eof) {
        _eof = index + data.size();
    }

    for (pair<size_t, string> top = _unassembled.top(); !_unassembled.empty() && _unassembled.top().first <= _next;
         _unassembled.pop(), top = _unassembled.top()) {
        if (top.first + top.second.size() < _next) {
            continue;
        }
        string cur = top.second.substr(_next - top.first);
        _next += _output.write(cur);
        if (_next >= _eof) {
            _output.end_input();
        }
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled.size(); }

bool StreamReassembler::empty() const { return _unassembled.empty() && _output.buffer_empty(); }
