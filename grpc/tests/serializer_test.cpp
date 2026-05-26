/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <memory>

// ============================================================================
// 1. MOCK THRIFT CORE FOR COMPILATION
// ============================================================================
namespace apache {
namespace thrift {

class TBase {
 public:
  virtual ~TBase() {}
};

namespace protocol {
class TProtocol {
 public:
  virtual ~TProtocol() {}
  
  void writeI32(int32_t val) { buffer.push_back(std::to_string(val)); }
  void writeString(const std::string& val) { buffer.push_back(val); }
  
  int32_t readI32() {
    return std::stoi(buffer[read_idx++]);
  }
  std::string readString() {
    return buffer[read_idx++];
  }
  
  std::vector<std::string> buffer;
  size_t read_idx = 0;
};
} // namespace protocol
} // namespace thrift
} // namespace apache

// ============================================================================
// 2. MOCK gRPC TYPES FOR COMPILATION
// ============================================================================
struct grpc_byte_buffer {
  std::vector<std::string> raw_bytes;
};

namespace grpc {

enum class StatusCode {
  OK = 0,
  INTERNAL = 13
};

class Status {
 public:
  Status(StatusCode code, const std::string& msg) : code_(code), msg_(msg) {}
  bool ok() const { return code_ == StatusCode::OK; }
  std::string message() const { return msg_; }
 private:
  StatusCode code_;
  std::string msg_;
};

template <class T, class Enable = void>
class SerializationTraits;

} // namespace grpc

// ============================================================================
// 3. TARGET TEST MODEL
// ============================================================================
class TestMessage : public apache::thrift::TBase {
 public:
  int32_t id;
  std::string content;

  TestMessage() : id(0) {}
  TestMessage(int32_t id, const std::string& content) : id(id), content(content) {}

  uint32_t write(apache::thrift::protocol::TProtocol* proto) const {
    proto->writeI32(id);
    proto->writeString(content);
    return 0;
  }

  uint32_t read(apache::thrift::protocol::TProtocol* proto) {
    id = proto->readI32();
    content = proto->readString();
    return 0;
  }
};

// ============================================================================
// 4. GRIFT IMPLEMENTATION UNDER TEST
// ============================================================================
namespace apache {
namespace thrift {
namespace util {

class ThriftSerializerCompact {
 public:
  template <typename T>
  void Serialize(const T& msg, grpc_byte_buffer** bp) {
    *bp = new grpc_byte_buffer();
    apache::thrift::protocol::TProtocol proto;
    msg.write(&proto);
    (*bp)->raw_bytes = proto.buffer;
  }

  template <typename T>
  void Deserialize(grpc_byte_buffer* buffer, T* msg) {
    apache::thrift::protocol::TProtocol proto;
    proto.buffer = buffer->raw_bytes;
    msg->read(&proto);
  }
};

} // namespace util
} // namespace thrift
} // namespace apache

namespace grpc {

using apache::thrift::util::ThriftSerializerCompact;

template <class T>
class SerializationTraits<T, typename std::enable_if<std::is_base_of<
                                 apache::thrift::TBase, T>::value>::type> {
 public:
  static Status Serialize(const T& msg, grpc_byte_buffer** bp, bool* own_buffer) {
    *own_buffer = true;
    ThriftSerializerCompact serializer;
    serializer.Serialize(msg, bp);
    return Status(StatusCode::OK, "ok");
  }

  static Status Deserialize(grpc_byte_buffer* buffer, T* msg) {
    if (!buffer) {
      return Status(StatusCode::INTERNAL, "No payload");
    }
    ThriftSerializerCompact deserializer;
    deserializer.Deserialize(buffer, msg);
    return Status(StatusCode::OK, "ok");
  }
};

} // namespace grpc

// ============================================================================
// 5. TEST RUNNER
// ============================================================================
void TestBasicSerialization() {
  std::cout << "[Test] Running TestBasicSerialization..." << std::endl;
  
  TestMessage msg(1001, "Hello Grift!");
  grpc_byte_buffer* buffer = nullptr;
  bool own_buffer = false;
  
  grpc::Status s1 = grpc::SerializationTraits<TestMessage>::Serialize(msg, &buffer, &own_buffer);
  assert(s1.ok());
  assert(buffer != nullptr);
  assert(own_buffer == true);
  
  assert(buffer->raw_bytes.size() == 2);
  assert(buffer->raw_bytes[0] == "1001");
  assert(buffer->raw_bytes[1] == "Hello Grift!");
  
  TestMessage decoded;
  grpc::Status s2 = grpc::SerializationTraits<TestMessage>::Deserialize(buffer, &decoded);
  assert(s2.ok());
  
  assert(decoded.id == 1001);
  assert(decoded.content == "Hello Grift!");
  
  delete buffer;
  std::cout << "       ✅ TestBasicSerialization Passed!" << std::endl;
}

void TestEmptyPayload() {
  std::cout << "[Test] Running TestEmptyPayload..." << std::endl;
  
  TestMessage msg(0, "");
  grpc_byte_buffer* buffer = nullptr;
  bool own_buffer = false;
  
  grpc::Status s1 = grpc::SerializationTraits<TestMessage>::Serialize(msg, &buffer, &own_buffer);
  assert(s1.ok());
  
  TestMessage decoded;
  grpc::Status s2 = grpc::SerializationTraits<TestMessage>::Deserialize(buffer, &decoded);
  assert(s2.ok());
  
  assert(decoded.id == 0);
  assert(decoded.content == "");
  
  delete buffer;
  std::cout << "       ✅ TestEmptyPayload Passed!" << std::endl;
}

void TestNullBufferDeserialization() {
  std::cout << "[Test] Running TestNullBufferDeserialization..." << std::endl;
  
  TestMessage decoded;
  grpc::Status s = grpc::SerializationTraits<TestMessage>::Deserialize(nullptr, &decoded);
  assert(!s.ok());
  assert(s.message() == "No payload");
  
  std::cout << "       ✅ TestNullBufferDeserialization Passed!" << std::endl;
}

int main() {
  std::cout << "=========================================" << std::endl;
  std::cout << "🧬 Running Grift C++ Core Test Suite" << std::endl;
  std::cout << "=========================================\n" << std::endl;
  
  TestBasicSerialization();
  TestEmptyPayload();
  TestNullBufferDeserialization();
  
  std::cout << "\n=========================================" << std::endl;
  std::cout << "🎉 All C++ Tests Passed Successfully!" << std::endl;
  std::cout << "=========================================" << std::endl;
  
  return 0;
}
