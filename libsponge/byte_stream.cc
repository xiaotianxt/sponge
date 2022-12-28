#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) { _capacity = capacity; }

size_t ByteStream::write(const string &data) {
    if (!_allowin || _error) {
        _error = true;
        return 0;
    }
    size_t bytes_write = data.size() > _capacity - _buffer.size() ? _capacity - _buffer.size() : data.size();

    _buffer += data.substr(0, bytes_write);

    _bytesin += bytes_write;
    return bytes_write;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    string ret = _buffer.substr(0, len);
    return ret;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t before = _buffer.size();
    _buffer = _buffer.substr(len);
    _bytesout += before - _buffer.size();
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string ret = peek_output(len);
    pop_output(len);

    return ret;
}

void ByteStream::end_input() { _allowin = false; }

bool ByteStream::input_ended() const { return !_allowin; }

size_t ByteStream::buffer_size() const { return _buffer.size(); }

bool ByteStream::buffer_empty() const { return _buffer.empty(); }

bool ByteStream::eof() const { return _buffer.size() == 0 && !_allowin; }

size_t ByteStream::bytes_written() const { return _bytesin; }

size_t ByteStream::bytes_read() const { return _bytesout; }

size_t ByteStream::remaining_capacity() const { return _capacity - _buffer.size(); }
