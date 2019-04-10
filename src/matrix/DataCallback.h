/*******************************************************************
 *  DataCallback.h - Callbacks used by the DataSinks to process
 *  data. There is a base callback class DataCallbackBase. Also
 *  provided is a DataMemberCB, based on DataCallbackBase, that
 *  accepts a class object and a member function pointer to allow a
 *  class member function to handle the data. There is also a
 *  GenericBufferHandler. Derive a new class from DataCallbackBase to
 *  handle any custom situation.
 *
 *  Copyright (C) 2015, 2019 Associated Universities, Inc. Washington
 *  DC, USA.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Correspondence concerning GBT software should be addressed as follows:
 *  GBT Operations
 *  National Radio Astronomy Observatory
 *  P. O. Box 2
 *  Green Bank, WV 24944-0002 USA
 *
 *******************************************************************/

#if !defined(_DATA_CALLBACK_H_)
#define _DATA_CALLBACK_H_

#include <string>
#include <yaml-cpp/yaml.h>

namespace matrix
{

    /**********************************************************************
     * Callback classes
     **********************************************************************/

    /**
     * \class DataCallbackBase
     *
     * A virtual pure base callback class. A pointer to one of these is
     * given when subscribing for a keymaster value. When the value is
     * published, it is received by the Keymaster client object, which then
     * calls the provided pointer to an object of this type.
     *
     */

    struct DataCallbackBase
    {
        void operator()(std::string key, void *val, size_t sze) {_call(key, val, sze);}
        void exec(std::string key, void *val, size_t sze)       {_call(key, val, sze);}
    private:
        virtual void _call(std::string key, void *val, size_t szed) = 0;
    };

    /**
     * \class DataMemberCB
     *
     * A subclassing of the base DataCallbackBase callback class that allows
     * a using class to use one of its methods as the callback.
     *
     * example:
     *
     *     class foo()
     *     {
     *     public:
     *         void bar(std::string, void *, size_t) {...}
     *
     *     private:
     *         DataMemberCB<foo> my_cb;
     *         DataSink *km;
     *     };
     *
     *     foo::foo()
     *       :
     *       my_cb(this, &foo::bar)
     *     {
     *         ...
     *         ds = new DataSink(data_name, km_url, component);
     *         ds->subscribe("Data", &my_cb); // assumes a data source named "Data"
     *     }
     *
     */

    template <typename T>
    class DataMemberCB : public DataCallbackBase
    {
    public:
        typedef void (T::*ActionMethod)(std::string, void *, size_t);

        DataMemberCB(T *obj, ActionMethod cb) :
            _object(obj),
            _faction(cb)
        {
        }

    private:
        ///
        /// Invoke a call to the user provided callback
        ///
        void _call(std::string key, void *buf, size_t len)
        {
            if (_object && _faction)
            {
                (_object->*_faction)(key, buf, len);
            }
        }

        T  *_object;
        ActionMethod _faction;
    };

    /**
     * \class GenericBufferHandler
     *
     * Base class to a callback functor enables actions to be defined by a
     * user of the GenericDataConsumer component, or other application.
     *
     */

    struct GenericBufferHandler
    {
        void operator()(YAML::Node dd, matrix::GenericBuffer &buf)
        {
            _call(dd, buf);
        }

        void exec(YAML::Node dd, matrix::GenericBuffer &buf)
        {
            _call(dd, buf);
        }

    private:
        virtual void _call(YAML::Node, matrix::GenericBuffer &)
        {
        }
    };

}
#endif
