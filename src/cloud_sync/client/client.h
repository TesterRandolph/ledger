// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_CLIENT_H_
#define APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_CLIENT_H_

#include <memory>
#include <string>
#include <vector>

#include "application/lib/app/application_context.h"
#include "apps/ledger/src/cloud_provider/public/cloud_provider.h"
#include "apps/ledger/src/cloud_sync/client/command.h"
#include "apps/ledger/src/cloud_sync/client/doctor_command.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/firebase/firebase.h"
#include "apps/ledger/src/gcs/cloud_storage.h"
#include "apps/ledger/src/network/network_service_impl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/macros.h"

namespace cloud_sync {

class ClientApp {
 public:
  ClientApp(ftl::CommandLine command_line);

 private:
  std::unique_ptr<Command> CommandFromArgs(
      const std::vector<std::string>& args);

  void PrintUsage();

  bool Initialize();

  void Start();

  ftl::CommandLine command_line_;
  configuration::Configuration configuration_;
  std::unique_ptr<modular::ApplicationContext> context_;
  std::unique_ptr<ledger::NetworkService> network_service_;
  std::unique_ptr<firebase::Firebase> firebase_;
  std::unique_ptr<gcs::CloudStorage> cloud_storage_;
  std::unique_ptr<cloud_provider::CloudProvider> cloud_provider_;
  std::unique_ptr<Command> command_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ClientApp);
};

}  // namespace cloud_sync

#endif  // APPS_LEDGER_SRC_CLOUD_SYNC_CLIENT_CLIENT_H_
