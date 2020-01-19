// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
#pragma once

#include <memory>

class CephContext;

namespace crimson::admin {

class ContextAdminImp;

/**
  \brief implementation of the configuration-related 'admin_socket' API of
         (Crimson) Ceph Context

  Main functionality:
  - manipulating Context-level configuration
  - process-wide commands ('abort', 'assert')
  - ...
 */
class ContextAdmin {

  std::unique_ptr<ContextAdminImp> m_imp;

 public:
  ContextAdmin(CephContext* cct, crimson::common::ConfigProxy& conf/*, crimson::admin::AdminSocketRef asok*/);
  ~ContextAdmin();

  /**
    Note: the only reason of having register_admin_commands() provided as a
    public interface (and not just be called from the ctor), is the (not just
    theoretical) race to register and unregister the same server block when
    creating a Context and immediately removing it
  */
  seastar::future<> register_admin_commands(crimson::admin::AdminSocketRef asok);
  seastar::future<> unregister_admin_commands();
};
}  // namespace crimson::admin
