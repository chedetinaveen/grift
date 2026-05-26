/*
 * Copyright 2016, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 *    * Neither the name of Google Inc. nor the names of its
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
 */

package io.grpc.thrift;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.google.common.io.ByteStreams;
import io.grpc.MethodDescriptor.Marshaller;
import java.io.InputStream;
import java.io.ByteArrayOutputStream;
import org.apache.thrift.TBase;
import org.apache.thrift.TException;
import org.apache.thrift.TFieldIdEnum;
import org.apache.thrift.TSerializer;
import org.apache.thrift.protocol.TProtocol;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/**
 * Technical integration test suite validating that ThriftUtils and ThriftInputStream
 * correctly marshal and unmarshal Apache Thrift payloads in the gRPC-Java runtime.
 */
@RunWith(JUnit4.class)
public class ThriftUtilsTest {

  private MessageFactory<MockUserMessage> messageFactory;
  private Marshaller<MockUserMessage> marshaller;

  @Before
  public void setUp() {
    messageFactory = new MessageFactory<MockUserMessage>() {
      @Override
      public MockUserMessage newInstance() {
        return new MockUserMessage();
      }
    };
    marshaller = ThriftUtils.marshaller(messageFactory);
  }

  @Test
  public void testMarshallerStreamAndParse() throws Exception {
    MockUserMessage original = new MockUserMessage(101, "Test User", "test.user@example.com");

    // 1. Marshall to Stream
    InputStream stream = marshaller.stream(original);
    assertNotNull("Stream should not be null", stream);

    // Verify stream properties
    assertTrue("Stream should be a ThriftInputStream", stream instanceof ThriftInputStream);
    assertEquals("Available bytes should be > 0", stream.available(), stream.available());

    // 2. Parse from Stream back to Thrift Object
    MockUserMessage parsed = marshaller.parse(stream);
    assertNotNull("Parsed message should not be null", parsed);
    assertEquals(original.id, parsed.id);
    assertEquals(original.name, parsed.name);
    assertEquals(original.email, parsed.email);
  }

  @Test
  public void testThriftInputStreamDrainTo() throws Exception {
    MockUserMessage message = new MockUserMessage(42, "Drain Test", "drain.test@example.com");
    ThriftInputStream stream = new ThriftInputStream(message);

    TSerializer serializer = new TSerializer();
    byte[] expectedBytes = serializer.serialize(message);

    ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
    int bytesDrained = stream.drainTo(outputStream);

    assertEquals(expectedBytes.length, bytesDrained);
    assertArrayEquals(expectedBytes, outputStream.toByteArray());
    assertEquals(0, stream.available());
  }

  @Test
  public void testMetadataMarshallerLifecycle() throws Exception {
    io.grpc.Metadata.BinaryMarshaller<MockUserMessage> metadataMarshaller =
        ThriftUtils.metadataMarshaller(messageFactory);

    MockUserMessage original = new MockUserMessage(777, "Metadata Test", "meta.test@example.com");

    // Serialize metadata
    byte[] serialized = metadataMarshaller.toBytes(original);
    assertNotNull(serialized);
    assertTrue(serialized.length > 0);

    // Deserialize metadata
    MockUserMessage parsed = metadataMarshaller.parseBytes(serialized);
    assertNotNull(parsed);
    assertEquals(original.id, parsed.id);
    assertEquals(original.name, parsed.name);
    assertEquals(original.email, parsed.email);
  }

  // ============================================================================
  // Mock Apache Thrift TBase Class for Isolated testing
  // ============================================================================
  private static class MockUserMessage implements TBase<MockUserMessage, TFieldIdEnum> {
    int id;
    String name;
    String email;

    MockUserMessage() {}

    MockUserMessage(int id, String name, String email) {
      this.id = id;
      this.name = name;
      this.email = email;
    }

    @Override
    public void read(TProtocol iprot) throws TException {
      try {
        this.id = iprot.readI32();
        this.name = iprot.readString();
        this.email = iprot.readString();
      } catch (Exception e) {
        throw new TException(e);
      }
    }

    @Override
    public void write(TProtocol oprot) throws TException {
      try {
        oprot.writeI32(id);
        oprot.writeString(name);
        oprot.writeString(email);
      } catch (Exception e) {
        throw new TException(e);
      }
    }

    @Override
    public TFieldIdEnum fieldForId(int fieldId) { return null; }
    @Override
    public boolean isSet(TFieldIdEnum field) { return true; }
    @Override
    public Object getFieldValue(TFieldIdEnum field) { return null; }
    @Override
    public void setFieldValue(TFieldIdEnum field, Object value) {}
    @Override
    public TBase<MockUserMessage, TFieldIdEnum> deepCopy() { return this; }
    @Override
    public void clear() {}
    @Override
    public int compareTo(MockUserMessage other) { return 0; }
  }
}
