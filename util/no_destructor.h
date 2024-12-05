// Copyright (c) 2018 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef STORAGE_LEVELDB_UTIL_NO_DESTRUCTOR_H_
#define STORAGE_LEVELDB_UTIL_NO_DESTRUCTOR_H_

#include <type_traits>
#include <utility>

namespace leveldb {

// Wraps an instance whose destructor is never called.
//
// This is intended for use with function-level static variables.
// 禁止对象析构模板
template <typename InstanceType>
class NoDestructor {
 public:
  template <typename... ConstructorArgTypes>
  explicit NoDestructor(ConstructorArgTypes&&... constructor_args) {
    static_assert(sizeof(instance_storage_) >= sizeof(InstanceType),
                  "instance_storage_ is not large enough to hold the instance");
    static_assert(
        alignof(decltype(instance_storage_)) >= alignof(InstanceType),
        "instance_storage_ does not meet the instance's alignment requirement");
    // C++ 原地构造，&instance_storage_提供了一个地址，告诉编译器在这个已经分配好的内存上构造instanceTyp对象，避免额外内存分配
    new (&instance_storage_)
        InstanceType(std::forward<ConstructorArgTypes>(constructor_args)...);
  }

  ~NoDestructor() = default;

  NoDestructor(const NoDestructor&) = delete;
  NoDestructor& operator=(const NoDestructor&) = delete;

  InstanceType* get() {
    return reinterpret_cast<InstanceType*>(&instance_storage_);
  }

 private:
  /**
   * std::aligned_storage 创建的原始内存区域和 NoDestructor 对象所在的内存区域一致，
   * 也就是说如果 NoDestructor 被定义为一个函数内的局部变量，那么它和其内的 instance_storage_ 都会位于栈上。
   * 如果 NoDestructor 被定义为静态或全局变量，它和 instance_storage_ 将位于静态存储区，静态存储区的对象具有整个程序执行期间的生命周期。
   *  */ 
  typename std::aligned_storage<sizeof(InstanceType),
                                alignof(InstanceType)>::type instance_storage_;
};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_NO_DESTRUCTOR_H_

// 参考这个文章来进一步理解 https://zhuanlan.zhihu.com/p/710359253