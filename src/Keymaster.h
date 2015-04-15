/*******************************************************************
 *  Keymaster.h - A YAML-based key/value store, accessible via 0MQ
 *
 *  Copyright (C) 2015 Associated Universities, Inc. Washington DC, USA.
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

#if !defined(_KEYMASTER_H_)
#define _KEYMASTER_H_

#include "yaml_util.h"
#include "Thread.h"
#include "TCondition.h"

#include <string>
#include <vector>
#include <exception>
#include <stdexcept>
#include <sstream>

#include <boost/shared_ptr.hpp>
#include <yaml-cpp/yaml.h>
#include <zmq.hpp>

#ifdef __GNUC__
#define __classmethod__  __PRETTY_FUNCTION__
#else
#define __classmethod__  __func__
#endif

class KeymasterServer
{
public:

    KeymasterServer(std::string configfile);
    ~KeymasterServer();

    void run();
    void terminate();

private:

    struct KmImpl;

    boost::shared_ptr<KeymasterServer::KmImpl> _impl;
};

class MatrixException : public std::runtime_error
{
public:

    MatrixException(std::string msg, std::string etype)
        : runtime_error(etype),
          _msg(msg)
    {
    }

    virtual ~MatrixException() throw ()
    {
    }


    virtual const char* what() const throw()
    {
        std::ostringstream msg;
        msg << std::runtime_error::what() << ": " << _msg;
        return msg.str().c_str();
    }

private:

    std::string _msg;
};

class KeymasterException : public MatrixException
{
public:
    KeymasterException(std::string msg) :
        MatrixException(msg, "Keymaster exception") {}
};

/**
 * \class KeymasterCallbackBase
 *
 * A virtual pure base callback class. A pointer to one of these is
 * given when subscribing for a keymaster value. When the value is
 * published, it is received by the Keymaster client object, which then
 * calls the provided pointer to an object of this type.
 *
 */

struct KeymasterCallbackBase
{
    void operator()(std::string key, YAML::Node val) {_call(key, val);}
    void exec(std::string key, YAML::Node val)       {_call(key, val);}
private:
    virtual void _call(std::string key, YAML::Node val) = 0;
};

/**
 * \class KeymasterMemberCB
 *
 * A subclassing of the base KeymasterCallbackBase callback class that allows
 * a using class to use one of its methods as the callback.
 *
 * example:
 *
 *     class foo()
 *     {
 *     public:
 *         void bar(YAML::Node n) {...}
 *
 *     private:
 *         KeymasterMemberCB<foo> my_cb;
 *         Keymaster *km;
 *     };
 *
 *     foo::foo()
 *       :
 *       my_cb(this, &foo::bar)
 *     {
 *         ...
 *         km = new Keymaster(km_url);
 *         km->subscribe("foo.bar.baz", my_cb);
 *     }
 *
 */

template <typename T>
class KeymasterMemberCB : public KeymasterCallbackBase
{
public:
    typedef void (T::*ActionMethod)(std::string, YAML::Node);

    KeymasterMemberCB(T *obj, ActionMethod cb) :
      _object(obj),
      _faction(cb)
    {
    }

private:
    ///
    /// Invoke a call to the user provided callback
    ///
    void _call(std::string key, YAML::Node val)
    {
        if (_object && _faction)
        {
            (_object->*_faction)(key, val);
        }
    }

    T  *_object;
    ActionMethod _faction;
};


class Keymaster
{
public:

    Keymaster(std::string keymaster_url);
    ~Keymaster();

    YAML::Node get(std::string key);
    template <typename T>
    T get_as(std::string key);

    void put(std::string key, YAML::Node n, bool create = false);
    template <typename T>
    void put(std::string key, T v, bool create = false);

    void del(std::string key);

    void subscribe(std::string key, KeymasterCallbackBase *f);
    void unsubscribe(std::string key);

    mxutils::yaml_result get_last_result() {return _r;}

private:

    void _subscriber_task();
    void _run();

    zmq::socket_t _km;
    mxutils::yaml_result _r;
    std::string _km_url;
    std::string _pipe_url;

    std::map<std::string, KeymasterCallbackBase *> _callbacks;
    Thread<Keymaster> _subscriber_thread;
    TCondition<bool> _subscriber_thread_ready;

};

template <typename T>
T Keymaster::get_as(std::string key)
{
    return get(key).as<T>();
}

template <typename T>
void Keymaster::put(std::string key, T v, bool create)
{
    YAML::Node n(v);
    put(key, n, create);
}

#endif
