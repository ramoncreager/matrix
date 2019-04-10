/*******************************************************************
 *  ZMQTransportClient.cc - ZMQ-Based transport client implementation
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

#include "matrix/ZMQTransportClient.h"
#include "matrix/RTTransportClient.h"
#include "matrix/ZMQContext.h"
#include "matrix/zmq_util.h"

#include <iostream>

#define SUBSCRIBE   1
#define UNSUBSCRIBE 2
#define QUIT        3

using namespace std;
using namespace mxutils;

namespace matrix
{
    /**********************************************************************
     * Transport Client
     **********************************************************************/

    /**
     * Creates a ZMQTransportClient, returning a TransportClient pointer to it. This
     * is a static function that can be used by TransportClient::create() to
     * create this type of object based solely on the transports provided to
     * create(). The caller of TransportClient::create() thus needs no specific
     * knowledge of ZMQTransportClient.
     *
     * @param km_urn: the URN to the keymaster.
     *
     * @param key: The key to query the keymaster. This key should point to
     * a YAML node that contains information about the data source. One of
     * the sub-keys of this node must be a key 'Specified', which returns a
     * vector of transports required for this data source.
     *
     * @return A TransportClient * pointing to the created ZMQTransportClient.
     *
     */

    TransportClient *ZMQTransportClient::factory(string urn)
    {
        TransportClient *ds = new ZMQTransportClient(urn);
        return ds;
    }

    // Matrix will support ZMQTransportClient out of the box, so we can go ahead
    // and initialize the static TransportClient::factories here, pre-loading it
    // with the ZMQTransportClient supported transports. Library users who
    // subclass their own TransportClient will need to call
    // TransportClient::add_factory() somewhere in their code, prior to creating
    // their custom TransportClient objects, to add support for custom
    // transports.
    map<string, TransportClient::factory_sig> TransportClient::factories =

    {
        {"tcp",      &ZMQTransportClient::factory},
        {"ipc",      &ZMQTransportClient::factory},
        {"inproc",   &ZMQTransportClient::factory},
        {"rtinproc", &RTTransportClient::factory}
    };



    struct ZMQTransportClient::Impl
    {
        Impl() :
            _pipe_urn("inproc://" + gen_random_string(20)),
            _ctx(ZMQContext::Instance()->get_context()),
            _connected(false),
            _sub_thread(this, &ZMQTransportClient::Impl::sub_task),
            _task_ready(false)
        {}

        ~Impl()
        {
            disconnect();
        }

        bool connect(std::string url);
        bool disconnect();
        bool subscribe(std::string key, DataCallbackBase *cb);
        bool unsubscribe(std::string key);

        void sub_task();

        std::string _pipe_urn;
        std::string _data_urn;
        zmq::context_t &_ctx;
        bool _connected;
        Thread<ZMQTransportClient::Impl> _sub_thread;
        TCondition<bool> _task_ready;
        std::map<std::string, DataCallbackBase *> _subscribers;
    };

    bool ZMQTransportClient::Impl::connect(string urn)
    {
        _data_urn = urn;

        if (!_connected)
        {
            if (_sub_thread.start() == 0)
            {
                if (_task_ready.wait(true, 100000000) == false)
                {
                    cerr << Time::isoDateTime(Time::getUTC())
                         << " -- ZMQTransportClient for URN " << urn
                         << ": subscriber thread aborted." << endl;
                    return false;
                }

                _connected = true;
                return true;
            }
            else
            {
                cerr << Time::isoDateTime(Time::getUTC())
                     << " -- ZMQTransportClient for URN " << urn
                     << ": failure to start susbcriber thread."
                     << endl;
                return false;
            }
        }

        return false;
    }

    bool ZMQTransportClient::Impl::disconnect()
    {
        if (_connected)
        {
            zmq::socket_t pipe(_ctx, ZMQ_REQ);
            pipe.connect(_pipe_urn.c_str());
            z_send(pipe, QUIT, 0);
            int rval;
            z_recv(pipe, rval);
            _sub_thread.stop_without_cancel();
            _connected = false;
            return rval ? true : false;
        }

        return false;
    }

    bool ZMQTransportClient::Impl::subscribe(string key, DataCallbackBase *cb)
    {
        if (_connected)
        {
            zmq::socket_t pipe(_ctx, ZMQ_REQ);
            pipe.connect(_pipe_urn.c_str());
            z_send(pipe, SUBSCRIBE, ZMQ_SNDMORE);
            z_send(pipe, key, ZMQ_SNDMORE);
            z_send(pipe, cb, 0);
            int rval;
            z_recv(pipe, rval);
            return rval ? true : false;
        }

        return false;
    }

    bool ZMQTransportClient::Impl::unsubscribe(string key)
    {
        if (_connected)
        {
            zmq::socket_t pipe(_ctx, ZMQ_REQ);
            pipe.connect(_pipe_urn.c_str());
            z_send(pipe, UNSUBSCRIBE, ZMQ_SNDMORE);
            z_send(pipe, key, 0);
            int rval;
            z_recv(pipe, rval);
            return rval ? true : false;
        }

        return false;
    }

    void ZMQTransportClient::Impl::sub_task()
    {
        zmq::socket_t sub_sock(_ctx, ZMQ_SUB);
        zmq::socket_t pipe(_ctx, ZMQ_REP);
        vector<string>::const_iterator cvi;
        bool invalid_context = false;

        sub_sock.connect(_data_urn.c_str());
        pipe.bind(_pipe_urn.c_str());

        // we're going to poll. We will be waiting for subscription requests
        // (via 'pipe'), and for subscription data (via 'sub_sock').
        zmq::pollitem_t items [] =
            {
#if ZMQ_VERSION_MAJOR > 3
                { (void *)pipe, 0, ZMQ_POLLIN, 0 },
                { (void *)sub_sock, 0, ZMQ_POLLIN, 0 }
#else
                { pipe, 0, ZMQ_POLLIN, 0 },
                { sub_sock, 0, ZMQ_POLLIN, 0 }
#endif
            };

        _task_ready.signal(true);

        while (1)
        {
            try
            {
                zmq::poll(&items[0], 2, -1);

                if (items[0].revents & ZMQ_POLLIN) // the control pipe
                {
                    int msg;
                    z_recv(pipe, msg);

                    if (msg == SUBSCRIBE)
                    {
                        string key;
                        DataCallbackBase *f_ptr;
                        z_recv(pipe, key);
                        z_recv(pipe, f_ptr);

                        if (key.empty())
                        {
                            z_send(pipe, 0, 0);
                        }
                        else
                        {
                            _subscribers[key] = f_ptr;
                            sub_sock.setsockopt(ZMQ_SUBSCRIBE, key.c_str(), key.length());
                            z_send(pipe, 1, 0);
                        }
                    }
                    else if (msg == UNSUBSCRIBE)
                    {
                        string key;
                        z_recv(pipe, key);

                        if (key.empty())
                        {
                            z_send(pipe, 0, 0);
                        }
                        else
                        {
                            sub_sock.setsockopt(ZMQ_UNSUBSCRIBE, key.c_str(), key.length());

                            if (_subscribers.find(key) != _subscribers.end())
                            {
                                _subscribers.erase(key);
                            }

                            z_send(pipe, 1, 0);
                        }
                    }
                    else if (msg == QUIT)
                    {
                        z_send(pipe, 0, 0);
                        break;
                    }
                }

                // The subscribed data is handled here
                if (items[1].revents & ZMQ_POLLIN)
                {
                    string key;
                    zmq::message_t msg; // the data
                    int more;
                    size_t more_size = sizeof(more);
                    map<string, DataCallbackBase *>::const_iterator mci;
                    DataCallbackBase *f = NULL;

                    // get the key
                    z_recv(sub_sock, key);
                    mci = _subscribers.find(key);

                    // get callback registered to this key
                    if (mci != _subscribers.end())
                    {
                        f = mci->second;
                    }

                    // repeat for every possible frame containing
                    // data. The use case is for just one data frame;
                    // this will work for this, but if there are more
                    // than one data frames it will also work. Every
                    // subsequent data frame will call the same
                    // callback however.
                    sub_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);

                    while (more)
                    {
                        sub_sock.recv(&msg);

                        // execute only if we found a callback.
                        if (f)
                        {
                            f->exec(key, msg.data(), msg.size());
                        }

                        sub_sock.getsockopt(ZMQ_RCVMORE, &more, &more_size);
                    }
                }
            }
            catch (zmq::error_t &e)
            {
                string error = e.what();
                cerr << Time::isoDateTime(Time::getUTC())
                     << " -- ZMQTransportClient subscriber task: " << error << endl
                     << "URN for this task: " << _data_urn << endl;

                if (error.find("Context was terminated", 0) != string::npos)
                {
                    invalid_context = true;
                    break;
                }
            }
        }

        if (!invalid_context)
        {
            int zero = 0;
            pipe.setsockopt(ZMQ_LINGER, &zero, sizeof zero);
            pipe.close();
            zero = 0;
            sub_sock.setsockopt(ZMQ_LINGER, &zero, sizeof zero);
            sub_sock.close();
        }
    }


    ZMQTransportClient::ZMQTransportClient(string urn)
        : TransportClient(urn),
          _impl(new Impl())

    {
    }

    ZMQTransportClient::~ZMQTransportClient()
    {
        _impl->disconnect();
    }

    bool ZMQTransportClient::_connect()
    {
        return _impl->connect(_urn);
    }


    bool ZMQTransportClient::_disconnect()
    {
        return _impl->disconnect();
    }

    bool ZMQTransportClient::_subscribe(string key, DataCallbackBase *cb)
    {
        return _impl->subscribe(key, cb);
    }

    bool ZMQTransportClient::_unsubscribe(std::string key)
    {
        return _impl->unsubscribe(key);
    }

}
