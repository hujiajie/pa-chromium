// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ppapi/tests/test_udp_socket.h"

#include <vector>

#include "ppapi/cpp/dev/tcp_socket_dev.h"
#include "ppapi/cpp/dev/udp_socket_dev.h"
#include "ppapi/cpp/pass_ref.h"
#include "ppapi/cpp/var.h"
#include "ppapi/tests/test_utils.h"
#include "ppapi/tests/testing_instance.h"

REGISTER_TEST_CASE(UDPSocket);

namespace {

const uint16_t kPortScanFrom = 1024;
const uint16_t kPortScanTo = 4096;

pp::NetAddress_Dev ReplacePort(const pp::InstanceHandle& instance,
                               const pp::NetAddress_Dev& addr,
                               uint16_t port) {
  switch (addr.GetFamily()) {
    case PP_NETADDRESS_FAMILY_IPV4: {
      PP_NetAddress_IPv4_Dev ipv4_addr;
      if (!addr.DescribeAsIPv4Address(&ipv4_addr))
        break;
      ipv4_addr.port = ConvertToNetEndian16(port);
      return pp::NetAddress_Dev(instance, ipv4_addr);
    }
    case PP_NETADDRESS_FAMILY_IPV6: {
      PP_NetAddress_IPv6_Dev ipv6_addr;
      if (!addr.DescribeAsIPv6Address(&ipv6_addr))
        break;
      ipv6_addr.port = ConvertToNetEndian16(port);
      return pp::NetAddress_Dev(instance, ipv6_addr);
    }
    default: {
      PP_NOTREACHED();
    }
  }
  return pp::NetAddress_Dev();
}

}  // namespace

TestUDPSocket::TestUDPSocket(TestingInstance* instance) : TestCase(instance) {
}

bool TestUDPSocket::Init() {
  bool tcp_socket_is_available = pp::TCPSocket_Dev::IsAvailable();
  if (!tcp_socket_is_available)
    instance_->AppendError("PPB_TCPSocket interface not available");

  bool udp_socket_is_available = pp::UDPSocket_Dev::IsAvailable();
  if (!udp_socket_is_available)
    instance_->AppendError("PPB_UDPSocket interface not available");

  bool net_address_is_available = pp::NetAddress_Dev::IsAvailable();
  if (!net_address_is_available)
    instance_->AppendError("PPB_NetAddress interface not available");

  std::string host;
  uint16_t port = 0;
  bool init_address =
      GetLocalHostPort(instance_->pp_instance(), &host, &port) &&
      ResolveHost(instance_->pp_instance(), host, port, &address_);
  if (!init_address)
    instance_->AppendError("Can't init address");

  return tcp_socket_is_available &&
      udp_socket_is_available &&
      net_address_is_available &&
      init_address &&
      CheckTestingInterface() &&
      EnsureRunningOverHTTP();
}

void TestUDPSocket::RunTests(const std::string& filter) {
  RUN_CALLBACK_TEST(TestUDPSocket, ReadWrite, filter);
  RUN_CALLBACK_TEST(TestUDPSocket, Broadcast, filter);
  RUN_CALLBACK_TEST(TestUDPSocket, SetOption, filter);
}

std::string TestUDPSocket::GetLocalAddress(pp::NetAddress_Dev* address) {
  pp::TCPSocket_Dev socket(instance_);
  TestCompletionCallback callback(instance_->pp_instance(), callback_type());
  callback.WaitForResult(socket.Connect(address_, callback.GetCallback()));
  CHECK_CALLBACK_BEHAVIOR(callback);
  ASSERT_EQ(PP_OK, callback.result());
  *address = socket.GetLocalAddress();
  ASSERT_NE(0, address->pp_resource());
  socket.Close();
  PASS();
}

std::string TestUDPSocket::SetBroadcastOptions(pp::UDPSocket_Dev* socket) {
  TestCompletionCallback callback_1(instance_->pp_instance(), callback_type());
  callback_1.WaitForResult(socket->SetOption(
      PP_UDPSOCKET_OPTION_ADDRESS_REUSE, pp::Var(true),
      callback_1.GetCallback()));
  CHECK_CALLBACK_BEHAVIOR(callback_1);
  ASSERT_EQ(PP_OK, callback_1.result());

  TestCompletionCallback callback_2(instance_->pp_instance(), callback_type());
  callback_2.WaitForResult(socket->SetOption(
      PP_UDPSOCKET_OPTION_BROADCAST, pp::Var(true), callback_2.GetCallback()));
  CHECK_CALLBACK_BEHAVIOR(callback_2);
  ASSERT_EQ(PP_OK, callback_2.result());

  PASS();
}

std::string TestUDPSocket::BindUDPSocket(pp::UDPSocket_Dev* socket,
                                         const pp::NetAddress_Dev& address) {
  TestCompletionCallback callback(instance_->pp_instance(), callback_type());
  callback.WaitForResult(socket->Bind(address, callback.GetCallback()));
  CHECK_CALLBACK_BEHAVIOR(callback);
  ASSERT_EQ(PP_OK, callback.result());
  PASS();
}

std::string TestUDPSocket::LookupPortAndBindUDPSocket(
    pp::UDPSocket_Dev* socket,
    pp::NetAddress_Dev* address) {
  pp::NetAddress_Dev base_address;
  ASSERT_SUBTEST_SUCCESS(GetLocalAddress(&base_address));

  bool is_free_port_found = false;
  for (uint16_t port = kPortScanFrom; port < kPortScanTo; ++port) {
    pp::NetAddress_Dev new_address = ReplacePort(instance_, base_address, port);
    ASSERT_NE(0, new_address.pp_resource());
    if (BindUDPSocket(socket, new_address).empty()) {
      is_free_port_found = true;
      break;
    }
  }
  if (!is_free_port_found)
    return "Can't find available port";

  *address = socket->GetBoundAddress();
  ASSERT_NE(0, address->pp_resource());

  PASS();
}

std::string TestUDPSocket::ReadSocket(pp::UDPSocket_Dev* socket,
                                      pp::NetAddress_Dev* address,
                                      size_t size,
                                      std::string* message) {
  std::vector<char> buffer(size);
  TestCompletionCallbackWithOutput<pp::NetAddress_Dev> callback(
      instance_->pp_instance(), callback_type());
  callback.WaitForResult(
      socket->RecvFrom(&buffer[0], size, callback.GetCallback()));
  CHECK_CALLBACK_BEHAVIOR(callback);
  ASSERT_FALSE(callback.result() < 0);
  ASSERT_EQ(size, static_cast<size_t>(callback.result()));
  *address = callback.output();
  message->assign(buffer.begin(), buffer.end());
  PASS();
}

std::string TestUDPSocket::PassMessage(pp::UDPSocket_Dev* target,
                                       pp::UDPSocket_Dev* source,
                                       const pp::NetAddress_Dev& target_address,
                                       const std::string& message,
                                       pp::NetAddress_Dev* recvfrom_address) {
  TestCompletionCallback callback(instance_->pp_instance(), callback_type());
  int32_t rv = source->SendTo(message.c_str(), message.size(),
                              target_address,
                              callback.GetCallback());
  std::string str;
  ASSERT_SUBTEST_SUCCESS(ReadSocket(target, recvfrom_address, message.size(),
                                    &str));

  callback.WaitForResult(rv);
  CHECK_CALLBACK_BEHAVIOR(callback);
  ASSERT_FALSE(callback.result() < 0);
  ASSERT_EQ(message.size(), static_cast<size_t>(callback.result()));
  ASSERT_EQ(message, str);
  PASS();
}

std::string TestUDPSocket::TestReadWrite() {
  pp::UDPSocket_Dev server_socket(instance_), client_socket(instance_);
  pp::NetAddress_Dev server_address, client_address;

  ASSERT_SUBTEST_SUCCESS(LookupPortAndBindUDPSocket(&server_socket,
                                                    &server_address));
  ASSERT_SUBTEST_SUCCESS(LookupPortAndBindUDPSocket(&client_socket,
                                                    &client_address));
  const std::string message = "Simple message that will be sent via UDP";
  pp::NetAddress_Dev recvfrom_address;
  ASSERT_SUBTEST_SUCCESS(PassMessage(&server_socket, &client_socket,
                                     server_address, message,
                                     &recvfrom_address));
  ASSERT_TRUE(EqualNetAddress(recvfrom_address, client_address));

  server_socket.Close();
  client_socket.Close();

  if (server_socket.GetBoundAddress().pp_resource() != 0)
    return "PPB_UDPSocket::GetBoundAddress: expected failure";

  PASS();
}

std::string TestUDPSocket::TestBroadcast() {
  pp::UDPSocket_Dev server1(instance_), server2(instance_);

  ASSERT_SUBTEST_SUCCESS(SetBroadcastOptions(&server1));
  ASSERT_SUBTEST_SUCCESS(SetBroadcastOptions(&server2));

  PP_NetAddress_IPv4_Dev any_ipv4_address = { 0, { 0, 0, 0, 0 } };
  pp::NetAddress_Dev any_address(instance_, any_ipv4_address);
  ASSERT_SUBTEST_SUCCESS(BindUDPSocket(&server1, any_address));
  // Fill port field of |server_address|.
  pp::NetAddress_Dev server_address = server1.GetBoundAddress();
  ASSERT_NE(0, server_address.pp_resource());
  ASSERT_SUBTEST_SUCCESS(BindUDPSocket(&server2, server_address));

  PP_NetAddress_IPv4_Dev server_ipv4_address;
  ASSERT_TRUE(server_address.DescribeAsIPv4Address(&server_ipv4_address));

  PP_NetAddress_IPv4_Dev broadcast_ipv4_address = {
    server_ipv4_address.port, { 0xff, 0xff, 0xff, 0xff }
  };
  pp::NetAddress_Dev broadcast_address(instance_, broadcast_ipv4_address);

  std::string message;
  const std::string first_message = "first message";
  const std::string second_message = "second_message";

  pp::NetAddress_Dev recvfrom_address;
  ASSERT_SUBTEST_SUCCESS(PassMessage(&server1, &server2, broadcast_address,
                                     first_message, &recvfrom_address));
  // |first_message| was also received by |server2|.
  ASSERT_SUBTEST_SUCCESS(ReadSocket(&server2, &recvfrom_address,
                                    first_message.size(), &message));
  ASSERT_EQ(first_message, message);

  ASSERT_SUBTEST_SUCCESS(PassMessage(&server2, &server1, broadcast_address,
                                     second_message, &recvfrom_address));
  // |second_message| was also received by |server1|.
  ASSERT_SUBTEST_SUCCESS(ReadSocket(&server1, &recvfrom_address,
                                    second_message.size(), &message));
  ASSERT_EQ(second_message, message);

  server1.Close();
  server2.Close();
  PASS();
}

std::string TestUDPSocket::TestSetOption() {
  pp::UDPSocket_Dev socket(instance_);

  ASSERT_SUBTEST_SUCCESS(SetBroadcastOptions(&socket));

  // Try to pass incorrect option value's type.
  TestCompletionCallback callback(instance_->pp_instance(), callback_type());
  callback.WaitForResult(socket.SetOption(
      PP_UDPSOCKET_OPTION_ADDRESS_REUSE, pp::Var(1), callback.GetCallback()));
  CHECK_CALLBACK_BEHAVIOR(callback);
  ASSERT_EQ(PP_ERROR_BADARGUMENT, callback.result());

  PASS();
}
