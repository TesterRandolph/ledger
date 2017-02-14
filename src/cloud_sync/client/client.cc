// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/cloud_sync/client/client.h"

#include <iostream>
#include <unordered_set>

#include "application/lib/app/connect.h"
#include "apps/ledger/src/cloud_provider/impl/cloud_provider_impl.h"
#include "apps/ledger/src/cloud_provider/public/types.h"
#include "apps/ledger/src/cloud_sync/impl/paths.h"
#include "apps/ledger/src/configuration/configuration.h"
#include "apps/ledger/src/configuration/configuration_encoder.h"
#include "apps/ledger/src/configuration/load_configuration.h"
#include "apps/ledger/src/firebase/encoding.h"
#include "apps/ledger/src/firebase/firebase_impl.h"
#include "apps/ledger/src/gcs/cloud_storage_impl.h"
#include "apps/ledger/src/glue/crypto/rand.h"
#include "apps/network/services/network_service.fidl.h"
#include "lib/ftl/strings/concatenate.h"
#include "lib/ftl/strings/string_view.h"
#include "lib/mtl/tasks/message_loop.h"

namespace cloud_sync {

namespace {

std::string RandomString() {
  return std::to_string(glue::RandUint64());
}

}  // namespace

ClientApp::ClientApp(ftl::CommandLine command_line)
    : command_line_(std::move(command_line)),
      context_(app::ApplicationContext::CreateFromStartupInfo()) {
  if (Initialize()) {
    Start();
  }
}

void ClientApp::PrintUsage() {
  std::cout << "Usage: cloud_sync <COMMAND>" << std::endl;
  std::cout << "Commands:" << std::endl;
  std::cout << " - `doctor` - checks up the cloud sync configuration (default)"
            << std::endl;
}

std::unique_ptr<Command> ClientApp::CommandFromArgs(
    const std::vector<std::string>& args) {
  // `doctor` is the default command.
  if (args.empty() || (args[0] == "doctor" && args.size() == 1)) {
    return std::make_unique<DoctorCommand>(
        network_service_.get(), configuration_.sync_params.firebase_id,
        cloud_provider_.get());
  }

  return nullptr;
}

bool ClientApp::Initialize() {
  std::unordered_set<std::string> valid_commands = {"doctor"};
  const std::vector<std::string>& args = command_line_.positional_args();
  if (args.size() && valid_commands.count(args[0]) == 0) {
    PrintUsage();
    return false;
  }

  if (!configuration::LoadConfiguration(&configuration_)) {
    std::cout << "Error: Ledger is misconfigured." << std::endl;
    std::cout
        << "Hint: refer to the User Guide at "
        << "https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md"
        << std::endl;
    return false;
  }

  if (!configuration_.use_sync) {
    std::cout << "Error: Cloud sync is disabled in the Ledger configuration."
              << std::endl;
    std::cout << "Hint: pass --firebase_id to `configure_ledger`" << std::endl;
    return false;
  }

  std::cout << "Cloud Sync Settings:" << std::endl;
  std::cout << " - firebase id: " << configuration_.sync_params.firebase_id
            << std::endl;
  if (!configuration_.sync_params.cloud_prefix.empty()) {
    std::cout << " - cloud prefix: " << configuration_.sync_params.cloud_prefix
              << std::endl;
  }
  std::cout << std::endl;

  network_service_ = std::make_unique<ledger::NetworkServiceImpl>(
      mtl::MessageLoop::GetCurrent()->task_runner(), [this] {
        return context_->ConnectToEnvironmentService<network::NetworkService>();
      });

  std::string app_firebase_path =
      GetFirebasePathForApp(configuration_.sync_params.cloud_prefix,
                            "cloud_sync_user", "cloud_sync_client");
  firebase_ = std::make_unique<firebase::FirebaseImpl>(
      network_service_.get(), configuration_.sync_params.firebase_id,
      GetFirebasePathForPage(app_firebase_path, RandomString()));
  std::string app_gcs_prefix =
      GetGcsPrefixForApp(configuration_.sync_params.cloud_prefix,
                         "cloud_sync_user", "cloud_sync_client");
  cloud_storage_ = std::make_unique<gcs::CloudStorageImpl>(
      mtl::MessageLoop::GetCurrent()->task_runner(), network_service_.get(),
      configuration_.sync_params.gcs_bucket,
      GetGcsPrefixForPage(app_gcs_prefix, RandomString()));
  cloud_provider_ = std::make_unique<cloud_provider::CloudProviderImpl>(
      firebase_.get(), cloud_storage_.get());

  command_ = CommandFromArgs(args);
  if (command_ == nullptr) {
    std::cout << "Failed to initialize the selected command." << std::endl;
    PrintUsage();
    return false;
  }
  return true;
}

void ClientApp::Start() {
  FTL_DCHECK(command_);
  command_->Start([] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
}

}  // namespace cloud_sync

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  mtl::MessageLoop loop;

  cloud_sync::ClientApp app(std::move(command_line));

  loop.Run();
  return 0;
}
