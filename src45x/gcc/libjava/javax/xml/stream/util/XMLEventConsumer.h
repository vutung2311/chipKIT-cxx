
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __javax_xml_stream_util_XMLEventConsumer__
#define __javax_xml_stream_util_XMLEventConsumer__

#pragma interface

#include <java/lang/Object.h>
extern "Java"
{
  namespace javax
  {
    namespace xml
    {
      namespace stream
      {
        namespace events
        {
            class XMLEvent;
        }
        namespace util
        {
            class XMLEventConsumer;
        }
      }
    }
  }
}

class javax::xml::stream::util::XMLEventConsumer : public ::java::lang::Object
{

public:
  virtual void add(::javax::xml::stream::events::XMLEvent *) = 0;
  static ::java::lang::Class class$;
} __attribute__ ((java_interface));

#endif // __javax_xml_stream_util_XMLEventConsumer__