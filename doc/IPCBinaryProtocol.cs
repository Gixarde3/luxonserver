// Copyright (c) 2026, the Luxon contributors
// SPDX-License-Identifier: BSD-3-Clause

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;

namespace Luxon.Ser
{
    // Minimal Models Required for Compilation
    public enum ProtocolImplID { IPCBinary, Other }
    public interface IProtocol { ProtocolImplID GetProtocolImplID(); }
    
    public struct Value : IEquatable<Value>
    {
        public int TypeTag;
        public object Content;
        
        public Value(int typeTag, object content) 
        { 
            TypeTag = typeTag; 
            Content = content; 
        }

        public bool Equals(Value other)
        {
            if (TypeTag != other.TypeTag) return false;
            if (ReferenceEquals(Content, other.Content)) return true;
            if (Content == null || other.Content == null) return false;

            return TypeTag switch
            {
                9 or 30 => ((byte[])Content).SequenceEqual((byte[])other.Content),
                10 => ((bool[])Content).SequenceEqual((bool[])other.Content),
                11 => ((short[])Content).SequenceEqual((short[])other.Content),
                12 => ((int[])Content).SequenceEqual((int[])other.Content),
                13 => ((long[])Content).SequenceEqual((long[])other.Content),
                14 => ((float[])Content).SequenceEqual((float[])other.Content),
                15 => ((double[])Content).SequenceEqual((double[])other.Content),
                16 => ((string[])Content).SequenceEqual((string[])other.Content),
                17 or 18 => ((Value[])Content).SequenceEqual((Value[])other.Content),
                22 => ((RawCustomValue)Content).Equals((RawCustomValue)other.Content),
                _ => Content.Equals(other.Content)
            };
        }

        public override bool Equals(object obj) => obj is Value v && Equals(v);

        public override int GetHashCode()
        {
            if (Content == null) return TypeTag.GetHashCode();
            
            int contentHash;
            if (Content is byte[] b) contentHash = b.Length > 0 ? b[0] : 0;
            else if (Content is Array a) contentHash = a.Length;
            else contentHash = Content.GetHashCode();

            return HashCode.Combine(TypeTag, contentHash);
        }
    }

    public class Message
    {
        public byte VariantIndex { get; set; }
        public bool Encrypted { get; set; }
        public object Payload { get; set; }
    }

    public class InitMessage 
    { 
        public byte ProtocolMajor = 1, ProtocolMinor = 8, ClientSdkId = 15; 
        public bool Ipv6; 
        public byte VersionMajor = 4, VersionMinor = 1, VersionPatch = 6, VersionRevision = 8; 
        public string AppId = ""; 
    }
    
    public class InitResponseMessage { }
    
    public interface IOperationRequest { byte OperationCode { get; set; } Dictionary<byte, Value> Parameters { get; set; } }
    
    public class OperationRequestMessage : IOperationRequest 
    { 
        public byte OperationCode { get; set; } 
        public Dictionary<byte, Value> Parameters { get; set; } = new(); 
    }
    
    public class InternalOperationRequestMessage : IOperationRequest 
    { 
        public byte OperationCode { get; set; } 
        public Dictionary<byte, Value> Parameters { get; set; } = new(); 
    }
    
    public interface IOperationResponse { byte OperationCode { get; set; } short ReturnCode { get; set; } string DebugMessage { get; set; } Dictionary<byte, Value> Parameters { get; set; } }
    
    public class OperationResponseMessage : IOperationResponse 
    { 
        public byte OperationCode { get; set; } 
        public short ReturnCode { get; set; } 
        public string DebugMessage { get; set; } 
        public Dictionary<byte, Value> Parameters { get; set; } = new(); 
    }
    
    public class InternalOperationResponseMessage : IOperationResponse 
    { 
        public byte OperationCode { get; set; } 
        public short ReturnCode { get; set; } 
        public string DebugMessage { get; set; } 
        public Dictionary<byte, Value> Parameters { get; set; } = new(); 
    }
    
    public class EventMessage 
    { 
        public byte EventCode { get; set; } 
        public Dictionary<byte, Value> Parameters { get; set; } = new(); 
    }
    
    public class DisconnectMessage 
    { 
        public short Code { get; set; } 
        public string Message { get; set; } 
        public Dictionary<byte, Value> Parameters { get; set; } = new(); 
    }
    
    public class GenericValueMessage { public Value Value { get; set; } }
    
    public class RawMessage { public byte[] Bytes { get; set; } = Array.Empty<byte>(); }
    
    public class GenericDictionary 
    { 
        public byte[] Header { get; set; } = Array.Empty<byte>(); 
        public List<KeyValuePair<Value, Value>> Entries { get; set; } = new(); 
    }
    
    public struct RawCustomValue : IEquatable<RawCustomValue>
    { 
        public byte CustomCode { get; set; } 
        public byte[] Data { get; set; }

        public bool Equals(RawCustomValue other) => 
            CustomCode == other.CustomCode && (Data?.SequenceEqual(other.Data ?? Array.Empty<byte>()) ?? (other.Data == null));
            
        public override bool Equals(object obj) => obj is RawCustomValue rcv && Equals(rcv);
        public override int GetHashCode() => HashCode.Combine(CustomCode, Data?.Length ?? 0);
    }

    // Protocol Implementation
    public class IPCBinaryProtocol : IProtocol
    {
        private const byte MAGIC = 0xF5;
        private const int MAX_DEPTH = 64;

        public ProtocolImplID GetProtocolImplID() => ProtocolImplID.IPCBinary;

        public byte[] Serialize(Message message)
        {
            using var ms = new MemoryStream();
            using var w = new BinaryWriter(ms, Encoding.UTF8, true);

            w.Write(MAGIC);
            w.Write(message.VariantIndex);
            w.Write(message.Encrypted ? (byte)1 : (byte)0);

            switch (message.Payload)
            {
                case InitMessage im:
                    w.Write(im.ProtocolMajor); w.Write(im.ProtocolMinor); w.Write(im.ClientSdkId);
                    w.Write(im.Ipv6 ? (byte)1 : (byte)0);
                    w.Write(im.VersionMajor); w.Write(im.VersionMinor); w.Write(im.VersionPatch); w.Write(im.VersionRevision);
                    EncodeString(w, im.AppId ?? "");
                    break;
                case InitResponseMessage _: break;
                case OperationRequestMessage req: EncodeOpReq(w, req, 0); break;
                case InternalOperationRequestMessage ireq: EncodeOpReq(w, ireq, 0); break;
                case OperationResponseMessage resp: EncodeOpResp(w, resp, 0); break;
                case InternalOperationResponseMessage iresp: EncodeOpResp(w, iresp, 0); break;
                case EventMessage ev: EncodeEvent(w, ev, 0); break;
                case DisconnectMessage dm:
                    w.Write(dm.Code);
                    w.Write(dm.Message != null ? (byte)1 : (byte)0);
                    if (dm.Message != null) EncodeString(w, dm.Message);
                    EncodeDict(w, dm.Parameters, 0);
                    break;
                case GenericValueMessage gvm: EncodeValue(w, gvm.Value, 0); break;
                case RawMessage raw:
                    w.Write((uint)(raw.Bytes?.Length ?? 0));
                    if (raw.Bytes != null) w.Write(raw.Bytes);
                    break;
                default: throw new InvalidOperationException("Unsupported message variant.");
            }
            return ms.ToArray();
        }

        public Message Deserialize(ReadOnlySpan<byte> packetBytes)
        {
            unsafe
            {
                fixed (byte* ptr = packetBytes)
                {
                    using var ms = new UnmanagedMemoryStream(ptr, packetBytes.Length);
                    using var r = new BinaryReader(ms, Encoding.UTF8, true);

                    byte magic = r.ReadByte();
                    if (magic != MAGIC) throw new InvalidDataException("Invalid magic for IPCBinary");

                    byte msgIdx = r.ReadByte();
                    bool isEncrypted = r.ReadByte() != 0;

                    object payload = msgIdx switch
                    {
                        0 => new InitMessage { ProtocolMajor = r.ReadByte(), ProtocolMinor = r.ReadByte(), ClientSdkId = r.ReadByte(), Ipv6 = r.ReadByte() != 0, VersionMajor = r.ReadByte(), VersionMinor = r.ReadByte(), VersionPatch = r.ReadByte(), VersionRevision = r.ReadByte(), AppId = DecodeString(r) },
                        1 => new InitResponseMessage(),
                        2 => DecodeOpReq<OperationRequestMessage>(r, 0),
                        3 => DecodeOpResp<OperationResponseMessage>(r, 0),
                        4 => DecodeEvent(r, 0),
                        5 => new DisconnectMessage { Code = r.ReadInt16(), Message = r.ReadByte() != 0 ? DecodeString(r) : null, Parameters = DecodeDict(r, 0) },
                        6 => DecodeOpReq<InternalOperationRequestMessage>(r, 0),
                        7 => DecodeOpResp<InternalOperationResponseMessage>(r, 0),
                        8 => new GenericValueMessage { Value = DecodeValue(r, 0) },
                        9 => new RawMessage { Bytes = r.ReadBytes((int)r.ReadUInt32()) },
                        _ => throw new InvalidDataException("Unknown message variant in IPCBinary")
                    };

                    return new Message { VariantIndex = msgIdx, Encrypted = isEncrypted, Payload = payload };
                }
            }
        }

        public void EncodeValue(BinaryWriter w, Value v, int depth)
        {
            if (depth > MAX_DEPTH) throw new InvalidOperationException("Nesting too deep");

            w.Write((byte)v.TypeTag);
            switch (v.TypeTag)
            {
                case 0: break; 
                case 1: w.Write((bool)v.Content ? (byte)1 : (byte)0); break;
                case 2: w.Write((byte)v.Content); break;
                case 3: w.Write((short)v.Content); break;
                case 4: w.Write((int)v.Content); break;
                case 5: w.Write((long)v.Content); break;
                case 6: w.Write((float)v.Content); break;
                case 7: w.Write((double)v.Content); break;
                case 8: EncodeString(w, (string)v.Content); break;
                case 9: EncodeByteArray(w, (byte[])v.Content); break;
                case 10:
                    var bArr = (bool[])v.Content;
                    w.Write((uint)bArr.Length);
                    foreach (var b in bArr) w.Write(b ? (byte)1 : (byte)0);
                    break;
                case 11: EncodePodVector(w, (short[])v.Content); break;
                case 12: EncodePodVector(w, (int[])v.Content); break;
                case 13: EncodePodVector(w, (long[])v.Content); break;
                case 14: EncodePodVector(w, (float[])v.Content); break;
                case 15: EncodePodVector(w, (double[])v.Content); break;
                case 16:
                    var sArr = (string[])v.Content;
                    w.Write((uint)sArr.Length);
                    foreach (var s in sArr) EncodeString(w, s);
                    break;
                case 17: 
                case 18: 
                    var oArr = (Value[])v.Content;
                    w.Write((uint)oArr.Length);
                    foreach (var val in oArr) EncodeValue(w, val, depth + 1);
                    break;
                case 19: EncodeDict(w, (Dictionary<byte, Value>)v.Content, depth); break;
                case 20:
                    var gd = (GenericDictionary)v.Content;
                    EncodeByteArray(w, gd.Header);
                    w.Write((uint)gd.Entries.Count);
                    foreach (var kvp in gd.Entries)
                    {
                        EncodeValue(w, kvp.Key, depth + 1);
                        EncodeValue(w, kvp.Value, depth + 1);
                    }
                    break;
                case 21:
                    var ht = (Dictionary<Value, Value>)v.Content;
                    if (ht == null) w.Write(0xFFFFFFFF);
                    else
                    {
                        w.Write((uint)ht.Count);
                        foreach (var kvp in ht)
                        {
                            EncodeValue(w, kvp.Key, depth + 1);
                            EncodeValue(w, kvp.Value, depth + 1);
                        }
                    }
                    break;
                case 22:
                    var rcv = (RawCustomValue)v.Content;
                    w.Write(rcv.CustomCode);
                    EncodeByteArray(w, rcv.Data);
                    break;
                case 23: EncodeEvent(w, (EventMessage)v.Content, depth); break;
                case 24: EncodeOpReq(w, (OperationRequestMessage)v.Content, depth); break;
                case 25: EncodeOpResp(w, (OperationResponseMessage)v.Content, depth); break;
                case 26:
                    var dArr = (Dictionary<byte, Value>[])v.Content;
                    w.Write((uint)dArr.Length);
                    foreach (var dict in dArr) EncodeDict(w, dict, depth + 1);
                    break;
                case 27:
                    var gdArr = (GenericDictionary[])v.Content;
                    w.Write((uint)gdArr.Length);
                    foreach (var item in gdArr)
                    {
                        EncodeByteArray(w, item.Header);
                        w.Write((uint)item.Entries.Count);
                        foreach (var kvp in item.Entries)
                        {
                            EncodeValue(w, kvp.Key, depth + 1);
                            EncodeValue(w, kvp.Value, depth + 1);
                        }
                    }
                    break;
                case 28:
                    var htArr = (Dictionary<Value, Value>[])v.Content;
                    w.Write((uint)htArr.Length);
                    foreach (var item in htArr)
                    {
                        if (item == null) { w.Write(0xFFFFFFFF); continue; }
                        w.Write((uint)item.Count);
                        foreach (var kvp in item)
                        {
                            EncodeValue(w, kvp.Key, depth + 1);
                            EncodeValue(w, kvp.Value, depth + 1);
                        }
                    }
                    break;
                case 29:
                    var rcvArr = (RawCustomValue[])v.Content;
                    w.Write((uint)rcvArr.Length);
                    foreach (var cv in rcvArr) { w.Write(cv.CustomCode); EncodeByteArray(w, cv.Data); }
                    break;
                case 30: EncodeByteArray(w, (byte[])v.Content); break;
                default: throw new InvalidOperationException($"Unknown type index: {v.TypeTag}");
            }
        }

        public Value DecodeValue(BinaryReader r, int depth)
        {
            if (depth > MAX_DEPTH) throw new InvalidOperationException("Nesting too deep");

            byte typeTag = r.ReadByte();
            return typeTag switch
            {
                0 => new Value(0, null),
                1 => new Value(1, r.ReadByte() != 0),
                2 => new Value(2, r.ReadByte()),
                3 => new Value(3, r.ReadInt16()),
                4 => new Value(4, r.ReadInt32()),
                5 => new Value(5, r.ReadInt64()),
                6 => new Value(6, r.ReadSingle()),
                7 => new Value(7, r.ReadDouble()),
                8 => new Value(8, DecodeString(r)),
                9 => new Value(9, DecodeByteArray(r)),
                10 => new Value(10, Enumerable.Range(0, (int)r.ReadUInt32()).Select(_ => r.ReadByte() != 0).ToArray()),
                11 => new Value(11, DecodePodVector<short>(r)),
                12 => new Value(12, DecodePodVector<int>(r)),
                13 => new Value(13, DecodePodVector<long>(r)),
                14 => new Value(14, DecodePodVector<float>(r)),
                15 => new Value(15, DecodePodVector<double>(r)),
                16 => new Value(16, Enumerable.Range(0, (int)r.ReadUInt32()).Select(_ => DecodeString(r)).ToArray()),
                17 => new Value(17, Enumerable.Range(0, (int)r.ReadUInt32()).Select(_ => DecodeValue(r, depth + 1)).ToArray()),
                18 => new Value(18, Enumerable.Range(0, (int)r.ReadUInt32()).Select(_ => DecodeValue(r, depth + 1)).ToArray()),
                19 => new Value(19, DecodeDict(r, depth)),
                20 => new Value(20, DecodeGenericDict(r, depth)),
                21 => new Value(21, DecodeHashtable(r, depth)),
                22 => new Value(22, new RawCustomValue { CustomCode = r.ReadByte(), Data = DecodeByteArray(r) }),
                23 => new Value(23, DecodeEvent(r, depth)),
                24 => new Value(24, DecodeOpReq<OperationRequestMessage>(r, depth)),
                25 => new Value(25, DecodeOpResp<OperationResponseMessage>(r, depth)),
                26 => new Value(26, Enumerable.Range(0, (int)r.ReadUInt32()).Select(_ => DecodeDict(r, depth + 1)).ToArray()),
                27 => new Value(27, Enumerable.Range(0, (int)r.ReadUInt32()).Select(_ => DecodeGenericDict(r, depth + 1)).ToArray()),
                28 => new Value(28, Enumerable.Range(0, (int)r.ReadUInt32()).Select(_ => DecodeHashtable(r, depth + 1)).ToArray()),
                29 => new Value(29, Enumerable.Range(0, (int)r.ReadUInt32()).Select(_ => new RawCustomValue { CustomCode = r.ReadByte(), Data = DecodeByteArray(r) }).ToArray()),
                30 => new Value(30, DecodeByteArray(r)), 
                _ => throw new InvalidDataException($"Unknown type index: {typeTag}")
            };
        }

        private static void EncodeString(BinaryWriter w, string str)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(str ?? "");
            w.Write((uint)bytes.Length);
            w.Write(bytes);
        }
        
        private static string DecodeString(BinaryReader r)
        {
            uint len = r.ReadUInt32();
            return Encoding.UTF8.GetString(r.ReadBytes((int)len));
        }
        
        private static void EncodeByteArray(BinaryWriter w, byte[] arr) { w.Write((uint)arr.Length); w.Write(arr); }
        private static byte[] DecodeByteArray(BinaryReader r) => r.ReadBytes((int)r.ReadUInt32());

        private void EncodeDict(BinaryWriter w, Dictionary<byte, Value> d, int depth)
        {
            if (d == null) { w.Write((uint)0); return; }
            w.Write((uint)d.Count);
            foreach (var kvp in d) { w.Write(kvp.Key); EncodeValue(w, kvp.Value, depth + 1); }
        }
        
        private Dictionary<byte, Value> DecodeDict(BinaryReader r, int depth)
        {
            uint len = r.ReadUInt32();
            var dict = new Dictionary<byte, Value>((int)len);
            for (int i = 0; i < len; i++) dict[r.ReadByte()] = DecodeValue(r, depth + 1);
            return dict;
        }

        private GenericDictionary DecodeGenericDict(BinaryReader r, int depth)
        {
            var dict = new GenericDictionary { Header = DecodeByteArray(r) };
            uint len = r.ReadUInt32();
            for (int i = 0; i < len; i++) dict.Entries.Add(new KeyValuePair<Value, Value>(DecodeValue(r, depth + 1), DecodeValue(r, depth + 1)));
            return dict;
        }

        private Dictionary<Value, Value> DecodeHashtable(BinaryReader r, int depth)
        {
            uint len = r.ReadUInt32();
            if (len == 0xFFFFFFFF) return null;
            var ht = new Dictionary<Value, Value>((int)len);
            for (int i = 0; i < len; i++) ht.Add(DecodeValue(r, depth + 1), DecodeValue(r, depth + 1));
            return ht;
        }

        private void EncodeEvent(BinaryWriter w, EventMessage ev, int depth) { w.Write(ev.EventCode); EncodeDict(w, ev.Parameters, depth); }
        
        private EventMessage DecodeEvent(BinaryReader r, int depth) => new EventMessage { EventCode = r.ReadByte(), Parameters = DecodeDict(r, depth) };

        private void EncodeOpReq(BinaryWriter w, IOperationRequest req, int depth) { w.Write(req.OperationCode); EncodeDict(w, req.Parameters, depth); }
        
        private T DecodeOpReq<T>(BinaryReader r, int depth) where T : IOperationRequest, new() => new T { OperationCode = r.ReadByte(), Parameters = DecodeDict(r, depth) };

        private void EncodeOpResp(BinaryWriter w, IOperationResponse resp, int depth)
        {
            w.Write(resp.OperationCode); w.Write(resp.ReturnCode);
            w.Write(resp.DebugMessage != null ? (byte)1 : (byte)0);
            if (resp.DebugMessage != null) EncodeString(w, resp.DebugMessage);
            EncodeDict(w, resp.Parameters, depth);
        }
        
        private T DecodeOpResp<T>(BinaryReader r, int depth) where T : IOperationResponse, new()
        {
            var resp = new T { OperationCode = r.ReadByte(), ReturnCode = r.ReadInt16() };
            if (r.ReadByte() != 0) resp.DebugMessage = DecodeString(r);
            resp.Parameters = DecodeDict(r, depth);
            return resp;
        }

        private void EncodePodVector<T>(BinaryWriter w, T[] vec) where T : unmanaged
        {
            w.Write((uint)vec.Length);
            Span<byte> byteSpan = MemoryMarshal.AsBytes(vec.AsSpan());
            w.Write(byteSpan);
        }

        private T[] DecodePodVector<T>(BinaryReader r) where T : unmanaged
        {
            uint len = r.ReadUInt32();
            if (len == 0) return Array.Empty<T>();
            T[] vec = new T[len];
            Span<byte> byteSpan = MemoryMarshal.AsBytes(vec.AsSpan());
            r.BaseStream.ReadExactly(byteSpan);
            return vec;
        }

        public byte[] CreateInitEncryptionRequest() => throw new NotSupportedException();
        public void HandleInitEncryptionResponse(InternalOperationResponseMessage response) => throw new NotSupportedException();
        public InternalOperationResponseMessage HandleInitEncryptionRequest(InternalOperationRequestMessage request) => throw new NotSupportedException();
    }
}
