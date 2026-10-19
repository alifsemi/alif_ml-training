/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#ifndef TENSORFLOW_LITE_MICRO_COMPATIBILITY_H_
#define TENSORFLOW_LITE_MICRO_COMPATIBILITY_H_

// Local override of the tensorflow-lite-micro 1.26.5 header.
//
// TF_LITE_STATIC_MEMORY gives virtual classes an empty operator delete so the
// link does not pull in global delete. That pack declares the operator in the
// private section. GCC 13 still checks that operator delete is accessible at a
// placement-new call, even with -fno-exceptions, so decode_state.cpp fails.
//
// The macro is used either at the end of a private section or at the start of
// a public section, so it switches to public and does not switch back.

#ifdef TF_LITE_STATIC_MEMORY
#define TF_LITE_REMOVE_VIRTUAL_DELETE \
 public:                              \
  static void operator delete(void* p) { (void)p; }
#else
#define TF_LITE_REMOVE_VIRTUAL_DELETE
#endif

#endif  // TENSORFLOW_LITE_MICRO_COMPATIBILITY_H_
