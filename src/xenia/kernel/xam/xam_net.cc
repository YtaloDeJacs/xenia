/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_error.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xsocket.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

// NOTE: must be included last as it expects windows.h to already be included.
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // inet_addr
#include <winsock2.h>                    // NOLINT(build/include_order)

namespace xe {
namespace kernel {
namespace xam {

// https://github.com/G91/TitanOffLine/blob/1e692d9bb9dfac386d08045ccdadf4ae3227bb5e/xkelib/xam/xamNet.h
enum {
  XNCALLER_INVALID = 0x0,
  XNCALLER_TITLE = 0x1,
  XNCALLER_SYSAPP = 0x2,
  XNCALLER_XBDM = 0x3,
  XNCALLER_TEST = 0x4,
  NUM_XNCALLER_TYPES = 0x4,
};

// https://github.com/pmrowla/hl2sdk-csgo/blob/master/common/xbox/xboxstubs.h
typedef struct {
  // FYI: IN_ADDR should be in network-byte order.
  IN_ADDR ina;                   // IP address (zero if not static/DHCP)
  IN_ADDR inaOnline;             // Online IP address (zero if not online)
  xe::be<uint16_t> wPortOnline;  // Online port
  uint8_t abEnet[6];             // Ethernet MAC address
  uint8_t abOnline[20];          // Online identification
} XNADDR;

typedef struct {
  xe::be<int32_t> status;
  xe::be<uint32_t> cina;
  IN_ADDR aina[8];
} XNDNS;

struct Xsockaddr_t {
  xe::be<uint16_t> sa_family;
  char sa_data[14];
};

struct X_WSADATA {
  xe::be<uint16_t> version;
  xe::be<uint16_t> version_high;
  char description[256 + 1];
  char system_status[128 + 1];
  xe::be<uint16_t> max_sockets;
  xe::be<uint16_t> max_udpdg;
  xe::be<uint32_t> vendor_info_ptr;
};

struct XWSABUF {
  xe::be<uint32_t> len;
  xe::be<uint32_t> buf_ptr;
};

struct XWSAOVERLAPPED {
  xe::be<uint32_t> internal;
  xe::be<uint32_t> internal_high;
  union {
    struct {
      xe::be<uint32_t> offset;
      xe::be<uint32_t> offset_high;
    };
    xe::be<uint32_t> pointer;
  };
  xe::be<uint32_t> event_handle;
};

void LoadSockaddr(const uint8_t* ptr, sockaddr* out_addr) {
  out_addr->sa_family = xe::load_and_swap<uint16_t>(ptr + 0);
  switch (out_addr->sa_family) {
    case AF_INET: {
      auto in_addr = reinterpret_cast<sockaddr_in*>(out_addr);
      in_addr->sin_port = xe::load_and_swap<uint16_t>(ptr + 2);
      // Maybe? Depends on type.
      in_addr->sin_addr.S_un.S_addr = *(uint32_t*)(ptr + 4);
      break;
    }
    default:
      assert_unhandled_case(out_addr->sa_family);
      break;
  }
}

void StoreSockaddr(const sockaddr& addr, uint8_t* ptr) {
  switch (addr.sa_family) {
    case AF_UNSPEC:
      std::memset(ptr, 0, sizeof(addr));
      break;
    case AF_INET: {
      auto& in_addr = reinterpret_cast<const sockaddr_in&>(addr);
      xe::store_and_swap<uint16_t>(ptr + 0, in_addr.sin_family);
      xe::store_and_swap<uint16_t>(ptr + 2, in_addr.sin_port);
      // Maybe? Depends on type.
      xe::store_and_swap<uint32_t>(ptr + 4, in_addr.sin_addr.S_un.S_addr);
      break;
    }
    default:
      assert_unhandled_case(addr.sa_family);
      break;
  }
}

// https://github.com/joolswills/mameox/blob/master/MAMEoX/Sources/xbox_Network.cpp#L136
struct XNetStartupParams {
  BYTE cfgSizeOfStruct;
  BYTE cfgFlags;
  BYTE cfgSockMaxDgramSockets;
  BYTE cfgSockMaxStreamSockets;
  BYTE cfgSockDefaultRecvBufsizeInK;
  BYTE cfgSockDefaultSendBufsizeInK;
  BYTE cfgKeyRegMax;
  BYTE cfgSecRegMax;
  BYTE cfgQosDataLimitDiv4;
  BYTE cfgQosProbeTimeoutInSeconds;
  BYTE cfgQosProbeRetries;
  BYTE cfgQosSrvMaxSimultaneousResponses;
  BYTE cfgQosPairWaitTimeInSeconds;
};

XNetStartupParams xnet_startup_params = {0};

dword_result_t NetDll_XNetStartup(dword_t caller,
                                  pointer_t<XNetStartupParams> params) {
  if (params) {
    assert_true(params->cfgSizeOfStruct == sizeof(XNetStartupParams));
    std::memcpy(&xnet_startup_params, params, sizeof(XNetStartupParams));
  }

  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  /*
  if (!xam->xnet()) {
    auto xnet = new XNet(kernel_state());
    xnet->Initialize();

    xam->set_xnet(xnet);
  }
  */

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_XNetStartup,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_XNetCleanup(dword_t caller, lpvoid_t params) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  // auto xnet = xam->xnet();
  // xam->set_xnet(nullptr);

  // TODO: Shut down and delete.
  // delete xnet;

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_XNetCleanup,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_XNetGetOpt(dword_t one, dword_t option_id,
                                 lpvoid_t buffer_ptr, lpdword_t buffer_size) {
  assert_true(one == 1);
  switch (option_id) {
    case 1:
      if (*buffer_size < sizeof(XNetStartupParams)) {
        *buffer_size = sizeof(XNetStartupParams);
        return WSAEMSGSIZE;
      }
      std::memcpy(buffer_ptr, &xnet_startup_params, sizeof(XNetStartupParams));
      return 0;
    default:
      XELOGE("NetDll_XNetGetOpt: option %d unimplemented", option_id);
      return WSAEINVAL;
  }
}
DECLARE_XAM_EXPORT(NetDll_XNetGetOpt, ExportTag::kNetworking);

dword_result_t NetDll_XNetRandom(dword_t caller, lpvoid_t buffer_ptr,
                                 dword_t length) {
  // For now, constant values.
  // This makes replicating things easier.
  std::memset(buffer_ptr, 0xBB, length);

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_XNetRandom,
                   ExportTag::kNetworking | ExportTag::kStub);

dword_result_t NetDll_WSAStartup(dword_t caller, word_t version,
                                 pointer_t<X_WSADATA> data_ptr) {
  // TODO(benvanik): abstraction layer needed.
  WSADATA wsaData;
  ZeroMemory(&wsaData, sizeof(WSADATA));
  int ret = WSAStartup(version, &wsaData);

  auto data_out = kernel_state()->memory()->TranslateVirtual(data_ptr);

  if (data_ptr) {
    data_ptr->version = wsaData.wVersion;
    data_ptr->version_high = wsaData.wHighVersion;
    std::memcpy(&data_ptr->description, wsaData.szDescription, 0x100);
    std::memcpy(&data_ptr->system_status, wsaData.szSystemStatus, 0x80);
    data_ptr->max_sockets = wsaData.iMaxSockets;
    data_ptr->max_udpdg = wsaData.iMaxUdpDg;

    // Some games (PoG) want this value round-tripped - they'll compare if it
    // changes and bugcheck if it does.
    uint32_t vendor_ptr = xe::load_and_swap<uint32_t>(data_out + 0x190);
    xe::store_and_swap<uint32_t>(data_out + 0x190, vendor_ptr);
  }

  // DEBUG
  /*
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  if (!xam->xnet()) {
    auto xnet = new XNet(kernel_state());
    xnet->Initialize();

    xam->set_xnet(xnet);
  }
  */

  return ret;
}
DECLARE_XAM_EXPORT(NetDll_WSAStartup,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_WSACleanup(dword_t caller) {
  // This does nothing. Xenia needs WSA running.
  return 0;
}
DECLARE_XAM_EXPORT(NetDll_WSACleanup,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_WSAGetLastError() { return XThread::GetLastError(); }
DECLARE_XAM_EXPORT(NetDll_WSAGetLastError,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_WSARecvFrom(dword_t caller, dword_t socket,
                                  pointer_t<XWSABUF> buffers_ptr,
                                  dword_t buffer_count,
                                  lpdword_t num_bytes_recv, lpdword_t flags_ptr,
                                  pointer_t<XSOCKADDR_IN> from_addr,
                                  pointer_t<XWSAOVERLAPPED> overlapped_ptr,
                                  lpvoid_t completion_routine_ptr) {
  if (overlapped_ptr) {
    // auto evt = kernel_state()->object_table()->LookupObject<XEvent>(
    //    overlapped_ptr->event_handle);

    // if (evt) {
    //  //evt->Set(0, false);
    //}
  }

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_WSARecvFrom, ExportTag::kNetworking);

// If the socket is a VDP socket, buffer 0 is the game data length, and buffer 1
// is the unencrypted game data.
dword_result_t NetDll_WSASendTo(dword_t caller, dword_t socket_handle,
                                pointer_t<XWSABUF> buffers, dword_t num_buffers,
                                lpdword_t num_bytes_sent, dword_t flags,
                                pointer_t<XSOCKADDR_IN> to_ptr, dword_t to_len,
                                pointer_t<XWSAOVERLAPPED> overlapped,
                                lpvoid_t completion_routine) {
  assert(!overlapped);
  assert(!completion_routine);

  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  // Our sockets implementation doesn't support multiple buffers, so we need
  // to combine the buffers the game has given us!
  std::vector<uint8_t> combined_buffer_mem;
  uint32_t combined_buffer_size = 0;
  uint32_t combined_buffer_offset = 0;
  for (uint32_t i = 0; i < num_buffers; i++) {
    combined_buffer_size += buffers[i].len;
    combined_buffer_mem.resize(combined_buffer_size);
    uint8_t* combined_buffer = combined_buffer_mem.data();

    std::memcpy(combined_buffer + combined_buffer_offset,
                kernel_memory()->TranslateVirtual(buffers[i].buf_ptr),
                buffers[i].len);
    combined_buffer_offset += buffers[i].len;
  }

  N_XSOCKADDR_IN native_to(to_ptr);
  socket->SendTo(combined_buffer_mem.data(), combined_buffer_size, flags,
                 &native_to, to_len);

  // TODO: Instantly complete overlapped

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_WSASendTo,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_WSAWaitForMultipleEvents(dword_t num_events,
                                               lpdword_t events,
                                               dword_t wait_all,
                                               dword_t timeout,
                                               dword_t alertable) {
  if (num_events > 64) {
    XThread::SetLastError(87);  // ERROR_INVALID_PARAMETER
    return ~0u;
  }

  uint64_t timeout_wait = (uint64_t)timeout;

  X_STATUS result = 0;
  do {
    result = xboxkrnl::NtWaitForMultipleObjectsEx(
        num_events, events, wait_all, 1, alertable,
        timeout != -1 ? &timeout_wait : nullptr);
  } while (result == X_STATUS_ALERTED);

  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return ~0u;
  }
  return 0;
}
DECLARE_XAM_EXPORT(NetDll_WSAWaitForMultipleEvents, ExportTag::kNetworking |
                                                        ExportTag::kThreading |
                                                        ExportTag::kBlocking);

dword_result_t NetDll_WSACreateEvent() {
  XEvent* ev = new XEvent(kernel_state());
  ev->Initialize(true, false);
  return ev->handle();
}
DECLARE_XAM_EXPORT(NetDll_WSACreateEvent,
                   ExportTag::kNetworking | ExportTag::kThreading);

dword_result_t NetDll_WSACloseEvent(dword_t event_handle) {
  X_STATUS result = kernel_state()->object_table()->ReleaseHandle(event_handle);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_WSACloseEvent,
                   ExportTag::kNetworking | ExportTag::kThreading);

dword_result_t NetDll_WSAResetEvent(dword_t event_handle) {
  X_STATUS result = xboxkrnl::NtClearEvent(event_handle);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_WSAResetEvent,
                   ExportTag::kNetworking | ExportTag::kThreading);

dword_result_t NetDll_WSASetEvent(dword_t event_handle) {
  X_STATUS result = xboxkrnl::NtSetEvent(event_handle, nullptr);
  if (XFAILED(result)) {
    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return 0;
  }
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_WSASetEvent,
                   ExportTag::kNetworking | ExportTag::kThreading);

struct XnAddrStatus {
  // Address acquisition is not yet complete
  static const uint32_t XNET_GET_XNADDR_PENDING = 0x00000000;
  // XNet is uninitialized or no debugger found
  static const uint32_t XNET_GET_XNADDR_NONE = 0x00000001;
  // Host has ethernet address (no IP address)
  static const uint32_t XNET_GET_XNADDR_ETHERNET = 0x00000002;
  // Host has statically assigned IP address
  static const uint32_t XNET_GET_XNADDR_STATIC = 0x00000004;
  // Host has DHCP assigned IP address
  static const uint32_t XNET_GET_XNADDR_DHCP = 0x00000008;
  // Host has PPPoE assigned IP address
  static const uint32_t XNET_GET_XNADDR_PPPOE = 0x00000010;
  // Host has one or more gateways configured
  static const uint32_t XNET_GET_XNADDR_GATEWAY = 0x00000020;
  // Host has one or more DNS servers configured
  static const uint32_t XNET_GET_XNADDR_DNS = 0x00000040;
  // Host is currently connected to online service
  static const uint32_t XNET_GET_XNADDR_ONLINE = 0x00000080;
  // Network configuration requires troubleshooting
  static const uint32_t XNET_GET_XNADDR_TROUBLESHOOT = 0x00008000;
};

dword_result_t NetDll_XNetGetTitleXnAddr(dword_t caller,
                                         pointer_t<XNADDR> addr_ptr) {
  // Just return a loopback address atm.
  // FIXME: This needs to return the ethernet MAC address!
  addr_ptr->ina.S_un.S_addr = htonl(INADDR_LOOPBACK);
  addr_ptr->inaOnline.S_un.S_addr = 0;
  addr_ptr->wPortOnline = 0;
  std::memset(addr_ptr->abEnet, 0, 6);
  std::memset(addr_ptr->abOnline, 0, 20);

  return XnAddrStatus::XNET_GET_XNADDR_STATIC;
}
DECLARE_XAM_EXPORT(NetDll_XNetGetTitleXnAddr,
                   ExportTag::kNetworking | ExportTag::kStub);

dword_result_t NetDll_XNetGetDebugXnAddr(dword_t caller,
                                         pointer_t<XNADDR> addr_ptr) {
  addr_ptr.Zero();

  // XNET_GET_XNADDR_NONE causes caller to gracefully return.
  return XnAddrStatus::XNET_GET_XNADDR_NONE;
}
DECLARE_XAM_EXPORT(NetDll_XNetGetDebugXnAddr,
                   ExportTag::kNetworking | ExportTag::kStub);

dword_result_t NetDll_XNetXnAddrToMachineId(dword_t caller,
                                            pointer_t<XNADDR> addr_ptr,
                                            lpdword_t id_ptr) {
  // Tell the caller we're not signed in to live (non-zero ret)
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_XNetXnAddrToMachineId,
                   ExportTag::kNetworking | ExportTag::kStub);

void NetDll_XNetInAddrToString(dword_t caller, dword_t in_addr,
                               lpstring_t string_out, dword_t string_size) {
  strncpy(string_out, "666.666.666.666", string_size);
}
DECLARE_XAM_EXPORT(NetDll_XNetInAddrToString,
                   ExportTag::kNetworking | ExportTag::kStub);

// This converts a XNet address to an IN_ADDR. The IN_ADDR is used for
// subsequent socket calls (like a handle to a XNet address)
dword_result_t NetDll_XNetXnAddrToInAddr(dword_t caller,
                                         pointer_t<XNADDR> xn_addr,
                                         lpvoid_t xid, lpvoid_t in_addr) {
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_XNetXnAddrToInAddr,
                   ExportTag::kNetworking | ExportTag::kStub);

// Does the reverse of the above.
// FIXME: Arguments may not be correct.
dword_result_t NetDll_XNetInAddrToXnAddr(dword_t caller, lpvoid_t in_addr,
                                         pointer_t<XNADDR> xn_addr,
                                         lpvoid_t xid) {
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_XNetInAddrToXnAddr,
                   ExportTag::kNetworking | ExportTag::kStub);

// http://www.google.com/patents/WO2008112448A1?cl=en
// Reserves a port for use by system link
dword_result_t NetDll_XNetSetSystemLinkPort(dword_t caller, dword_t port) {
  return 1;
}
DECLARE_XAM_EXPORT(NetDll_XNetSetSystemLinkPort,
                   ExportTag::kNetworking | ExportTag::kStub);

// https://github.com/ILOVEPIE/Cxbx-Reloaded/blob/master/src/CxbxKrnl/EmuXOnline.h#L39
struct XEthernetStatus {
  static const uint32_t XNET_ETHERNET_LINK_ACTIVE = 0x01;
  static const uint32_t XNET_ETHERNET_LINK_100MBPS = 0x02;
  static const uint32_t XNET_ETHERNET_LINK_10MBPS = 0x04;
  static const uint32_t XNET_ETHERNET_LINK_FULL_DUPLEX = 0x08;
  static const uint32_t XNET_ETHERNET_LINK_HALF_DUPLEX = 0x10;
};

dword_result_t NetDll_XNetGetEthernetLinkStatus(dword_t caller) { return 0; }
DECLARE_XAM_EXPORT(NetDll_XNetGetEthernetLinkStatus,
                   ExportTag::kStub | ExportTag::kNetworking);

dword_result_t NetDll_XNetDnsLookup(lpstring_t address, dword_t evt_handle,
                                    pointer_t<XNDNS> host_out) {
  return 0;
}
DECLARE_XAM_EXPORT(NetDll_XNetDnsLookup,
                   ExportTag::kStub | ExportTag::kNetworking);

SHIM_CALL NetDll_XNetQosServiceLookup_shim(PPCContext* ppc_context,
                                           KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t zero = SHIM_GET_ARG_32(1);
  uint32_t event_handle = SHIM_GET_ARG_32(2);
  uint32_t out_ptr = SHIM_GET_ARG_32(3);

  XELOGD("NetDll_XNetQosServiceLookup(%d, %d, %.8X, %.8X)", caller, zero,
         event_handle, out_ptr);

  // Non-zero is error.
  SHIM_SET_RETURN_32(1);
}

dword_result_t NetDll_XNetQosListen(dword_t caller, lpvoid_t id, lpvoid_t data,
                                    dword_t data_size, dword_t r7,
                                    dword_t flags) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT(NetDll_XNetQosListen,
                   ExportTag::kStub | ExportTag::kNetworking);

dword_result_t NetDll_inet_addr(lpstring_t addr_ptr) {
  uint32_t addr = inet_addr(addr_ptr);
  return xe::byte_swap(addr);
}
DECLARE_XAM_EXPORT(NetDll_inet_addr,
                   ExportTag::kImplemented | ExportTag::kNetworking);

dword_result_t NetDll_socket(dword_t caller, dword_t af, dword_t type,
                             dword_t protocol) {
  XSocket* socket = new XSocket(kernel_state());
  X_STATUS result = socket->Initialize(XSocket::AddressFamily((uint32_t)af),
                                       XSocket::Type((uint32_t)type),
                                       XSocket::Protocol((uint32_t)protocol));

  if (XFAILED(result)) {
    socket->Release();

    uint32_t error = xboxkrnl::RtlNtStatusToDosError(result);
    XThread::SetLastError(error);
    return -1;
  }

  return socket->handle();
}
DECLARE_XAM_EXPORT(NetDll_socket,
                   ExportTag::kImplemented | ExportTag::kNetworking);

dword_result_t NetDll_closesocket(dword_t caller, dword_t socket_handle) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  // TODO: Absolutely delete this object. It is no longer valid after calling
  // closesocket.
  socket->Close();
  socket->ReleaseHandle();
  return 0;
}
DECLARE_XAM_EXPORT(NetDll_closesocket,
                   ExportTag::kImplemented | ExportTag::kNetworking);

dword_result_t NetDll_setsockopt(dword_t caller, dword_t socket_handle,
                                 dword_t level, dword_t optname,
                                 lpvoid_t optval_ptr, dword_t optlen) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  X_STATUS status = socket->SetOption(level, optname, optval_ptr, optlen);
  return XSUCCEEDED(status) ? 0 : -1;
}
DECLARE_XAM_EXPORT(NetDll_setsockopt,
                   ExportTag::kImplemented | ExportTag::kNetworking);

dword_result_t NetDll_ioctlsocket(dword_t caller, dword_t socket_handle,
                                  dword_t cmd, lpvoid_t arg_ptr) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  X_STATUS status = socket->IOControl(cmd, arg_ptr);
  if (XFAILED(status)) {
    XThread::SetLastError(xboxkrnl::RtlNtStatusToDosError(status));
    return -1;
  }

  // TODO
  return 0;
}
DECLARE_XAM_EXPORT(NetDll_ioctlsocket,
                   ExportTag::kImplemented | ExportTag::kNetworking);

dword_result_t NetDll_bind(dword_t caller, dword_t socket_handle,
                           pointer_t<XSOCKADDR_IN> name, dword_t namelen) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  N_XSOCKADDR_IN native_name(name);
  X_STATUS status = socket->Bind(&native_name, namelen);
  if (XFAILED(status)) {
    XThread::SetLastError(xboxkrnl::RtlNtStatusToDosError(status));
    return -1;
  }

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_bind,
                   ExportTag::kImplemented | ExportTag::kNetworking);

dword_result_t NetDll_connect(dword_t caller, dword_t socket_handle,
                              pointer_t<XSOCKADDR> name, dword_t namelen) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  N_XSOCKADDR native_name(name);
  X_STATUS status = socket->Connect(&native_name, namelen);
  if (XFAILED(status)) {
    XThread::SetLastError(xboxkrnl::RtlNtStatusToDosError(status));
    return -1;
  }

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_connect,
                   ExportTag::kImplemented | ExportTag::kNetworking);

dword_result_t NetDll_listen(dword_t caller, dword_t socket_handle,
                             int_t backlog) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  X_STATUS status = socket->Listen(backlog);
  if (XFAILED(status)) {
    XThread::SetLastError(xboxkrnl::RtlNtStatusToDosError(status));
    return -1;
  }

  return 0;
}
DECLARE_XAM_EXPORT(NetDll_listen,
                   ExportTag::kImplemented | ExportTag::kNetworking);

dword_result_t NetDll_accept(dword_t caller, dword_t socket_handle,
                             pointer_t<XSOCKADDR> addr_ptr,
                             lpdword_t addrlen_ptr) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  N_XSOCKADDR native_addr(addr_ptr);
  int native_len = *addrlen_ptr;
  auto new_socket = socket->Accept(&native_addr, &native_len);
  if (new_socket) {
    addr_ptr->address_family = native_addr.address_family;
    std::memcpy(addr_ptr->sa_data, native_addr.sa_data, *addrlen_ptr - 2);
    *addrlen_ptr = native_len;

    return new_socket->handle();
  } else {
    return -1;
  }
}
DECLARE_XAM_EXPORT(NetDll_accept,
                   ExportTag::kImplemented | ExportTag::kNetworking);

void LoadFdset(const uint8_t* src, fd_set* dest) {
  dest->fd_count = xe::load_and_swap<uint32_t>(src);
  for (int i = 0; i < 64; ++i) {
    auto socket_handle =
        static_cast<X_HANDLE>(xe::load_and_swap<uint32_t>(src + 4 + i * 4));
    if (!socket_handle) {
      break;
    }

    // Convert from Xenia -> native
    auto socket =
        kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
    auto native_handle = socket->native_handle();

    dest->fd_array[i] = native_handle;
  }
}

void StoreFdset(const fd_set& src, uint8_t* dest) {
  xe::store_and_swap<uint32_t>(dest, src.fd_count);
  for (int i = 0; i < 64; ++i) {
    SOCKET socket_handle = src.fd_array[i];

    // TODO: Native -> Xenia

    assert_true(socket_handle >> 32 == 0);
    xe::store_and_swap<uint32_t>(dest + 4 + i * 4,
                                 static_cast<uint32_t>(socket_handle));
  }
}

SHIM_CALL NetDll_select_shim(PPCContext* ppc_context,
                             KernelState* kernel_state) {
  uint32_t caller = SHIM_GET_ARG_32(0);
  uint32_t nfds = SHIM_GET_ARG_32(1);
  uint32_t readfds_ptr = SHIM_GET_ARG_32(2);
  uint32_t writefds_ptr = SHIM_GET_ARG_32(3);
  uint32_t exceptfds_ptr = SHIM_GET_ARG_32(4);
  uint32_t timeout_ptr = SHIM_GET_ARG_32(5);

  XELOGD("NetDll_select(%d, %d, %.8X, %.8X, %.8X, %.8X)", caller, nfds,
         readfds_ptr, writefds_ptr, exceptfds_ptr, timeout_ptr);

  fd_set readfds = {0};
  if (readfds_ptr) {
    LoadFdset(SHIM_MEM_ADDR(readfds_ptr), &readfds);
  }
  fd_set writefds = {0};
  if (writefds_ptr) {
    LoadFdset(SHIM_MEM_ADDR(writefds_ptr), &writefds);
  }
  fd_set exceptfds = {0};
  if (exceptfds_ptr) {
    LoadFdset(SHIM_MEM_ADDR(exceptfds_ptr), &exceptfds);
  }
  timeval* timeout_in = nullptr;
  timeval timeout;
  if (timeout_ptr) {
    timeout = {static_cast<int32_t>(SHIM_MEM_32(timeout_ptr + 0)),
               static_cast<int32_t>(SHIM_MEM_32(timeout_ptr + 4))};
    Clock::ScaleGuestDurationTimeval(
        reinterpret_cast<int32_t*>(&timeout.tv_sec),
        reinterpret_cast<int32_t*>(&timeout.tv_usec));
    timeout_in = &timeout;
  }
  int ret = select(nfds, readfds_ptr ? &readfds : nullptr,
                   writefds_ptr ? &writefds : nullptr,
                   exceptfds_ptr ? &exceptfds : nullptr, timeout_in);
  if (readfds_ptr) {
    StoreFdset(readfds, SHIM_MEM_ADDR(readfds_ptr));
  }
  if (writefds_ptr) {
    StoreFdset(writefds, SHIM_MEM_ADDR(writefds_ptr));
  }
  if (exceptfds_ptr) {
    StoreFdset(exceptfds, SHIM_MEM_ADDR(exceptfds_ptr));
  }

  SHIM_SET_RETURN_32(ret);
}

dword_result_t NetDll_recv(dword_t caller, dword_t socket_handle,
                           lpvoid_t buf_ptr, dword_t buf_len, dword_t flags) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  return socket->Recv(buf_ptr, buf_len, flags);
}
DECLARE_XAM_EXPORT(NetDll_recv,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_recvfrom(dword_t caller, dword_t socket_handle,
                               lpvoid_t buf_ptr, dword_t buf_len, dword_t flags,
                               pointer_t<XSOCKADDR_IN> from_ptr,
                               lpdword_t fromlen_ptr) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  N_XSOCKADDR_IN native_from;
  if (from_ptr) {
    native_from = *from_ptr;
  }
  uint32_t native_fromlen = fromlen_ptr ? *fromlen_ptr : 0;
  int ret = socket->RecvFrom(buf_ptr, buf_len, flags, &native_from,
                             fromlen_ptr ? &native_fromlen : 0);

  if (from_ptr) {
    from_ptr->sin_family = native_from.sin_family;
    from_ptr->sin_port = native_from.sin_port;
    from_ptr->sin_addr = native_from.sin_addr;
    memset(from_ptr->sin_zero, 0, 8);
  }
  if (fromlen_ptr) {
    *fromlen_ptr = native_fromlen;
  }

  if (ret == -1) {
    // TODO: Better way of getting the error code
    uint32_t error_code = WSAGetLastError();
    XThread::SetLastError(error_code);
  }

  return ret;
}
DECLARE_XAM_EXPORT(NetDll_recvfrom,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_send(dword_t caller, dword_t socket_handle,
                           lpvoid_t buf_ptr, dword_t buf_len, dword_t flags) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  return socket->Send(buf_ptr, buf_len, flags);
}
DECLARE_XAM_EXPORT(NetDll_send,
                   ExportTag::kNetworking | ExportTag::kImplemented);

dword_result_t NetDll_sendto(dword_t caller, dword_t socket_handle,
                             lpvoid_t buf_ptr, dword_t buf_len, dword_t flags,
                             pointer_t<XSOCKADDR_IN> to_ptr, dword_t to_len) {
  auto socket =
      kernel_state()->object_table()->LookupObject<XSocket>(socket_handle);
  if (!socket) {
    // WSAENOTSOCK
    XThread::SetLastError(0x2736);
    return -1;
  }

  N_XSOCKADDR_IN native_to(to_ptr);
  return socket->SendTo(buf_ptr, buf_len, flags, &native_to, to_len);
}
DECLARE_XAM_EXPORT(NetDll_sendto,
                   ExportTag::kNetworking | ExportTag::kImplemented);

void RegisterNetExports(xe::cpu::ExportResolver* export_resolver,
                        KernelState* kernel_state) {
  SHIM_SET_MAPPING("xam.xex", NetDll_XNetQosServiceLookup, state);
  SHIM_SET_MAPPING("xam.xex", NetDll_select, state);
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
