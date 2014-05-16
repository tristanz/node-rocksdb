// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once
#include <deque>
#include <string>

#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/merge_operator.h"
#include "utilities/utility_db.h"
#include "db/db_impl.h"

namespace rocksdb {

class DBWithTTL : public StackableDB {
 public:
  static void SanitizeOptions(int32_t ttl, Options* options);

  explicit DBWithTTL(DB* db);

  virtual ~DBWithTTL();

  virtual Status Put(const WriteOptions& o, const Slice& key,
                     const Slice& val) override;

  virtual Status Get(const ReadOptions& options, const Slice& key,
                     std::string* value) override;

  virtual std::vector<Status> MultiGet(
      const ReadOptions& options, const std::vector<Slice>& keys,
      std::vector<std::string>* values) override;

  virtual bool KeyMayExist(const ReadOptions& options,
                           const Slice& key,
                           std::string* value,
                           bool* value_found = nullptr) override;

  virtual Status Merge(const WriteOptions& options, const Slice& key,
                       const Slice& value) override;

  virtual Status Write(const WriteOptions& opts, WriteBatch* updates) override;

  virtual Iterator* NewIterator(const ReadOptions& opts) override;

  // Simulate a db crash, no elegant closing of database.
  void TEST_Destroy_DBWithTtl();

  virtual DB* GetBaseDB() {
    return db_;
  }

  static bool IsStale(const Slice& value, int32_t ttl);

  static Status AppendTS(const Slice& val, std::string& val_with_ts);

  static Status SanityCheckTimestamp(const Slice& str);

  static Status StripTS(std::string* str);

  static Status GetCurrentTime(int64_t& curtime);

  static const uint32_t kTSLength = sizeof(int32_t); // size of timestamp

  static const int32_t kMinTimestamp = 1368146402; // 05/09/2013:5:40PM GMT-8

  static const int32_t kMaxTimestamp = 2147483647; // 01/18/2038:7:14PM GMT-8
};

class TtlIterator : public Iterator {

 public:
  explicit TtlIterator(Iterator* iter)
    : iter_(iter) {
    assert(iter_);
  }

  ~TtlIterator() {
    delete iter_;
  }

  bool Valid() const {
    return iter_->Valid();
  }

  void SeekToFirst() {
    iter_->SeekToFirst();
  }

  void SeekToLast() {
    iter_->SeekToLast();
  }

  void Seek(const Slice& target) {
    iter_->Seek(target);
  }

  void Next() {
    iter_->Next();
  }

  void Prev() {
    iter_->Prev();
  }

  Slice key() const {
    return iter_->key();
  }

  int32_t timestamp() const {
    return DecodeFixed32(
      iter_->value().data() + iter_->value().size() - DBWithTTL::kTSLength);
  }

  Slice value() const {
    //TODO: handle timestamp corruption like in general iterator semantics
    assert(DBWithTTL::SanityCheckTimestamp(iter_->value()).ok());
    Slice trimmed_value = iter_->value();
    trimmed_value.size_ -= DBWithTTL::kTSLength;
    return trimmed_value;
  }

  Status status() const {
    return iter_->status();
  }

 private:
  Iterator* iter_;
};

class TtlCompactionFilter : public CompactionFilter {

 public:
  TtlCompactionFilter(
      int32_t ttl,
      const CompactionFilter* user_comp_filter,
      std::unique_ptr<const CompactionFilter>
      user_comp_filter_from_factory = nullptr)
    : ttl_(ttl),
      user_comp_filter_(user_comp_filter),
      user_comp_filter_from_factory_(std::move(user_comp_filter_from_factory)) {
    // Unlike the merge operator, compaction filter is necessary for TTL, hence
    // this would be called even if user doesn't specify any compaction-filter
    if (!user_comp_filter_) {
      user_comp_filter_ = user_comp_filter_from_factory_.get();
    }
  }

  virtual bool Filter(int level,
                      const Slice& key,
                      const Slice& old_val,
                      std::string* new_val,
                      bool* value_changed) const override {
    if (DBWithTTL::IsStale(old_val, ttl_)) {
      return true;
    }
    if (user_comp_filter_ == nullptr) {
      return false;
    }
    assert(old_val.size() >= DBWithTTL::kTSLength);
    Slice old_val_without_ts(old_val.data(),
                             old_val.size() - DBWithTTL::kTSLength);
    if (user_comp_filter_->Filter(level, key, old_val_without_ts, new_val,
                                  value_changed)) {
      return true;
    }
    if (*value_changed) {
      new_val->append(old_val.data() + old_val.size() - DBWithTTL::kTSLength,
                      DBWithTTL::kTSLength);
    }
    return false;
  }

  virtual const char* Name() const override {
    return "Delete By TTL";
  }

 private:
  int32_t ttl_;
  const CompactionFilter* user_comp_filter_;
  std::unique_ptr<const CompactionFilter> user_comp_filter_from_factory_;
};

class TtlCompactionFilterFactory : public CompactionFilterFactory {
  public:
    TtlCompactionFilterFactory(
        int32_t ttl,
        std::shared_ptr<CompactionFilterFactory> comp_filter_factory)
    : ttl_(ttl),
      user_comp_filter_factory_(comp_filter_factory) { }

    virtual std::unique_ptr<CompactionFilter> CreateCompactionFilter(
        const CompactionFilter::Context& context) {
      return std::unique_ptr<TtlCompactionFilter>(
        new TtlCompactionFilter(
          ttl_,
          nullptr,
          std::move(user_comp_filter_factory_->CreateCompactionFilter(context))
        )
      );
    }

    virtual const char* Name() const override {
      return "TtlCompactionFilterFactory";
    }

  private:
    int32_t ttl_;
    std::shared_ptr<CompactionFilterFactory> user_comp_filter_factory_;
};

class TtlMergeOperator : public MergeOperator {

 public:
  explicit TtlMergeOperator(const std::shared_ptr<MergeOperator> merge_op)
    : user_merge_op_(merge_op) {
    assert(merge_op);
  }

  virtual bool FullMerge(const Slice& key,
                         const Slice* existing_value,
                         const std::deque<std::string>& operands,
                         std::string* new_value,
                         Logger* logger) const override {
    const uint32_t ts_len = DBWithTTL::kTSLength;
    if (existing_value && existing_value->size() < ts_len) {
      Log(logger, "Error: Could not remove timestamp from existing value.");
      return false;
    }

    // Extract time-stamp from each operand to be passed to user_merge_op_
    std::deque<std::string> operands_without_ts;
    for (const auto &operand : operands) {
      if (operand.size() < ts_len) {
        Log(logger, "Error: Could not remove timestamp from operand value.");
        return false;
      }
      operands_without_ts.push_back(operand.substr(0, operand.size() - ts_len));
    }

    // Apply the user merge operator (store result in *new_value)
    bool good = true;
    if (existing_value) {
      Slice existing_value_without_ts(existing_value->data(),
                                      existing_value->size() - ts_len);
      good = user_merge_op_->FullMerge(key, &existing_value_without_ts,
                                       operands_without_ts, new_value, logger);
    } else {
      good = user_merge_op_->FullMerge(key, nullptr, operands_without_ts,
                                       new_value, logger);
    }

    // Return false if the user merge operator returned false
    if (!good) {
      return false;
    }

    // Augment the *new_value with the ttl time-stamp
    int64_t curtime;
    if (!DBWithTTL::GetCurrentTime(curtime).ok()) {
      Log(logger, "Error: Could not get current time to be attached internally "
                  "to the new value.");
      return false;
    } else {
      char ts_string[ts_len];
      EncodeFixed32(ts_string, (int32_t)curtime);
      new_value->append(ts_string, ts_len);
      return true;
    }
  }

  virtual bool PartialMergeMulti(const Slice& key,
                                 const std::deque<Slice>& operand_list,
                                 std::string* new_value, Logger* logger) const
      override {
    const uint32_t ts_len = DBWithTTL::kTSLength;
    std::deque<Slice> operands_without_ts;

    for (const auto& operand : operand_list) {
      if (operand.size() < ts_len) {
        Log(logger, "Error: Could not remove timestamp from value.");
        return false;
      }

      operands_without_ts.push_back(
          Slice(operand.data(), operand.size() - ts_len));
    }

    // Apply the user partial-merge operator (store result in *new_value)
    assert(new_value);
    if (!user_merge_op_->PartialMergeMulti(key, operands_without_ts, new_value,
                                           logger)) {
      return false;
    }

    // Augment the *new_value with the ttl time-stamp
    int64_t curtime;
    if (!DBWithTTL::GetCurrentTime(curtime).ok()) {
      Log(logger, "Error: Could not get current time to be attached internally "
                  "to the new value.");
      return false;
    } else {
      char ts_string[ts_len];
      EncodeFixed32(ts_string, (int32_t)curtime);
      new_value->append(ts_string, ts_len);
      return true;
    }

  }

  virtual const char* Name() const override {
    return "Merge By TTL";
  }

 private:
  std::shared_ptr<MergeOperator> user_merge_op_;
};

}
