/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include <proxygen/lib/http/session/test/TestUtils.h>

using namespace apache::thrift::async;
using namespace apache::thrift::test;
using namespace apache::thrift::transport;
using namespace folly;

namespace proxygen {

const TransportInfo mockTransportInfo = TransportInfo();
const SocketAddress localAddr{"127.0.0.1", 80};
const SocketAddress peerAddr{"127.0.0.1", 12345};

TAsyncTimeoutSet::UniquePtr makeInternalTimeoutSet(EventBase* evb) {
  return TAsyncTimeoutSet::UniquePtr(
    new TAsyncTimeoutSet(evb, TimeoutManager::InternalEnum::INTERNAL,
                         std::chrono::milliseconds(500)));
}

TAsyncTimeoutSet::UniquePtr makeTimeoutSet(EventBase* evb) {
  return TAsyncTimeoutSet::UniquePtr(
    new TAsyncTimeoutSet(evb, std::chrono::milliseconds(500)));
}

testing::NiceMock<MockTAsyncTransport>* newMockTransport(EventBase* evb) {
  auto transport = new testing::NiceMock<MockTAsyncTransport>();
  EXPECT_CALL(*transport, getEventBase())
    .WillRepeatedly(testing::Return(evb));
  return transport;
}

}
