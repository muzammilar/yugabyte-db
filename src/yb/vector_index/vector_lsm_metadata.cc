// Copyright (c) YugabyteDB, Inc.
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

#include "yb/vector_index/vector_lsm_metadata.h"

#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include "yb/gutil/endian.h"

#include "yb/util/crc.h"
#include "yb/util/path_util.h"
#include "yb/util/stol_utils.h"

namespace yb::vector_index {

namespace {

using EntrySizeType = uint32_t;
using CrcType = decltype(crc::Crc32c(nullptr, 0));
const std::string kMetaFileSuffix = ".meta";

}

Result<VectorLSMMetadataLoadResult> VectorLSMMetadataLoad(
    Env* env, const std::string& dir) {
  VectorLSMMetadataLoadResult result;
  auto files = VERIFY_RESULT(env->GetChildren(dir));
  erase_if(files, [](const auto& file) {
    return !boost::ends_with(file, kMetaFileSuffix);
  });
  std::sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
    // We expect that files are generated by our code, so there is no leading zeros.
    // Because of that, file with shorter name comes before file with longer name.
    return lhs.size() < rhs.size() || (lhs.size() == rhs.size() && lhs < rhs);
  });
  LOG(INFO) << "Metadata files in " << dir << ": " << AsString(files);
  const std::string* first_file_to_keep = nullptr;
  for (const auto& file : files) {
    faststring buffer;
    auto full_path = JoinPathSegments(dir, file);
    RETURN_NOT_OK_PREPEND(ReadFileToString(env, full_path, &buffer),
                          Format("Failed to read $0: ", full_path));
    Slice slice(buffer);
    auto start = slice.data();
    for (;;) {
      if (slice.size() <= sizeof(EntrySizeType)) {
        LOG_IF(WARNING, !slice.empty())
            << "Incomplete entry size " << full_path << "(" << slice.data() - start << "): "
            << slice.size() << " bytes present";
        break;
        if (slice.empty()) {
          break;
        }

      }
      auto entry_size = Load<EntrySizeType, LittleEndian>(slice.data());
      slice.RemovePrefix(sizeof(EntrySizeType));
      if (slice.size() < entry_size + sizeof(uint32_t)) {
        LOG(WARNING) << "Incomplete entry " << full_path << "(" << slice.data() - start
                     << ", size: " << entry_size << "): " << slice.size() << " bytes present";
        break;
      }
      auto expected_crc = crc::Crc32c(slice.data(), entry_size);
      auto block_slice = slice.Prefix(entry_size);
      slice.RemovePrefix(entry_size);
      auto stored_crc = Load<CrcType, LittleEndian>(slice.data());
      slice.RemovePrefix(sizeof(uint32_t));
      if (expected_crc != stored_crc) {
        auto message = Format("CRC mismatch $0 ($1, size: $2): $3 vs $4", full_path,
                              block_slice.data() - start, entry_size, stored_crc, expected_crc);
        LOG(WARNING) << message;
        if (!slice.empty()) {
          return STATUS_FORMAT(Corruption, "$0, but there is data after this block", message);
        }
        break;
      }
      VectorLSMUpdatePB update;
      if (!update.ParseFromArray(block_slice.data(), narrow_cast<int>(block_slice.size()))) {
        return STATUS_FORMAT(
            Corruption, "Parse $0 ($1, size: $2) failed: $3",
            full_path, block_slice.data() - start, block_slice.size(),
            update.InitializationErrorString());
      }
      if (update.reset()) {
        first_file_to_keep = &file;
        result.updates.clear();
      }
      result.updates.push_back(std::move(update));
    }
  }
  if (!files.empty()) {
    auto used_file_no = VERIFY_RESULT(CheckedStoull(
        files.back().substr(0, files.back().size() - kMetaFileSuffix.size())));
    result.next_free_file_no = used_file_no + 1;
  }
  if (first_file_to_keep) {
    for (const auto& file : files) {
      if (&file == first_file_to_keep) {
        break;
      }
      auto full_path = JoinPathSegments(dir, file);
      auto delete_status = env->DeleteFile(full_path);
      if (!delete_status.ok()) {
        LOG(WARNING) << "Failed to delete " << full_path << ": " << delete_status;
        break;
      }
      LOG(INFO) << "Removed file " << full_path << " as outdated";
    }
    // Since we delete files lazily, it is not necessary to sync directory after deletion.
    // So reappearance of deleted files would not harm upcoming bootstraps.
  }

  LOG(INFO) << "Loaded: " << result.ToString();

  return result;
}

Result<std::unique_ptr<WritableFile>> VectorLSMMetadataOpenFile(
    Env* env, const std::string& dir, size_t file_index) {
  auto fname = std::to_string(file_index) + kMetaFileSuffix;
  auto full_path = JoinPathSegments(dir, fname);
  std::unique_ptr<WritableFile> result;
  RETURN_NOT_OK(env->NewWritableFile(full_path, &result));
  return result;
}

// An update is serialized in the following format:
//  - 4 bytes - size of serialized update body.
//  - serialized update body.
//  - CRC sum for the serialized update body.
Status VectorLSMMetadataAppendUpdate(WritableFile& file, const VectorLSMUpdatePB& update) {
  constexpr auto kHeaderSize = sizeof(EntrySizeType);
  std::string bytes;
  bytes.append(kHeaderSize, '\0');

  update.AppendToString(&bytes);

  Store<EntrySizeType, LittleEndian>(
      bytes.data(), narrow_cast<EntrySizeType>(bytes.size()) - kHeaderSize);

  auto crc = crc::Crc32c(bytes.c_str() + kHeaderSize, bytes.size() - kHeaderSize);
  char buffer[sizeof(CrcType)];
  Store<CrcType, LittleEndian>(buffer, crc);
  bytes.append(buffer, sizeof(CrcType));

  RETURN_NOT_OK(file.Append(Slice(bytes)));

  return file.Sync();
}

std::string VectorLSMMetadataLoadResult::ToString() const {
  return YB_STRUCT_TO_STRING(next_free_file_no, updates);
}

}  // namespace yb::vector_index
