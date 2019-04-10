/*******************************************************************
 *  TransportServer.h - Transport Server. Base class for the various
 *  Transport classes that will fill the role of transport servers.
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

#if !defined(_TRANSPORT_SERVER_H_)
#define _TRANSPORT_SERVER_H_

#include "matrix/Mutex.h"
#include <string>
#include <vector>
#include <map>
#include <boost/algorithm/string.hpp>

namespace matrix
{
/**********************************************************************
 * Transport Server
 **********************************************************************/

 /**
  * \class TransportServer
  *
  * The Transport server takes data given to it by the DataSource and
  * transmits it to the recipient via some sort of transport
  * technology. The details of the underlying transport are private and
  * not known to the DataSource. Several implementations may be used:
  * ZMQ, shared memory, semafore FIFOS, etc.

  * Transports are specified in the YAML configuration using the
  * 'Transport' keyword inside of a component specification as follows:
  *
  *     nettask:
  *       Transports:
  *         A:
  *           Specified: [inproc, tcp]
  *           AsConfigured: [inproc://slizlieollwd, tcp://ajax.gb.nrao.edu:32553]
  *       ...
  *       ...
  *
  * Here the component, 'nettask', has 1 transport service, 'A'. It is
  * specified to support the 'inproc' and 'tcp' transports via the
  * 'Specified' keyword. Note that the value of this key is a vector;
  * thus more than one transport may be specified, but they must be
  * compatible. Here, 'inproc' and 'tcp' transports are both supported by
  * 0MQ, which also supports the 'ipc' transport. Thus if this list
  * contains any combination of the three a ZMQ based TransportServer
  * will be created. DataSource 'Log' will use this Transport because the
  * key 'Log' has as its value the key 'A', which specifies this
  * transport.
  *
  * When the transport is constructed it will turn the 'Specified' list
  * of transports into an 'AsConfigured' list of URLs that may be used by
  * clients to access this service. (Clients will transparently access
  * these via the TransportClient class & Keymaster.)
  *
  * Note here that in transforming the transport specification into URLs
  * the software generates random, temporary URLs. IPC and INPROC get a
  * string of random alphanumeric characters, and TCP gets turned into a
  * tcp url that contains the canonical host name and an ephemeral port
  * randomly chosen. This can be controlled by providing partial or full
  * URLs in place of the transport specifier:
  *
  *      nettask:
  *        Transports:
  *          A:
  *            Specified: [inproc://matrix.nettask.XXXXX, tcp://\*]
  *            AsConfigured: [inproc://matrix.nettask.a4sLv, tcp://ajax.gb.nrao.edu:52016]
  *
  * The 'AsSpecified' values will be identical for inproc and ipc, with
  * the exception that any 'XXXXX' values at the end of the URL will be
  * replaced by a string of random alphanumeric characters of the same
  * length; and for TCP the canonical host name will replace the '*' (as
  * it is needed by potential clients) and a randomly assigned port
  * number from the ephemeral port range will be provided, if no port is
  * given.
  *
  * NOTE ON EXTENDING THIS CLASS: Users who wish to create their own
  * derivations of TransportServer, as in the example above, need only create
  * the new class with a static 'factory' method that has the signature
  * `TransportServer *(*)(std::string, std::string)`. The static
  * 'TransportServer::factories' map must then be updated with this new
  * factory method, with the supported transport as keys. Note that the
  * transport names must be unique! If a standard name, or one defined
  * earlier, is reused, the prefious factory will be unreachable.
  *
  *     // 1) declare the new TransportServer class
  *     class MyTransportServer : public TransportServer
  *     {
  *     public:
  *         MyTransportServer(std::string, std::string);
  *         ~MyTransportServer();
  *         bool publish(std::string, const void *, size_t);
  *         bool publish(std::string, std::string);
  *
  *         static TransportServer *factory(std::string, std::string);
  *     };
  *
  *     // 2) implement the new class
  *            ...
  *
  *     // 3) Add the new factory
  *     vector<string> transports = {'my_transport'};
  *     TransportServer::add_factory(transports, TransportServer *(*)(string, string));
  *
  */

    class TransportServer
    {
    public:
        TransportServer(std::string keymaster_url, std::string key);
        virtual ~TransportServer();

        bool bind(std::vector<std::string> urns);
        bool publish(std::string key, const void *data, size_t size_of_data);
        bool publish(std::string key, std::string data);

        // exception type for this class.
        class CreationError : public std::exception
        {
            std::string msg;

        public:

            CreationError(std::string err_msg,
                          std::vector<std::string> t = std::vector<std::string>())
            {
                std::string transports = boost::algorithm::join(t, ", ");
                msg = std::string("Cannot create TransportServer for transports "
                                  + transports + ": " + err_msg);
            }

            ~CreationError() throw()
            {
            }

            const char *what() const throw()
            {
                return msg.c_str();
            }
        };

        typedef TransportServer *(*factory_sig)(std::string, std::string);

        static void add_factory(std::vector<std::string>, factory_sig);
        static std::shared_ptr<TransportServer> get_transport(std::string km_urn,
                                                              std::string component_name,
                                                              std::string transport_name);
        static void release_transport(std::string component_name, std::string transport_name);

    protected:

        virtual bool _bind(std::vector<std::string> urns);
        virtual bool _publish(std::string key, const void *data, size_t size_of_data);
        virtual bool _publish(std::string key, std::string data);

        bool _register_urn(std::vector<std::string> urns);
        bool _unregister_urn();

        std::string _km_url;
        std::string _transport_key;

    private:

        static std::shared_ptr<TransportServer> create(std::string km_urn, std::string transport_key);

        typedef std::map<std::string, factory_sig> factory_map_t;
        static factory_map_t factories;
        static matrix::Mutex factories_mutex;

        typedef std::map<std::string, std::shared_ptr<TransportServer> > transport_map_t;
        typedef matrix::Protected<std::map<std::string, transport_map_t> > component_map_t;
        static component_map_t transports;
    };

    inline bool TransportServer::bind(std::vector<std::string> urns)
    {
        return _bind(urns);
    }

    inline bool TransportServer::publish(std::string key, const void *data, size_t size_of_data)
    {
        return _publish(key, data, size_of_data);
    }

    inline bool TransportServer::publish(std::string key, std::string data)
    {
        return _publish(key, data);
    }
}

#endif
