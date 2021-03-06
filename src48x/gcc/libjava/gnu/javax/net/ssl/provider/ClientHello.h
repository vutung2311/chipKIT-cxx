
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_javax_net_ssl_provider_ClientHello__
#define __gnu_javax_net_ssl_provider_ClientHello__

#pragma interface

#include <java/lang/Object.h>
#include <gcj/array.h>

extern "Java"
{
  namespace gnu
  {
    namespace javax
    {
      namespace net
      {
        namespace ssl
        {
          namespace provider
          {
              class CipherSuiteList;
              class ClientHello;
              class CompressionMethodList;
              class ExtensionList;
              class ProtocolVersion;
              class Random;
          }
        }
      }
    }
  }
  namespace java
  {
    namespace nio
    {
        class ByteBuffer;
    }
  }
}

class gnu::javax::net::ssl::provider::ClientHello : public ::java::lang::Object
{

public:
  ClientHello(::java::nio::ByteBuffer *);
  virtual jint length();
  virtual ::gnu::javax::net::ssl::provider::ProtocolVersion * version();
  virtual ::gnu::javax::net::ssl::provider::Random * random();
  virtual JArray< jbyte > * sessionId();
  virtual ::gnu::javax::net::ssl::provider::CipherSuiteList * cipherSuites();
  virtual ::gnu::javax::net::ssl::provider::CompressionMethodList * compressionMethods();
  virtual jboolean hasExtensions();
  virtual ::gnu::javax::net::ssl::provider::ExtensionList * extensions();
  virtual jint extensionsLength();
public: // actually protected
  virtual jint getCipherSuitesOffset();
  virtual jint getCompressionMethodsOffset();
  virtual jint getExtensionsOffset();
public:
  virtual ::java::lang::String * toString();
  virtual ::java::lang::String * toString(::java::lang::String *);
public: // actually protected
  static const jint RANDOM_OFFSET = 2;
  static const jint SESSID_OFFSET = 34;
  static const jint SESSID_OFFSET2 = 35;
  ::java::nio::ByteBuffer * __attribute__((aligned(__alignof__( ::java::lang::Object)))) buffer;
  jboolean disableExtensions;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_javax_net_ssl_provider_ClientHello__
