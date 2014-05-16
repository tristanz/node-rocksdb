#include <cstdio>
#include <string>
#include <node.h>
#include <v8.h>

#include "rocksdb/db.h"
#include "rocksdb/slice.h"
#include "rocksdb/options.h"

using namespace v8;

Handle<Value> Method(const Arguments& args) {
  HandleScope scope;

  std::string kDBPath = "/tmp/rocksdb_simple_example";
  rocksdb::DB* db;
  rocksdb::Options options;
  // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
  options.IncreaseParallelism();
  options.OptimizeLevelStyleCompaction();
  // create the DB if it's not already present
  options.create_if_missing = true;

  // open DB
  rocksdb::Status s = rocksdb::DB::Open(options, kDBPath, &db);
  assert(s.ok());

  // Put key-value
  s = db->Put(rocksdb::WriteOptions(), "key", "value");
  assert(s.ok());
  std::string value;
  // get value
  s = db->Get(rocksdb::ReadOptions(), "key", &value);
  assert(s.ok());
  assert(value == "value");

  delete db;

  return scope.Close(String::New("sdf"));
}

void init(Handle<Object> exports) {
  exports->Set(String::NewSymbol("hello"),
    FunctionTemplate::New(Method)->GetFunction());
}

NODE_MODULE(binding, init)