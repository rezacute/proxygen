/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/ByteEventTracker.h>

#include <folly/io/async/DelayedDestruction.h>
#include <proxygen/lib/http/session/HTTPSession.h>
#include <proxygen/lib/http/session/HTTPSessionStats.h>
#include <string>

using apache::thrift::async::TAsyncSocket;
using apache::thrift::async::TAsyncTimeoutSet;
using apache::thrift::async::TAsyncTransport;
using std::string;
using std::vector;

namespace proxygen {

ByteEventTracker::~ByteEventTracker() {
  drainByteEvents();
}

ByteEventTracker::ByteEventTracker(ByteEventTracker&& other) noexcept {
  nextLastByteEvent_ = other.nextLastByteEvent_;
  other.nextLastByteEvent_ = nullptr;

  byteEvents_ = std::move(other.byteEvents_);
}

void ByteEventTracker::processByteEvents(uint64_t bytesWritten,
                                         bool eorTrackingEnabled) {
  bool advanceEOM = false;

  while (!byteEvents_.empty() &&
         (byteEvents_.front().byteOffset_ <= bytesWritten)) {
    ByteEvent& event = byteEvents_.front();
    int64_t latency;
    auto txn = event.getTransaction();

    switch (event.eventType_) {
    case ByteEvent::FIRST_HEADER_BYTE:
      txn->onEgressHeaderFirstByte();
      break;
    case ByteEvent::FIRST_BYTE:
      txn->onEgressBodyFirstByte();
      break;
    case ByteEvent::LAST_BYTE:
      txn->onEgressBodyLastByte();
      addAckToLastByteEvent(txn, event, eorTrackingEnabled);
      advanceEOM = true;
      break;
    case ByteEvent::PING_REPLY_SENT:
      latency = event.getLatency();
      callback_->onPingReplyLatency(latency);
      break;
    }

    VLOG(5) << " removing ByteEvent " << event;
    delete &event;
  }

  if (eorTrackingEnabled && advanceEOM) {
    nextLastByteEvent_ = nullptr;
    for (auto& event : byteEvents_) {
      if (event.eventType_ == ByteEvent::LAST_BYTE) {
        nextLastByteEvent_ = &event;
        break;
      }
    }

    VLOG(5) << "Setting nextLastByteNo to "
            << (nextLastByteEvent_ ? nextLastByteEvent_->byteOffset_ : 0);
  }
}

size_t ByteEventTracker::drainByteEvents() {
  size_t numEvents = 0;
  // everything is dead from here on, let's just drop all extra refs to txns
  while (!byteEvents_.empty()) {
    delete &byteEvents_.front();
    ++numEvents;
  }
  nextLastByteEvent_ = nullptr;
  return numEvents;
}

void ByteEventTracker::addLastByteEvent(
    HTTPTransaction* txn,
    uint64_t byteNo,
    bool eorTrackingEnabled) noexcept {
  VLOG(5) << " adding last byte event for " << byteNo;
  TransactionByteEvent* event = new TransactionByteEvent(
      byteNo, ByteEvent::LAST_BYTE, HTTPTransaction::CallbackGuard(*txn));
  byteEvents_.push_back(*event);

  if (eorTrackingEnabled && !nextLastByteEvent_) {
    VLOG(5) << " set nextLastByteNo to " << event->byteOffset_;
    nextLastByteEvent_ = event;
  }
}

void ByteEventTracker::addPingByteEvent(size_t pingSize,
                                        TimePoint timestamp,
                                        uint64_t bytesScheduled) {
  // register a byte event on ping reply sent, and adjust the byteOffset_
  // for others by one ping size
  uint64_t offset = bytesScheduled + pingSize;
  auto i = byteEvents_.rbegin();
  for (; i != byteEvents_.rend(); ++i) {
    if (i->byteOffset_ > bytesScheduled) {
      VLOG(5) << "pushing back ByteEvent from " << *i << " to "
              << ByteEvent(i->byteOffset_ + pingSize, i->eventType_);
      i->byteOffset_ += pingSize;
    } else {
      break; // the rest of the events are already scheduled
    }
  }

  ByteEvent* be = new PingByteEvent(offset, timestamp);
  if (i == byteEvents_.rend()) {
    byteEvents_.push_front(*be);
  } else if (i == byteEvents_.rbegin()) {
    byteEvents_.push_back(*be);
  } else {
    --i;
    CHECK(i->byteOffset_ > bytesScheduled);
    byteEvents_.insert(i.base(), *be);
  }
}

uint64_t ByteEventTracker::preSend(bool* cork,
                                   bool* eom,
                                   uint64_t bytesWritten) {
  if (nextLastByteEvent_) {
    uint64_t nextLastByteNo = nextLastByteEvent_->byteOffset_;
    CHECK(nextLastByteNo > bytesWritten);
    uint64_t needed = nextLastByteNo - bytesWritten;
    VLOG(5) << "needed: " << needed << "(" << nextLastByteNo << "-"
            << bytesWritten << ")";

    return needed;
  }
  return 0;
}

void ByteEventTracker::addFirstBodyByteEvent(uint64_t offset,
                                             HTTPTransaction* txn) {
  byteEvents_.push_back(
      *new TransactionByteEvent(
          offset, ByteEvent::FIRST_BYTE, HTTPTransaction::CallbackGuard(*txn)));
}

void ByteEventTracker::addFirstHeaderByteEvent(uint64_t offset,
                                               HTTPTransaction* txn) {
  // onWriteSuccess() is called after the entire header has been written.
  // It does not catch partial write case.
  byteEvents_.push_back(
      *new TransactionByteEvent(offset,
                                ByteEvent::FIRST_HEADER_BYTE,
                                HTTPTransaction::CallbackGuard(*txn)));
}

} // proxygen
