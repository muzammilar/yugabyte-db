//
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//

#pragma once

#include "yb/common/wire_protocol.h"

#include "yb/util/status_ec.h"

namespace yb {

struct AdvisoryLocksErrorTag : IntegralErrorTag<uint64_t> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 24;

  static std::string ToMessage(Value value) {
    return Format("Min active PgSessionRequestVersion: $0", value);
  }
};

typedef StatusErrorCodeImpl<AdvisoryLocksErrorTag> AdvisoryLocksError;

} // namespace yb
