// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PAPPI_TESTS_TEST_TCP_SOCKET_H_
#define PAPPI_TESTS_TEST_TCP_SOCKET_H_

#include <string>

#include "ppapi/c/pp_stdint.h"
#include "ppapi/cpp/dev/net_address_dev.h"
#include "ppapi/tests/test_case.h"

namespace pp {
class TCPSocket_Dev;
}

class TestTCPSocket: public TestCase {
 public:
  explicit TestTCPSocket(TestingInstance* instance);

  // TestCase implementation.
  virtual bool Init();
  virtual void RunTests(const std::string& filter);

 private:
  std::string TestConnect();
  std::string TestReadWrite();
  std::string TestSetOption();

  int32_t ReadFirstLineFromSocket(pp::TCPSocket_Dev* socket, std::string* s);
  int32_t WriteStringToSocket(pp::TCPSocket_Dev* socket, const std::string& s);

  pp::NetAddress_Dev addr_;
};

#endif  // PAPPI_TESTS_TEST_TCP_SOCKET_H_
