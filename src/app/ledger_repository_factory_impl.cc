// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/ledger_repository_factory_impl.h"

#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/files/path.h"

namespace ledger {

LedgerRepositoryFactoryImpl::LedgerRepositoryFactoryImpl(
    ledger::Environment* environment)
    : environment_(environment) {}

LedgerRepositoryFactoryImpl::~LedgerRepositoryFactoryImpl() {}

void LedgerRepositoryFactoryImpl::GetRepository(
    const fidl::String& repository_path,
    fidl::InterfaceRequest<LedgerRepository> repository_request,
    const GetRepositoryCallback& callback) {
  TRACE_DURATION("ledger", "repository_factory_get_repository");
  std::string sanitized_path =
      files::SimplifyPath(std::move(repository_path.get()));
  auto it = repositories_.find(sanitized_path);
  if (it == repositories_.end()) {
    auto result = repositories_.emplace(
        std::piecewise_construct, std::forward_as_tuple(sanitized_path),
        std::forward_as_tuple(sanitized_path, environment_));
    FTL_DCHECK(result.second);
    it = result.first;
  }
  it->second.BindRepository(std::move(repository_request));
  callback(Status::OK);
}

}  // namespace ledger
