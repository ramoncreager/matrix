/*******************************************************************
 *  TransportClient.h - Transport Client. Base class for the various
 *  Transport classes that will fill the role of transport clients.
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

#if !defined(_TRANSPORT_CLIENT_H_)
#define _TRANSPORT_CLIENT_H_

#include "matrix/Mutex.h"
#include "matrix/ThreadLock.h"
#include <string>
#include <vector>
#include <memory>
#include <map>

namespace matrix
{

/**********************************************************************
 * Transport Client
 **********************************************************************/

/**
 * \class TransportClient
 *
 * Represents a client to a named transport on a named component. The
 * client may be shared by multiple DataSinks, provided they also need
 * access to the same transport on the same component.
 *
 * The client connects by looking up the component and transport on
 * the keymaster, which should return a URL. This URL is then used to
 * connect to the transport.
 *
 * The TransportClient object's lifetime is controlled by use, via
 * std::shared_ptr<>. When a DataSink requests a TransportClient to
 * make the connection to the DataSource, a new TransportClient of the
 * correct type will be created, and stored in a static map of
 * std::shared_ptr<TransportClients>. A copy of the shared pointer is
 * then returned to the DataSink. The next DataSink to require this
 * TransportClient will request it and get another shared_ptr<> copy
 * to it. When a DataSink disconnects or calls its destructor, it
 * `reset()`s its shared_ptr copy, and then calls its static
 * `release_transport()` with the component name and data name
 * keys. That function checks to see if the stored shared_ptr is
 * unique, and if so, it resets it, terminating the TransportClient.
 *
 */
    class DataCallbackBase;


    class TransportClient
    {
    public:
        TransportClient(std::string urn);
        virtual ~TransportClient();

        bool connect(std::string urn = "");
        bool disconnect();
        bool subscribe(std::string key, DataCallbackBase *cb);
        bool unsubscribe(std::string key);

        // exception type for this class.
        class CreationError : public std::exception
        {
            std::string msg;

        public:

            CreationError(std::string err_msg)
            {
                msg = std::string("Cannot create TransportClient for transport: "
                                  + err_msg);
            }

            ~CreationError() throw()
            {
            }

            const char *what() const throw()
            {
                return msg.c_str();
            }
        };

        typedef TransportClient *(*factory_sig)(std::string);

        static void add_factory(std::vector<std::string>, factory_sig);
        static std::shared_ptr<TransportClient> get_transport(std::string urn);
        static void release_transport(std::string urn);

    protected:

        virtual bool _connect();
        virtual bool _disconnect();
        virtual bool _subscribe(std::string key, DataCallbackBase *cb);
        virtual bool _unsubscribe(std::string key);

        std::string _urn;

    private:

        matrix::Mutex _shared_lock;

        static std::shared_ptr<TransportClient> create(std::string urn);

        typedef std::map<std::string, factory_sig> factory_map_t;
        static factory_map_t factories;
        static matrix::Mutex factories_mutex;

        typedef matrix::Protected<std::map<std::string,
                                           std::shared_ptr<TransportClient>>> client_map_t;
        static client_map_t transports;
    };

    inline bool TransportClient::connect(std::string urn)
    {
        matrix::ThreadLock<matrix::Mutex> l(_shared_lock);
        l.lock();

        // connect to new urn?
        if (!urn.empty())
        {
            _urn = urn;
        }

        return _connect();
    }

    inline bool TransportClient::disconnect()
    {
        matrix::ThreadLock<matrix::Mutex> l(_shared_lock);
        l.lock();
        return _disconnect();
    }

    inline bool TransportClient::subscribe(std::string key, DataCallbackBase *cb)
    {
        matrix::ThreadLock<matrix::Mutex> l(_shared_lock);
        l.lock();
        return _subscribe(key, cb);
    }

    inline bool TransportClient::unsubscribe(std::string key)
    {
        matrix::ThreadLock<matrix::Mutex> l(_shared_lock);
        l.lock();
        return _unsubscribe(key);
    }

}

#endif
