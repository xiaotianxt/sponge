#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity), _capacity(capacity), _unassembled(), _unassembled_bytes(), _next(0), _eof(SIZE_MAX) {}

void StreamReassembler::__push_to_list(const string &data, const size_t index) {
    auto left = index, right = data.size() + index;

    // find the first one that may overlap with [data]
    auto iter = _unassembled.begin();
    while (iter != _unassembled.end() && iter->first + iter->second.size() < left) {
        iter++;
    }

    // 1. there's no conflict because end of string
    if (iter == _unassembled.end()) {
        _unassembled.push_back(make_pair(index, data));
        _unassembled_bytes += data.size();
        return;
    }

    // 2. there's no conflict because it fills the gap
    if (iter->first > right) {
        _unassembled.insert(iter, make_pair(index, data));
        _unassembled_bytes += data.size();
        return;
    }

    // 3. there's conflict
    //  1) loop through all the pairs that conflict with this string, merge them.
    string merged = data;
    size_t merged_idx = index;
    auto start = iter;
    while (iter != _unassembled.end() && iter->first <= right) {
        auto ileft = iter->first, iright = iter->first + iter->second.size();
        if (ileft < left) {
            merged = iter->second.substr(0, left - ileft) + merged;
            merged_idx = ileft;
        }
        if (right < iright) {
            merged = merged + iter->second.substr(iter->second.size() - iright + right);
        }
        _unassembled_bytes -= iter->second.size();
        iter++;
    }
    //  2) erase all conflict element
    _unassembled.erase(start, iter);
    //  3) insert the new merged string
    _unassembled_bytes += merged.size();
    _unassembled.insert(iter, make_pair(merged_idx, merged));
}

void StreamReassembler::__reduce(const size_t amount) {
    // get the end of list
    auto iter = _unassembled.end();
    iter--;
    auto cnt = amount;

    while (_unassembled.size()) {
        if (iter->second.size() <= cnt) {
            // remove the element
            cnt -= iter->second.size();
            _unassembled.erase(iter--);
        } else {
            // squash the element
            iter->second = iter->second.substr(0, iter->second.size() - cnt);
            break;
        }
    }
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // Useless string
    if (_next > data.size() + index) {
        return;
    }

    if (eof) {
        _eof = index + data.size();
    }

    if (index < _next) {
        __push_to_list(data.substr(_next - index), _next);
    } else {
        __push_to_list(data, index);
    }
    if (_unassembled_bytes + _output.buffer_size() > _capacity) {
        __reduce(_unassembled_bytes + _output.buffer_size() - _capacity);
        _unassembled_bytes = _capacity - _output.buffer_size();
    }

    if (_unassembled.size() == 0) {
        return;
    }

    auto elem = _unassembled.front();

    if (elem.first > _next)
        return;

    _output.write(elem.second.substr(_next - elem.first));
    _next = elem.first + elem.second.size();
    _unassembled.pop_front();
    _unassembled_bytes -= elem.second.size();

    if (_next >= _eof) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled.empty() && _output.buffer_empty(); }
