// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/configuration/configuration_encoder.h"

#include <string>

#include "apps/ledger/src/configuration/configuration.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace configuration {
namespace {
const char kSynchronization[] = "synchronization";
const char kUseSync[] = "use_sync";
const char kFirebaseId[] = "firebase_id";
const char kDeprecatedCloudPrefix[] = "cloud_prefix";
const char kDeprecatedUserPrefix[] = "user_prefix";
}  // namespace

bool ConfigurationEncoder::Decode(const std::string& configuration_path,
                                  Configuration* configuration) {
  std::string json;
  if (!files::ReadFileToString(configuration_path.data(), &json)) {
    FTL_LOG(ERROR) << "Unable to read configuration in " << configuration_path;
    return false;
  }

  return DecodeFromString(json, configuration);
}

bool ConfigurationEncoder::Write(const std::string& configuration_path,
                                 const Configuration& configuration) {
  const std::string& data = EncodeToString(configuration);
  if (!files::WriteFile(configuration_path, data.c_str(), data.size())) {
    return false;
  }
  return true;
}

bool ConfigurationEncoder::DecodeFromString(const std::string& json,
                                            Configuration* configuration) {
  rapidjson::Document document;
  document.Parse(json.data(), json.size());

  if (document.HasParseError()) {
    return false;
  }

  if (!document.IsObject()) {
    return false;
  }

  Configuration new_configuration;

  if (!document.HasMember(kSynchronization)) {
    new_configuration.use_sync = false;
    *configuration = std::move(new_configuration);
    return true;
  }

  if (!document[kSynchronization].IsObject()) {
    FTL_LOG(ERROR) << "The " << kSynchronization
                   << " parameter must an object.";
    return false;
  }

  auto sync_config = document[kSynchronization].GetObject();

  if (!sync_config.HasMember(kFirebaseId) ||
      !sync_config[kFirebaseId].IsString()) {
    FTL_LOG(ERROR) << "The " << kFirebaseId
                   << " parameter must be specified inside " << kSynchronization
                   << ".";
    return false;
  }
  new_configuration.sync_params.firebase_id =
      sync_config[kFirebaseId].GetString();

  if (sync_config.HasMember(kDeprecatedCloudPrefix) ||
      sync_config.HasMember(kDeprecatedUserPrefix)) {
    FTL_LOG(ERROR) << "Configuration contains deprecated parameters. "
                   << "Run `ledger_tool clean` and `configure_ledger`.";
    return false;
  }

  if (!sync_config.HasMember(kUseSync) || !sync_config[kUseSync].IsBool()) {
    FTL_LOG(ERROR) << "The " << kUseSync << " parameter inside "
                   << kSynchronization
                   << " must be specified and must be a boolean.";
    return false;
  }
  new_configuration.use_sync = sync_config[kUseSync].GetBool();

  *configuration = std::move(new_configuration);
  return true;
}

std::string ConfigurationEncoder::EncodeToString(
    const Configuration& configuration) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();
  {
    writer.Key(kSynchronization);

    writer.StartObject();
    {
      {
        writer.Key(kUseSync);
        writer.Bool(configuration.use_sync);
      }
      {
        writer.Key(kFirebaseId);
        writer.String(configuration.sync_params.firebase_id.c_str(),
                      configuration.sync_params.firebase_id.size());
      }
    }
    writer.EndObject();
  }
  writer.EndObject();

  return string_buffer.GetString();
}

}  // namespace configuration
