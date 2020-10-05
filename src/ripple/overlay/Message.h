//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_OVERLAY_MESSAGE_H_INCLUDED
#define RIPPLE_OVERLAY_MESSAGE_H_INCLUDED

#include <ripple/overlay/Compression.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/messages.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/buffers_iterator.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <memory>
#include <type_traits>

namespace ripple {

// VFALCO NOTE If we forward declare Message and write out shared_ptr
//             instead of using the in-class type alias, we can remove the
//             entire ripple.pb.h from the main headers.
//

// packaging of messages into length/type-prepended buffers
// ready for transmission.
//
// Message implements simple "packing" of protocol buffers Messages into
// a string prepended by a header specifying the message length.
// MessageType should be a Message class generated by the protobuf compiler.
//

class Message : public std::enable_shared_from_this<Message>
{
    using Compressed = compression::Compressed;
    using Algorithm = compression::Algorithm;

public:
    /** Constructor
     * @param message Protocol message to serialize
     * @param type Protocol message type
     * @param validator Public Key of the source validator for Validation or
     * Proposal message. Used to check if the message should be squelched.
     */
    Message(
        ::google::protobuf::Message const& message,
        int type,
        boost::optional<PublicKey> const& validator = {});

    /** Retrieve the packed message data. If compressed message is requested but
     * the message is not compressible then the uncompressed buffer is returned.
     * @param compressed Request compressed (Compress::On) or
     *     uncompressed (Compress::Off) payload buffer
     * @return Payload buffer
     */
    std::vector<uint8_t> const&
    getBuffer(Compressed tryCompressed);

    /** Get the traffic category */
    std::size_t
    getCategory() const
    {
        return category_;
    }

    /** Get the validator's key */
    boost::optional<PublicKey> const&
    getValidatorKey() const
    {
        return validatorKey_;
    }

    std::uint16_t type_ = 0;
    std::uint16_t size_ = 0;
    std::uint32_t sizeCompressed_ = 0;
    bool compressed_ = false;

private:
    std::vector<uint8_t> buffer_;
    std::vector<uint8_t> bufferCompressed_;
    std::size_t category_;
    std::once_flag once_flag_;
    boost::optional<PublicKey> validatorKey_;

    /** Set the payload header
     * @param in Pointer to the payload
     * @param payloadBytes Size of the payload excluding the header size
     * @param type Protocol message type
     * @param compression Compression algorithm used in compression,
     *   currently LZ4 only. If None then the message is uncompressed.
     * @param uncompressedBytes Size of the uncompressed message
     */
    void
    setHeader(
        std::uint8_t* in,
        std::uint32_t payloadBytes,
        int type,
        Algorithm compression,
        std::uint32_t uncompressedBytes);

    /** Try to compress the payload.
     * Can be called concurrently by multiple peers but is compressed once.
     * If the message is not compressible then the serialized buffer_ is used.
     */
    void
    compress();

    /** Get the message type from the payload header.
     * First four bytes are the compression/algorithm flag and the payload size.
     * Next two bytes are the message type
     * @param in Payload header pointer
     * @return Message type
     */
    int
    getType(std::uint8_t const* in) const;
};

}  // namespace ripple

#endif
