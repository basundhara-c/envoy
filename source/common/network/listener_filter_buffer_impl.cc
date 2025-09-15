#include "source/common/network/listener_filter_buffer_impl.h"

#include <string>

namespace Envoy {
namespace Network {

ListenerFilterBufferImpl::ListenerFilterBufferImpl(IoHandle& io_handle,
                                                   Event::Dispatcher& dispatcher,
                                                   ListenerFilterBufferOnCloseCb close_cb,
                                                   ListenerFilterBufferOnDataCb on_data_cb,
                                                   bool on_data_cb_disabled, uint64_t buffer_size)
    : io_handle_(io_handle), dispatcher_(dispatcher), on_close_cb_(close_cb),
      on_data_cb_(on_data_cb), on_data_cb_disabled_(on_data_cb_disabled),
      buffer_size_(on_data_cb_disabled ? 1 : buffer_size),
      buffer_(std::make_unique<uint8_t[]>(buffer_size_)), base_(buffer_.get()) {
  // If the buffer_size not greater than 0, it means that doesn't expect any data.
  ENVOY_LOG_MISC(trace, "ListenerFilterBufferImpl::ListenerFilterBufferImpl()");
  io_handle_.initializeFileEvent(
      dispatcher_, [this](uint32_t events) { return onFileEvent(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read | Event::FileReadyType::Closed);
}

const Buffer::ConstRawSlice ListenerFilterBufferImpl::rawSlice() const {
  Buffer::ConstRawSlice slice;
  slice.mem_ = base_;
  slice.len_ = data_size_;
  return slice;
}

bool ListenerFilterBufferImpl::drain(uint64_t length) {
  if (length == 0) {
    return true;
  }

  ASSERT(length <= data_size_);

  uint64_t read_size = 0;
  while (read_size < length) {
    auto result = io_handle_.recv(base_, length - read_size, 0);
    ENVOY_LOG(trace, "recv returned: {}", result.return_value_);

    if (!result.ok()) {
      // `IoErrorCode::Again` isn't processed here, since
      // the data already in the socket buffer.
      return false;
    }
    read_size += result.return_value_;
  }
  base_ += length;
  data_size_ -= length;
  return true;
}

PeekState ListenerFilterBufferImpl::peekFromSocket() {
  ENVOY_LOG(debug, "ListenerFilterBufferImpl::peekFromSocket: starting peek, buffer_size_={}", buffer_size_);
  
  // Reset buffer base in case of draining changed base.
  auto old_base = base_;
  base_ = buffer_.get();
  const auto result = io_handle_.recv(base_, buffer_size_, MSG_PEEK);
  ENVOY_LOG(trace, "recv returned: {}", result.return_value_);
  ENVOY_LOG(debug, "ListenerFilterBufferImpl::peekFromSocket: recv result - ok={}, return_value={}, error_code={}", 
            result.ok(), result.return_value_, 
            result.ok() ? "N/A" : std::to_string(static_cast<int>(result.err_->getErrorCode())));

  if (!result.ok()) {
    if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
      ENVOY_LOG(trace, "recv return try again");
      ENVOY_LOG(debug, "ListenerFilterBufferImpl::peekFromSocket: returning Again state");
      base_ = old_base;
      return PeekState::Again;
    }
    ENVOY_LOG(debug, "recv failed: {}: {}", static_cast<int>(result.err_->getErrorCode()),
              result.err_->getErrorDetails());
    ENVOY_LOG(debug, "ListenerFilterBufferImpl::peekFromSocket: returning Error state");
    return PeekState::Error;
  }
  // Remote closed
  if (result.return_value_ == 0) {
    ENVOY_LOG(debug, "recv failed: remote closed");
    ENVOY_LOG(debug, "ListenerFilterBufferImpl::peekFromSocket: returning RemoteClose state");
    return PeekState::RemoteClose;
  }
  data_size_ = result.return_value_;
  ASSERT(data_size_ <= buffer_size_);
  
  ENVOY_LOG(debug, "ListenerFilterBufferImpl::peekFromSocket: returning Done state, data_size_={}", data_size_);

  return PeekState::Done;
}

void ListenerFilterBufferImpl::resetCapacity(uint64_t size) {
  buffer_ = std::make_unique<uint8_t[]>(size);
  base_ = buffer_.get();
  buffer_size_ = size;
  data_size_ = 0;
}

void ListenerFilterBufferImpl::activateFileEvent(uint32_t events) {
  io_handle_.activateFileEvents(events);
}

absl::Status ListenerFilterBufferImpl::onFileEvent(uint32_t events) {
  ENVOY_LOG(trace, "onFileEvent: {}", events);
  ENVOY_LOG(debug, "ListenerFilterBufferImpl::onFileEvent: events={}, Closed={}, Read={}, Write={}", 
            events, 
            (events & Event::FileReadyType::Closed) ? "YES" : "NO",
            (events & Event::FileReadyType::Read) ? "YES" : "NO", 
            (events & Event::FileReadyType::Write) ? "YES" : "NO");

  if (events & Event::FileReadyType::Closed) {
    ENVOY_LOG(debug, "ListenerFilterBufferImpl::onFileEvent: CLOSED event detected, calling on_close_cb_(false)");
    on_close_cb_(false);
    return absl::OkStatus();
  }

  ASSERT(events == Event::FileReadyType::Read);

  ENVOY_LOG(debug, "ListenerFilterBufferImpl::onFileEvent: calling peekFromSocket()");
  auto state = peekFromSocket();
  ENVOY_LOG(debug, "ListenerFilterBufferImpl::onFileEvent: peekFromSocket() returned state={}", 
            state == PeekState::Done ? "Done" : 
            state == PeekState::Again ? "Again" :
            state == PeekState::Error ? "Error" : "RemoteClose");
            
  if (state == PeekState::Done && !on_data_cb_disabled_) {
    // buffer_size_ will be set to 1 if the first listener filter in
    // filter chain has maxReadBytes() of 0. Bypass onData callback of it.
    // The reason for setting it to 1 is that different platforms have
    // different libevent implementation, e.g. on macOS the connection
    // doesn't get early close notification and instead gets the close
    // after reading the FIN, which means Event::FileReadyType::Read is
    // required. But on macOS if Read is enabled with 0 buffer size, the
    // connection will be closed when there is any data sent
    ENVOY_LOG(debug, "ListenerFilterBufferImpl::onFileEvent: calling on_data_cb_");
    on_data_cb_(*this);
  } else if (state == PeekState::Error) {
    ENVOY_LOG(debug, "ListenerFilterBufferImpl::onFileEvent: ERROR state, calling on_close_cb_(true)");
    on_close_cb_(true);
  } else if (state == PeekState::RemoteClose) {
    ENVOY_LOG(debug, "ListenerFilterBufferImpl::onFileEvent: REMOTE_CLOSE state, calling on_close_cb_(false)");
    on_close_cb_(false);
  }
  // Did nothing for `Api::IoError::IoErrorCode::Again`
  return absl::OkStatus();
}

} // namespace Network
} // namespace Envoy
