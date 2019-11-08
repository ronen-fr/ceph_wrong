// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2019 Red Hat Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */
#include "common/ceph_context.h"

#include <iostream>
#include <atomic>
#include <boost/algorithm/string.hpp>
//#include "seastar/net/api.hh"
#include "seastar/core/future.hh"


#include "crimson/admin/admin_socket.h"
#include "crimson/admin/osd_admin.h"
#include "crimson/osd/osd.h"
#include "crimson/osd/exceptions.h"
#include "common/config.h"
//#include "common/errno.h"
//#include "common/Graylog.h"

#include "crimson/common/log.h"
//#include "common/valgrind.h"
//#include "include/spinlock.h"

//using ceph::HeartbeatMap;

// for CINIT_FLAGS
//#include "common/common_init.h"

#include <iostream>
//#include <pthread.h>

#ifndef WITH_SEASTAR
#error "this is a Crimson-specific implementation of some OSD APIs"
#endif

using ceph::bufferlist;
using ceph::common::local_conf;
using ceph::osd::OSD;
//using AdminSocket::hook_server_tag;

namespace {
  seastar::logger& logger() {
    return ceph::get_logger(ceph_subsys_osd);
  }
}


namespace ceph::osd {

/*!
  the hooks and states needed to handle OSD asok requests
*/
class OsdAdminImp {
  friend class OsdAdmin;
  friend class OsdAdminHookBase;

  OSD* m_osd;
  CephContext* m_cct;
  ceph::common::ConfigProxy& m_conf;

  ///
  ///  Common code for all OSD admin hooks.
  ///  Adds access to the owning OSD.
  ///
  class OsdAdminHookBase : public AdminSocketHook {
  protected:
    OsdAdminImp& m_osd_admin;

    /// the specific command implementation
    virtual seastar::future<> exec_command(Formatter* formatter, std::string_view command, const cmdmap_t& cmdmap,
	                      std::string_view format, bufferlist& out) = 0;

    explicit OsdAdminHookBase(OsdAdminImp& master) : 
      m_osd_admin{master}
    {}
  };

  ///
  ///  An Osd admin hook: OSD status
  ///
  class OsdStatusHook : public OsdAdminHookBase {
  public:
    explicit OsdStatusHook(OsdAdminImp& master) : OsdAdminHookBase(master) {};
    seastar::future<> exec_command(Formatter* f, std::string_view command, const cmdmap_t& cmdmap,
	                      std::string_view format, bufferlist& out) final {

      f->dump_stream("cluster_fsid") << m_osd_admin.osd_superblock().cluster_fsid;
      f->dump_stream("osd_fsid") << m_osd_admin.osd_superblock().osd_fsid;
      f->dump_unsigned("whoami", m_osd_admin.osd_superblock().whoami);
      // \todo f->dump_string("state", get_state_name(get_state()));
      f->dump_unsigned("oldest_map", m_osd_admin.osd_superblock().oldest_map);
      f->dump_unsigned("newest_map", m_osd_admin.osd_superblock().newest_map);
      // \todo f->dump_unsigned("num_pgs", num_pgs);
      //std::cerr << "OsdStatusHook 111" << std::endl;
      return seastar::now();
    }
  };

  ///
  ///  A test hook that throws or returns an exceptional future
  ///
  class TestThrowHook : public OsdAdminHookBase {
  public:
    explicit TestThrowHook(OsdAdminImp& master) : OsdAdminHookBase(master) {};
    seastar::future<> exec_command(Formatter* f, std::string_view command, const cmdmap_t& cmdmap,
	                      std::string_view format, bufferlist& out) final {

      if (command == "fthrow")
        return seastar::make_exception_future<>(ceph::osd::no_message_available{});
      throw(std::invalid_argument("TestThrowHook"));
    }
  };

  ///
  ///  provide the hooks with access to OSD internals 
  ///
  const OSDSuperblock& osd_superblock() {
    return m_osd->superblock;
  }

  OsdStatusHook   osd_status_hook;
  TestThrowHook   osd_test_throw_hook;

  std::atomic_flag  m_no_registrations{false}; // 'double negative' as that matches 'atomic_flag' "direction"

public:

  OsdAdminImp(OSD* osd, CephContext* cct, ceph::common::ConfigProxy& conf)
    : m_osd{osd}
    , m_cct{cct}
    , m_conf{conf}
    , osd_status_hook{*this}
    , osd_test_throw_hook{*this}
  {
    register_admin_commands();
  }

  ~OsdAdminImp() {
    unregister_admin_commands();
  }

  void register_admin_commands() {  // should probably be a future<void>

    auto admin_if = m_cct->get_admin_socket();

    (void)seastar::when_all_succeed(
              [this, admin_if](){ return admin_if->register_command(AdminSocket::hook_server_tag{this}, "status",   "status",  &osd_status_hook,      "OSD status"); }
            , [this, admin_if](){ return admin_if->register_command(AdminSocket::hook_server_tag{this}, "status2",  "status 2",&osd_status_hook,      "OSD status"); }
            , [this, admin_if](){ return admin_if->register_command(AdminSocket::hook_server_tag{this}, "throw",    "throw",   &osd_test_throw_hook,  "dev throw"); }
            , [this, admin_if](){ return admin_if->register_command(AdminSocket::hook_server_tag{this}, "fthrow",   "fthrow",  &osd_test_throw_hook,  "dev throw"); }
          );
    //admin_if->register_command(AdminSocket::hook_server_tag{this}, "status",    "status",  &osd_status_hook,      "OSD status");
    //admin_if->register_command(AdminSocket::hook_server_tag{this}, "ZZ_ZZ_ZZ_ZZ",    "ZZ_ZZ_ZZ_ZZ",  &osd_status_hook,      "OSD status");
  }

  void unregister_admin_commands() {
    if (m_no_registrations.test_and_set()) {
      //  already un-registered
      return;
    }

    auto admin_if = m_cct->get_admin_socket();
    if (admin_if) {
      // guarding against possible (?) destruction order problems
      try {
        (void)admin_if->unregister_server(AdminSocket::hook_server_tag{this}).finally([]{}).discard_result();
      } catch (...) {
        std::cerr << " failed unregistering" << std::endl;
      }
    }
  }
};

//
//  some Pimpl details:
//
OsdAdmin::OsdAdmin(OSD* osd, CephContext* cct, ceph::common::ConfigProxy& conf)
  : m_imp{ std::make_unique<ceph::osd::OsdAdminImp>(osd, cct, conf) }
{}

void OsdAdmin::unregister_admin_commands()
{
  m_imp->unregister_admin_commands();
}

OsdAdmin::~OsdAdmin() = default;

} // namespace
