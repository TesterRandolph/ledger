// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_COROUTINE_COROUTINE_IMPL_H_
#define APPS_LEDGER_SRC_COROUTINE_COROUTINE_IMPL_H_

#include <memory>
#include <vector>

#include "apps/ledger/src/coroutine/coroutine.h"
#include "lib/ftl/macros.h"

namespace context {
class Stack;
}

namespace coroutine {

class CoroutineServiceImpl : public CoroutineService {
 public:
  CoroutineServiceImpl();
  ~CoroutineServiceImpl() override;

  // CoroutineService.
  void StartCoroutine(std::function<void(CoroutineHandler*)> runnable) override;

 private:
  class CoroutineHandlerImpl;

  std::vector<std::unique_ptr<context::Stack>> available_stack_;
  std::vector<std::unique_ptr<CoroutineHandlerImpl>> handlers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CoroutineServiceImpl);
};

}  // namespace coroutine

#endif  // APPS_LEDGER_SRC_COROUTINE_COROUTINE_IMPL_H_
