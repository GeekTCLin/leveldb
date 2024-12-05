// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "table/filter_block.h"

#include "leveldb/filter_policy.h"
#include "util/coding.h"

namespace leveldb {

// See doc/table_format.md for an explanation of the filter block format.

// Generate new filter every 2KB of data
// 还是每2kb一个索引点，看看怎么理解，存在多个索引点指向同一个内容
// 可以这样理解，一个datablock 对应一个 filter_block，假设偏移量为0的datablock 占用7kb，则存在 index[0] = index[1] = index[2]，这几个index存储的偏移量都是一样的
// 虽然这几个偏移量都一样，但使用datablock的偏移量除以2k，获得的是index[2]这个值，也就是相等值中的最后一个
static const size_t kFilterBaseLg = 11;
static const size_t kFilterBase = 1 << kFilterBaseLg;

FilterBlockBuilder::FilterBlockBuilder(const FilterPolicy* policy)
    : policy_(policy) {}

void FilterBlockBuilder::StartBlock(uint64_t block_offset) {
  uint64_t filter_index = (block_offset / kFilterBase);
  assert(filter_index >= filter_offsets_.size());
  while (filter_index > filter_offsets_.size()) {
    GenerateFilter();
  }
}

// 这里只是预记录下key和 key的偏移量
void FilterBlockBuilder::AddKey(const Slice& key) {
  Slice k = key;
  start_.push_back(keys_.size());
  keys_.append(k.data(), k.size());
}

Slice FilterBlockBuilder::Finish() {
  if (!start_.empty()) {
    GenerateFilter();
  }

  // Append array of per-filter offsets
  // 存储总长度（总偏移量）
  const uint32_t array_offset = result_.size();
  for (size_t i = 0; i < filter_offsets_.size(); i++) {
    PutFixed32(&result_, filter_offsets_[i]);
  }

  // result_ 尾部存储偏移量的个数
  PutFixed32(&result_, array_offset);
  result_.push_back(kFilterBaseLg);  // Save encoding parameter in result 存储11这个偏移量
  // array_offset 和 kFilterBaseLg 总共占用5个字节
  return Slice(result_);
}

void FilterBlockBuilder::GenerateFilter() {
  //num_keys key的个数
  const size_t num_keys = start_.size();
  if (num_keys == 0) {
    // Fast path if there are no keys for this filter
    filter_offsets_.push_back(result_.size());
    return;
  }

  // Make list of keys from flattened key structure
  // 存储这个最后的长度（偏移量）是为了下面这个循环计算key的长度
  start_.push_back(keys_.size());  // Simplify length computation
  tmp_keys_.resize(num_keys);
  for (size_t i = 0; i < num_keys; i++) {
	// base 定位第i个key
    const char* base = keys_.data() + start_[i];
	// key的长度
    size_t length = start_[i + 1] - start_[i];
    tmp_keys_[i] = Slice(base, length);
  }
  // tmp_keys_ 为本次计算布隆过滤器的key列表

  // Generate filter for current set of keys and append to result_.
  // 计算布隆过滤器结果进入增加至 result_中
  filter_offsets_.push_back(result_.size());
  policy_->CreateFilter(&tmp_keys_[0], static_cast<int>(num_keys), &result_);

  tmp_keys_.clear();
  keys_.clear();
  start_.clear();
}

FilterBlockReader::FilterBlockReader(const FilterPolicy* policy,
                                     const Slice& contents)
    : policy_(policy), data_(nullptr), offset_(nullptr), num_(0), base_lg_(0) {
  size_t n = contents.size();
  if (n < 5) return;  // 1 byte for base_lg_ and 4 for start of offset array
  // 取出 kFilterBaseLg，如果无修改则为11
  base_lg_ = contents[n - 1];
  // last_word 取出总result_ 总长度
  uint32_t last_word = DecodeFixed32(contents.data() + n - 5);
  if (last_word > n - 5) return;
  data_ = contents.data();
  // offset_ 指向偏移量开始位置
  offset_ = data_ + last_word;
  // 减去 5（尾部result_ 总长度字节数 + kFilterBaseLg） 和 result_ 总字节数 = 剩余偏移量占用的字节数，每4个字节为一个32位偏移量
  num_ = (n - 5 - last_word) / 4; // 偏移量的个数
}

/**
 * 匹配key是否在当前过滤器中
 * key          关键值
 * block_offset data_block 偏移量
 */
bool FilterBlockReader::KeyMayMatch(uint64_t block_offset, const Slice& key) {
  // 计算下标，block_offset 除以 2kb
  uint64_t index = block_offset >> base_lg_;
  if (index < num_) {
    // 获取开始偏移量 start 和 结束偏移量limit
    uint32_t start = DecodeFixed32(offset_ + index * 4);
    uint32_t limit = DecodeFixed32(offset_ + index * 4 + 4);
    if (start <= limit && limit <= static_cast<size_t>(offset_ - data_)) {
      // 获取出filter进行匹配
      Slice filter = Slice(data_ + start, limit - start);
      return policy_->KeyMayMatch(key, filter);
    } else if (start == limit) {
      // Empty filters do not match any keys
      return false;
    }
  }
  return true;  // Errors are treated as potential matches
}

}  // namespace leveldb
