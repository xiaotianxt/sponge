#include "wrapping_integers.hh"

#define MASK32 4294967296UL
#define MASK31 2147483648UL

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // first convert n to uint32_t
    auto scaled_n = static_cast<uint32_t>(n);
    // then add together, no need to get the remainder because the overflow handles.
    return WrappingInt32{(isn.raw_value() + scaled_n)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // get low 32 bit content
    auto base = checkpoint - checkpoint % MASK32;
    // get 32bit absolute seqno
    auto diff = static_cast<uint32_t>(n - isn);
    // one 64bit absolute seqno
    auto center = base + diff;

    if (center <= (UINT64_MAX - MASK31) && center < checkpoint && checkpoint - center > MASK31) {
        // right value is closer
        return center + MASK32;
    } else if (center >= MASK32 && center > checkpoint && center - checkpoint > MASK31) {
        // left value is closer
        return center - MASK32;
    }
    return center;
}
