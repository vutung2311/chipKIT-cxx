
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __gnu_javax_net_ssl_provider_SSLEngineImpl$Mode__
#define __gnu_javax_net_ssl_provider_SSLEngineImpl$Mode__

#pragma interface

#include <java/lang/Enum.h>
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
              class SSLEngineImpl$Mode;
          }
        }
      }
    }
  }
}

class gnu::javax::net::ssl::provider::SSLEngineImpl$Mode : public ::java::lang::Enum
{

  SSLEngineImpl$Mode(::java::lang::String *, jint);
public:
  static JArray< ::gnu::javax::net::ssl::provider::SSLEngineImpl$Mode * > * values();
  static ::gnu::javax::net::ssl::provider::SSLEngineImpl$Mode * valueOf(::java::lang::String *);
  static ::gnu::javax::net::ssl::provider::SSLEngineImpl$Mode * SERVER;
  static ::gnu::javax::net::ssl::provider::SSLEngineImpl$Mode * CLIENT;
private:
  static JArray< ::gnu::javax::net::ssl::provider::SSLEngineImpl$Mode * > * ENUM$VALUES;
public:
  static ::java::lang::Class class$;
};

#endif // __gnu_javax_net_ssl_provider_SSLEngineImpl$Mode__