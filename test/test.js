var assert = require("assert");
var rocksdb = require('../');

describe('RocksDB', function(){
  it('should return "world" when hello() called', function(){
    assert.equal(rocksdb.hello(), 'world');
  });
});