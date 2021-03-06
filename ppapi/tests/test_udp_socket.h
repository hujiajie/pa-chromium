// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PPAPI_TESTS_TEST_UDP_SOCKET_H_
#define PPAPI_TESTS_TEST_UDP_SOCKET_H_

#include <string>

#include "ppapi/c/pp_stdint.h"
#include "ppapi/cpp/dev/net_address_dev.h"
#include "ppapi/tests/test_case.h"

namespace pp {
class UDPSocket_Dev;
}

class TestUDPSocket: public TestCase {
 public:
  explicit TestUDPSocket(TestingInstance* instance);

  // TestCase implementation.
  virtual bool Init();
  virtual void RunTests(const std::string& filter);

 private:
  std::string GetLocalAddress(pp::NetAddress_Dev* address);
  std::string SetBroadcastOptions(pp::UDPSocket_Dev* socket);
  std::string BindUDPSocket(pp::UDPSocket_Dev* socket,
                            const pp::NetAddress_Dev& address);
  std::string LookupPortAndBindUDPSocket(pp::UDPSocket_Dev* socket,
                                         pp::NetAddress_Dev* address);
  std::string ReadSocket(pp::UDPSocket_Dev* socket,
                         pp::NetAddress_Dev* address,
                         size_t size,
                         std::string* message);
  std::string PassMessage(pp::UDPSocket_Dev* target,
                          pp::UDPSocket_Dev* source,
                          const pp::NetAddress_Dev& target_address,
                          const std::string& message,
                          pp::NetAddress_Dev* recvfrom_address);

  std::string TestReadWrite();
  std::string TestBroadcast();
  std::string TestSetOption();

  pp::NetAddress_Dev address_;
};

#endif  // PPAPI_TESTS_TEST_UDP_SOCKET_H_
