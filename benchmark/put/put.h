// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_BENCHMARK_PUT_PUT_H_
#define APPS_LEDGER_BENCHMARK_PUT_PUT_H_

#include <memory>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/ftl/files/scoped_temp_dir.h"

namespace benchmark {

// Benchmark that measures performance of the Put() operation.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --value-size=<int> the size of a single value in bytes
class PutBenchmark {
 public:
  PutBenchmark(int entry_count, int value_size);

  void Run();

 private:
  void RunSingle(int i, int count);

  void ShutDown();

  files::ScopedTempDir tmp_dir_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  int entry_count_;
  fidl::Array<uint8_t> value_;

  app::ApplicationControllerPtr ledger_controller_;
  ledger::PagePtr page_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PutBenchmark);
};

}  // namespace benchmark

#endif  // APPS_LEDGER_BENCHMARK_PUT_PUT_H_
