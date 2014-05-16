//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "rocksdb/options.h"

#include <limits>

#include "rocksdb/cache.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/comparator.h"
#include "rocksdb/env.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/memtablerep.h"
#include "rocksdb/merge_operator.h"
#include "rocksdb/slice.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/table.h"
#include "rocksdb/table_properties.h"
#include "table/block_based_table_factory.h"

namespace rocksdb {

Options::Options()
    : comparator(BytewiseComparator()),
      merge_operator(nullptr),
      compaction_filter(nullptr),
      compaction_filter_factory(std::shared_ptr<CompactionFilterFactory>(
          new DefaultCompactionFilterFactory())),
      compaction_filter_factory_v2(new DefaultCompactionFilterFactoryV2()),
      create_if_missing(false),
      error_if_exists(false),
      paranoid_checks(true),
      env(Env::Default()),
      info_log(nullptr),
      info_log_level(INFO),
      write_buffer_size(4 << 20),
      max_write_buffer_number(2),
      min_write_buffer_number_to_merge(1),
      max_open_files(5000),
      block_cache(nullptr),
      block_cache_compressed(nullptr),
      block_size(4096),
      block_restart_interval(16),
      compression(kSnappyCompression),
      filter_policy(nullptr),
      prefix_extractor(nullptr),
      whole_key_filtering(true),
      num_levels(7),
      level0_file_num_compaction_trigger(4),
      level0_slowdown_writes_trigger(20),
      level0_stop_writes_trigger(24),
      max_mem_compaction_level(2),
      target_file_size_base(2 * 1048576),
      target_file_size_multiplier(1),
      max_bytes_for_level_base(10 * 1048576),
      max_bytes_for_level_multiplier(10),
      max_bytes_for_level_multiplier_additional(num_levels, 1),
      expanded_compaction_factor(25),
      source_compaction_factor(1),
      max_grandparent_overlap_factor(10),
      disableDataSync(false),
      use_fsync(false),
      db_stats_log_interval(1800),
      db_log_dir(""),
      wal_dir(""),
      disable_seek_compaction(true),
      delete_obsolete_files_period_micros(6 * 60 * 60 * 1000000UL),
      max_background_compactions(1),
      max_background_flushes(1),
      max_log_file_size(0),
      log_file_time_to_roll(0),
      keep_log_file_num(1000),
      soft_rate_limit(0.0),
      hard_rate_limit(0.0),
      rate_limit_delay_max_milliseconds(1000),
      max_manifest_file_size(std::numeric_limits<uint64_t>::max()),
      no_block_cache(false),
      table_cache_numshardbits(4),
      table_cache_remove_scan_count_limit(16),
      arena_block_size(0),
      disable_auto_compactions(false),
      WAL_ttl_seconds(0),
      WAL_size_limit_MB(0),
      manifest_preallocation_size(4 * 1024 * 1024),
      purge_redundant_kvs_while_flush(true),
      allow_os_buffer(true),
      allow_mmap_reads(false),
      allow_mmap_writes(false),
      is_fd_close_on_exec(true),
      skip_log_error_on_recovery(false),
      stats_dump_period_sec(3600),
      block_size_deviation(10),
      advise_random_on_open(true),
      access_hint_on_compaction_start(NORMAL),
      use_adaptive_mutex(false),
      bytes_per_sync(0),
      compaction_style(kCompactionStyleLevel),
      verify_checksums_in_compaction(true),
      filter_deletes(false),
      max_sequential_skip_in_iterations(8),
      memtable_factory(std::shared_ptr<SkipListFactory>(new SkipListFactory)),
      table_factory(
          std::shared_ptr<TableFactory>(new BlockBasedTableFactory())),
      inplace_update_support(false),
      inplace_update_num_locks(10000),
      inplace_callback(nullptr),
      memtable_prefix_bloom_bits(0),
      memtable_prefix_bloom_probes(6),
      bloom_locality(0),
      max_successive_merges(0),
      min_partial_merge_operands(2),
      allow_thread_local(true) {
  assert(memtable_factory.get() != nullptr);
}

static const char* const access_hints[] = {
  "NONE", "NORMAL", "SEQUENTIAL", "WILLNEED"
};

void
Options::Dump(Logger* log) const
{
    Log(log,"              Options.comparator: %s", comparator->Name());
    Log(log,"          Options.merge_operator: %s",
        merge_operator? merge_operator->Name() : "None");
    Log(log,"       Options.compaction_filter: %s",
        compaction_filter? compaction_filter->Name() : "None");
    Log(log,"       Options.compaction_filter_factory: %s",
        compaction_filter_factory->Name());
    Log(log, "       Options.compaction_filter_factory_v2: %s",
        compaction_filter_factory_v2->Name());
    Log(log,"        Options.memtable_factory: %s",
        memtable_factory->Name());
    Log(log,"           Options.table_factory: %s", table_factory->Name());
    Log(log,"         Options.error_if_exists: %d", error_if_exists);
    Log(log,"       Options.create_if_missing: %d", create_if_missing);
    Log(log,"         Options.paranoid_checks: %d", paranoid_checks);
    Log(log,"                     Options.env: %p", env);
    Log(log,"                Options.info_log: %p", info_log.get());
    Log(log,"       Options.write_buffer_size: %zd", write_buffer_size);
    Log(log," Options.max_write_buffer_number: %d", max_write_buffer_number);
    Log(log,"          Options.max_open_files: %d", max_open_files);
    Log(log,"             Options.block_cache: %p", block_cache.get());
    Log(log,"  Options.block_cache_compressed: %p",
        block_cache_compressed.get());
    if (block_cache) {
      Log(log,"        Options.block_cache_size: %zd",
          block_cache->GetCapacity());
    }
    if (block_cache_compressed) {
      Log(log,"Options.block_cache_compressed_size: %zd",
          block_cache_compressed->GetCapacity());
    }
    Log(log,"              Options.block_size: %zd", block_size);
    Log(log,"  Options.block_restart_interval: %d", block_restart_interval);
    if (!compression_per_level.empty()) {
      for (unsigned int i = 0; i < compression_per_level.size(); i++) {
          Log(log,"       Options.compression[%d]: %d",
              i, compression_per_level[i]);
       }
    } else {
      Log(log,"         Options.compression: %d", compression);
    }
    Log(log,"         Options.filter_policy: %s",
        filter_policy == nullptr ? "nullptr" : filter_policy->Name());
    Log(log,"      Options.prefix_extractor: %s",
        prefix_extractor == nullptr ? "nullptr" : prefix_extractor->Name());
    Log(log,"   Options.whole_key_filtering: %d", whole_key_filtering);
    Log(log,"            Options.num_levels: %d", num_levels);
    Log(log,"       Options.disableDataSync: %d", disableDataSync);
    Log(log,"             Options.use_fsync: %d", use_fsync);
    Log(log,"     Options.max_log_file_size: %zu", max_log_file_size);
    Log(log,"Options.max_manifest_file_size: %lu",
        (unsigned long)max_manifest_file_size);
    Log(log,"     Options.log_file_time_to_roll: %zu", log_file_time_to_roll);
    Log(log,"     Options.keep_log_file_num: %zu", keep_log_file_num);
    Log(log," Options.db_stats_log_interval: %d",
        db_stats_log_interval);
    Log(log,"       Options.allow_os_buffer: %d", allow_os_buffer);
    Log(log,"      Options.allow_mmap_reads: %d", allow_mmap_reads);
    Log(log,"     Options.allow_mmap_writes: %d", allow_mmap_writes);
    Log(log,"       Options.min_write_buffer_number_to_merge: %d",
        min_write_buffer_number_to_merge);
    Log(log,"        Options.purge_redundant_kvs_while_flush: %d",
         purge_redundant_kvs_while_flush);
    Log(log,"           Options.compression_opts.window_bits: %d",
        compression_opts.window_bits);
    Log(log,"                 Options.compression_opts.level: %d",
        compression_opts.level);
    Log(log,"              Options.compression_opts.strategy: %d",
        compression_opts.strategy);
    Log(log,"     Options.level0_file_num_compaction_trigger: %d",
        level0_file_num_compaction_trigger);
    Log(log,"         Options.level0_slowdown_writes_trigger: %d",
        level0_slowdown_writes_trigger);
    Log(log,"             Options.level0_stop_writes_trigger: %d",
        level0_stop_writes_trigger);
    Log(log,"               Options.max_mem_compaction_level: %d",
        max_mem_compaction_level);
    Log(log,"                  Options.target_file_size_base: %d",
        target_file_size_base);
    Log(log,"            Options.target_file_size_multiplier: %d",
        target_file_size_multiplier);
    Log(log,"               Options.max_bytes_for_level_base: %lu",
        (unsigned long)max_bytes_for_level_base);
    Log(log,"         Options.max_bytes_for_level_multiplier: %d",
        max_bytes_for_level_multiplier);
    for (int i = 0; i < num_levels; i++) {
      Log(log,"Options.max_bytes_for_level_multiplier_addtl[%d]: %d",
          i, max_bytes_for_level_multiplier_additional[i]);
    }
    Log(log,"      Options.max_sequential_skip_in_iterations: %lu",
        (unsigned long)max_sequential_skip_in_iterations);
    Log(log,"             Options.expanded_compaction_factor: %d",
        expanded_compaction_factor);
    Log(log,"               Options.source_compaction_factor: %d",
        source_compaction_factor);
    Log(log,"         Options.max_grandparent_overlap_factor: %d",
        max_grandparent_overlap_factor);
    Log(log,"                             Options.db_log_dir: %s",
        db_log_dir.c_str());
    Log(log,"                             Options.wal_dir: %s",
        wal_dir.c_str());
    Log(log,"                Options.disable_seek_compaction: %d",
        disable_seek_compaction);
    Log(log,"                         Options.no_block_cache: %d",
        no_block_cache);
    Log(log,"               Options.table_cache_numshardbits: %d",
        table_cache_numshardbits);
    Log(log,"    Options.table_cache_remove_scan_count_limit: %d",
        table_cache_remove_scan_count_limit);
    Log(log,"                       Options.arena_block_size: %zu",
        arena_block_size);
    Log(log,"    Options.delete_obsolete_files_period_micros: %lu",
        (unsigned long)delete_obsolete_files_period_micros);
    Log(log,"             Options.max_background_compactions: %d",
        max_background_compactions);
    Log(log,"                 Options.max_background_flushes: %d",
        max_background_flushes);
    Log(log,"                      Options.soft_rate_limit: %.2f",
        soft_rate_limit);
    Log(log,"                      Options.hard_rate_limit: %.2f",
        hard_rate_limit);
    Log(log,"      Options.rate_limit_delay_max_milliseconds: %u",
        rate_limit_delay_max_milliseconds);
    Log(log,"               Options.disable_auto_compactions: %d",
        disable_auto_compactions);
    Log(log,"                        Options.WAL_ttl_seconds: %lu",
        (unsigned long)WAL_ttl_seconds);
    Log(log,"                      Options.WAL_size_limit_MB: %lu",
        (unsigned long)WAL_size_limit_MB);
    Log(log,"            Options.manifest_preallocation_size: %zu",
        manifest_preallocation_size);
    Log(log,"         Options.purge_redundant_kvs_while_flush: %d",
        purge_redundant_kvs_while_flush);
    Log(log,"                         Options.allow_os_buffer: %d",
        allow_os_buffer);
    Log(log,"                        Options.allow_mmap_reads: %d",
        allow_mmap_reads);
    Log(log,"                       Options.allow_mmap_writes: %d",
        allow_mmap_writes);
    Log(log,"                     Options.is_fd_close_on_exec: %d",
        is_fd_close_on_exec);
    Log(log,"              Options.skip_log_error_on_recovery: %d",
        skip_log_error_on_recovery);
    Log(log,"                   Options.stats_dump_period_sec: %u",
        stats_dump_period_sec);
    Log(log,"                    Options.block_size_deviation: %d",
        block_size_deviation);
    Log(log,"                   Options.advise_random_on_open: %d",
        advise_random_on_open);
    Log(log,"         Options.access_hint_on_compaction_start: %s",
        access_hints[access_hint_on_compaction_start]);
    Log(log,"                      Options.use_adaptive_mutex: %d",
        use_adaptive_mutex);
    Log(log,"                          Options.bytes_per_sync: %lu",
        (unsigned long)bytes_per_sync);
    Log(log,"                          Options.filter_deletes: %d",
        filter_deletes);
    Log(log, "          Options.verify_checksums_in_compaction: %d",
        verify_checksums_in_compaction);
    Log(log,"                        Options.compaction_style: %d",
        compaction_style);
    Log(log," Options.compaction_options_universal.size_ratio: %u",
        compaction_options_universal.size_ratio);
    Log(log,"Options.compaction_options_universal.min_merge_width: %u",
        compaction_options_universal.min_merge_width);
    Log(log,"Options.compaction_options_universal.max_merge_width: %u",
        compaction_options_universal.max_merge_width);
    Log(log,"Options.compaction_options_universal."
            "max_size_amplification_percent: %u",
        compaction_options_universal.max_size_amplification_percent);
    Log(log,
        "Options.compaction_options_universal.compression_size_percent: %u",
        compaction_options_universal.compression_size_percent);
    std::string collector_names;
    for (auto collector : table_properties_collectors) {
      collector_names.append(collector->Name());
      collector_names.append("; ");
    }
    Log(log, "                  Options.table_properties_collectors: %s",
        collector_names.c_str());
    Log(log, "                  Options.inplace_update_support: %d",
        inplace_update_support);
    Log(log, "                Options.inplace_update_num_locks: %zd",
        inplace_update_num_locks);
    Log(log, "              Options.min_partial_merge_operands: %u",
        min_partial_merge_operands);
    // TODO: easier config for bloom (maybe based on avg key/value size)
    Log(log, "              Options.memtable_prefix_bloom_bits: %d",
        memtable_prefix_bloom_bits);
    Log(log, "            Options.memtable_prefix_bloom_probes: %d",
        memtable_prefix_bloom_probes);
    Log(log, "                   Options.max_successive_merges: %zd",
        max_successive_merges);
}   // Options::Dump

//
// The goal of this method is to create a configuration that
// allows an application to write all files into L0 and
// then do a single compaction to output all files into L1.
Options*
Options::PrepareForBulkLoad()
{
  // never slowdown ingest.
  level0_file_num_compaction_trigger = (1<<30);
  level0_slowdown_writes_trigger = (1<<30);
  level0_stop_writes_trigger = (1<<30);

  // no auto compactions please. The application should issue a
  // manual compaction after all data is loaded into L0.
  disable_auto_compactions = true;
  disable_seek_compaction = true;
  disableDataSync = true;

  // A manual compaction run should pick all files in L0 in
  // a single compaction run.
  source_compaction_factor = (1<<30);

  // It is better to have only 2 levels, otherwise a manual
  // compaction would compact at every possible level, thereby
  // increasing the total time needed for compactions.
  num_levels = 2;

  // Prevent a memtable flush to automatically promote files
  // to L1. This is helpful so that all files that are
  // input to the manual compaction are all at L0.
  max_background_compactions = 2;

  // The compaction would create large files in L1.
  target_file_size_base = 256 * 1024 * 1024;
  return this;
}

}  // namespace rocksdb
