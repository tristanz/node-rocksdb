{
  "targets": [
    {
      "target_name": "binding",
      "sources": [ "src/binding.cc" ],
      "include_dirs": ["deps/rocksdb/include"],
      "libraries": ["../deps/rocksdb/librocksdb.a", "-lsnappy", "-lz", "-lbz2"],
      "xcode_settings": {
        "MACOSX_DEPLOYMENT_TARGET": "10.8",
        "OTHER_CPLUSPLUSFLAGS" : ["-std=c++11", "-stdlib=libc++"],
        "OTHER_LDFLAGS": ["-stdlib=libc++"]
      },
      "cflags": ["-std=c++11"]
    }
  ]
}