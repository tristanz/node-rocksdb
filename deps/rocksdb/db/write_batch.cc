//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// WriteBatch::rep_ :=
//    sequence: fixed64
//    count: fixed32
//    data: record[count]
// record :=
//    kTypeValue varstring varstring
//    kTypeMerge varstring varstring
//    kTypeDeletion varstring
// varstring :=
//    len: varint32
//    data: uint8[len]

#include "rocksdb/write_batch.h"
#include "rocksdb/options.h"
#include "rocksdb/merge_operator.h"
#include "db/dbformat.h"
#include "db/db_impl.h"
#include "db/memtable.h"
#include "db/snapshot.h"
#include "db/write_batch_internal.h"
#include "util/coding.h"
#include "util/statistics.h"
#include <stdexcept>

namespace rocksdb {

// WriteBatch header has an 8-byte sequence number followed by a 4-byte count.
static const size_t kHeader = 12;

WriteBatch::WriteBatch(size_t reserved_bytes) {
  rep_.reserve((reserved_bytes > kHeader) ? reserved_bytes : kHeader);
  Clear();
}

WriteBatch::~WriteBatch() { }

WriteBatch::Handler::~Handler() { }

void WriteBatch::Handler::Merge(const Slice& key, const Slice& value) {
  throw std::runtime_error("Handler::Merge not implemented!");
}

void WriteBatch::Handler::LogData(const Slice& blob) {
  // If the user has not specified something to do with blobs, then we ignore
  // them.
}

bool WriteBatch::Handler::Continue() {
  return true;
}

void WriteBatch::Clear() {
  rep_.clear();
  rep_.resize(kHeader);
}

int WriteBatch::Count() const {
  return WriteBatchInternal::Count(this);
}

Status WriteBatch::Iterate(Handler* handler) const {
  Slice input(rep_);
  if (input.size() < kHeader) {
    return Status::Corruption("malformed WriteBatch (too small)");
  }

  input.remove_prefix(kHeader);
  Slice key, value, blob;
  int found = 0;
  while (!input.empty() && handler->Continue()) {
    char tag = input[0];
    input.remove_prefix(1);
    switch (tag) {
      case kTypeValue:
        if (GetLengthPrefixedSlice(&input, &key) &&
            GetLengthPrefixedSlice(&input, &value)) {
          handler->Put(key, value);
          found++;
        } else {
          return Status::Corruption("bad WriteBatch Put");
        }
        break;
      case kTypeDeletion:
        if (GetLengthPrefixedSlice(&input, &key)) {
          handler->Delete(key);
          found++;
        } else {
          return Status::Corruption("bad WriteBatch Delete");
        }
        break;
      case kTypeMerge:
        if (GetLengthPrefixedSlice(&input, &key) &&
            GetLengthPrefixedSlice(&input, &value)) {
          handler->Merge(key, value);
          found++;
        } else {
          return Status::Corruption("bad WriteBatch Merge");
        }
        break;
      case kTypeLogData:
        if (GetLengthPrefixedSlice(&input, &blob)) {
          handler->LogData(blob);
        } else {
          return Status::Corruption("bad WriteBatch Blob");
        }
        break;
      default:
        return Status::Corruption("unknown WriteBatch tag");
    }
  }
 if (found != WriteBatchInternal::Count(this)) {
    return Status::Corruption("WriteBatch has wrong count");
  } else {
    return Status::OK();
  }
}

int WriteBatchInternal::Count(const WriteBatch* b) {
  return DecodeFixed32(b->rep_.data() + 8);
}

void WriteBatchInternal::SetCount(WriteBatch* b, int n) {
  EncodeFixed32(&b->rep_[8], n);
}

SequenceNumber WriteBatchInternal::Sequence(const WriteBatch* b) {
  return SequenceNumber(DecodeFixed64(b->rep_.data()));
}

void WriteBatchInternal::SetSequence(WriteBatch* b, SequenceNumber seq) {
  EncodeFixed64(&b->rep_[0], seq);
}

void WriteBatch::Put(const Slice& key, const Slice& value) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeValue));
  PutLengthPrefixedSlice(&rep_, key);
  PutLengthPrefixedSlice(&rep_, value);
}

void WriteBatch::Put(const SliceParts& key, const SliceParts& value) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeValue));
  PutLengthPrefixedSliceParts(&rep_, key);
  PutLengthPrefixedSliceParts(&rep_, value);
}

void WriteBatch::Delete(const Slice& key) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeDeletion));
  PutLengthPrefixedSlice(&rep_, key);
}

void WriteBatch::Merge(const Slice& key, const Slice& value) {
  WriteBatchInternal::SetCount(this, WriteBatchInternal::Count(this) + 1);
  rep_.push_back(static_cast<char>(kTypeMerge));
  PutLengthPrefixedSlice(&rep_, key);
  PutLengthPrefixedSlice(&rep_, value);
}

void WriteBatch::PutLogData(const Slice& blob) {
  rep_.push_back(static_cast<char>(kTypeLogData));
  PutLengthPrefixedSlice(&rep_, blob);
}

namespace {
class MemTableInserter : public WriteBatch::Handler {
 public:
  SequenceNumber sequence_;
  MemTable* mem_;
  const Options* options_;
  DBImpl* db_;
  const bool filter_deletes_;

  MemTableInserter(SequenceNumber sequence, MemTable* mem, const Options* opts,
                   DB* db, const bool filter_deletes)
    : sequence_(sequence),
      mem_(mem),
      options_(opts),
      db_(reinterpret_cast<DBImpl*>(db)),
      filter_deletes_(filter_deletes) {
    assert(mem_);
    if (filter_deletes_) {
      assert(options_);
      assert(db_);
    }
  }

  virtual void Put(const Slice& key, const Slice& value) {
    if (!options_->inplace_update_support) {
      mem_->Add(sequence_, kTypeValue, key, value);
    } else if (options_->inplace_callback == nullptr) {
      mem_->Update(sequence_, key, value);
      RecordTick(options_->statistics.get(), NUMBER_KEYS_UPDATED);
    } else {
      if (mem_->UpdateCallback(sequence_, key, value, *options_)) {
      } else {
        // key not found in memtable. Do sst get, update, add
        SnapshotImpl read_from_snapshot;
        read_from_snapshot.number_ = sequence_;
        ReadOptions ropts;
        ropts.snapshot = &read_from_snapshot;

        std::string prev_value;
        std::string merged_value;
        Status s = db_->Get(ropts, key, &prev_value);
        char* prev_buffer = const_cast<char*>(prev_value.c_str());
        uint32_t prev_size = prev_value.size();
        auto status =
          options_->inplace_callback(s.ok() ? prev_buffer: nullptr,
                                     s.ok() ? &prev_size: nullptr,
                                     value, &merged_value);
        if (status == UpdateStatus::UPDATED_INPLACE) {
          // prev_value is updated in-place with final value.
          mem_->Add(sequence_, kTypeValue, key, Slice(prev_buffer, prev_size));
          RecordTick(options_->statistics.get(), NUMBER_KEYS_WRITTEN);
        } else if (status == UpdateStatus::UPDATED) {
          // merged_value contains the final value.
          mem_->Add(sequence_, kTypeValue, key, Slice(merged_value));
          RecordTick(options_->statistics.get(), NUMBER_KEYS_WRITTEN);
        }
      }
    }
    // Since all Puts are logged in trasaction logs (if enabled), always bump
    // sequence number. Even if the update eventually fails and does not result
    // in memtable add/update.
    sequence_++;
  }

  virtual void Merge(const Slice& key, const Slice& value) {
    bool perform_merge = false;

    if (options_->max_successive_merges > 0 && db_ != nullptr) {
      LookupKey lkey(key, sequence_);

      // Count the number of successive merges at the head
      // of the key in the memtable
      size_t num_merges = mem_->CountSuccessiveMergeEntries(lkey);

      if (num_merges >= options_->max_successive_merges) {
        perform_merge = true;
      }
    }

    if (perform_merge) {
      // 1) Get the existing value
      std::string get_value;

      // Pass in the sequence number so that we also include previous merge
      // operations in the same batch.
      SnapshotImpl read_from_snapshot;
      read_from_snapshot.number_ = sequence_;
      ReadOptions read_options;
      read_options.snapshot = &read_from_snapshot;

      db_->Get(read_options, key, &get_value);
      Slice get_value_slice = Slice(get_value);

      // 2) Apply this merge
      auto merge_operator = options_->merge_operator.get();
      assert(merge_operator);

      std::deque<std::string> operands;
      operands.push_front(value.ToString());
      std::string new_value;
      if (!merge_operator->FullMerge(key,
                                     &get_value_slice,
                                     operands,
                                     &new_value,
                                     options_->info_log.get())) {
          // Failed to merge!
          RecordTick(options_->statistics.get(), NUMBER_MERGE_FAILURES);

          // Store the delta in memtable
          perform_merge = false;
      } else {
        // 3) Add value to memtable
        mem_->Add(sequence_, kTypeValue, key, new_value);
      }
    }

    if (!perform_merge) {
      // Add merge operator to memtable
      mem_->Add(sequence_, kTypeMerge, key, value);
    }

    sequence_++;
  }
  virtual void Delete(const Slice& key) {
    if (filter_deletes_) {
      SnapshotImpl read_from_snapshot;
      read_from_snapshot.number_ = sequence_;
      ReadOptions ropts;
      ropts.snapshot = &read_from_snapshot;
      std::string value;
      if (!db_->KeyMayExist(ropts, key, &value)) {
        RecordTick(options_->statistics.get(), NUMBER_FILTERED_DELETES);
        return;
      }
    }
    mem_->Add(sequence_, kTypeDeletion, key, Slice());
    sequence_++;
  }
};
}  // namespace

Status WriteBatchInternal::InsertInto(const WriteBatch* b, MemTable* mem,
                                      const Options* opts, DB* db,
                                      const bool filter_deletes) {
  MemTableInserter inserter(WriteBatchInternal::Sequence(b), mem, opts, db,
                            filter_deletes);
  return b->Iterate(&inserter);
}

void WriteBatchInternal::SetContents(WriteBatch* b, const Slice& contents) {
  assert(contents.size() >= kHeader);
  b->rep_.assign(contents.data(), contents.size());
}

void WriteBatchInternal::Append(WriteBatch* dst, const WriteBatch* src) {
  SetCount(dst, Count(dst) + Count(src));
  assert(src->rep_.size() >= kHeader);
  dst->rep_.append(src->rep_.data() + kHeader, src->rep_.size() - kHeader);
}

}  // namespace rocksdb
