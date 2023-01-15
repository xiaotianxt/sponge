#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <iostream>
#include <list>
#include <queue>

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! RTO for the first outstanding segment
    unsigned int _rto{};

    //! timer for the first outstanding segment
    unsigned int _sent_time{};

    //! the sender timer
    size_t _timer{0};

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};
    //! the (absolute) sequence number for the next byte to be acked
    uint64_t _next_ackno{0};

    //! the window size (size can be sent to IP), initially 1 bytes
    uint64_t _window{1};

    //! the number of times retransmite a segment
    size_t _retx_cnt{0};

    //! this byte was sent because window size is `0`
    bool _zero_window{false};

    //! the encapsulation of a TCPSegment that indicate a oustanding segment
    class OutStandingSegment {
      private:
        //! the parent object
        TCPSender &_parent;

        //! the real TCPSegment.
        TCPSegment _segment;

        //! the (absolute) ackno want to receive.
        uint64_t _ackno{};

      public:
        //! Initialize a OutStandingSegment
        OutStandingSegment(TCPSender &parent, TCPSegment segment);

        //! get the real tcp segment
        const TCPSegment &tcp_segment() const { return _segment; }
        TCPSegment &tcp_segment() { return _segment; }

        bool partial_ack(uint64_t abs_ackno) {
            return _ackno - _segment.length_in_sequence_space() <= abs_ackno && abs_ackno < _ackno;
        }

        bool fully_ack(uint64_t abs_ackno) { return abs_ackno >= _ackno; }
    };

    //! outstanding segments that the TCPSender already sent but no ack.
    std::list<OutStandingSegment> _segments_outstanding{};

  public:
    OutStandingSegment send_segment(const uint16_t flag, std::optional<std::string> payload = {});

    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    uint64_t next_ackno_absolute() const { return _next_ackno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }

    //! \brief relative ackno for the next byte to be receive
    WrappingInt32 next_ackno() const { return wrap(_next_ackno, _isn); }
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
