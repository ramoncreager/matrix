/*******************************************************************
 *  TransportClient.cc - Implementation of base class transport client.
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


#include "matrix/TransportClient.h"
#include <boost/algorithm/string.hpp>

using namespace std;
using namespace matrix;


namespace matrix
{

    // This is the transport client repository. A map of map of
    // transport client shared_ptr. The first map is keyed by
    // component, and the value is another map, keyed by transport
    // name, whose value is a shared_ptr for that transport.
    TransportClient::client_map_t TransportClient::transports;
    Mutex TransportClient::factories_mutex;

    /**********************************************************************
     * Transport Client
     **********************************************************************/

    /**
     * Returns a shared_ptr to a TransportClient, creating a TransportClient
     * first if one does not exist for the keys given.
     *
     * @param urn: The fully formed URL to the data source, ready to use
     * to connect to that source.
     *
     * @return A shared_ptr to the transport object that was requested.
     *
     */

    shared_ptr<TransportClient> TransportClient::get_transport(string urn)
    {
        ThreadLock<decltype(transports)> l(transports);
        client_map_t::iterator cmi;

        l.lock();

        if ((cmi = transports.find(urn)) == transports.end())
        {
            transports[urn] = shared_ptr<TransportClient>(TransportClient::create(urn));
        }

        return transports[urn];
    }

    /**
     * Manages the static transport map. The lifetime of a TransportClient
     * is determined by how many clients it has. If it has no more
     * clients, it gets deleted. The transport map uses shared_ptr to make
     * this determination. Every time a DataSink disconnects from a
     * TransportClient it resets its shared_ptr, and calls this
     * function. This function then checks to see if the shared_ptr in the
     * map is unique, that is, there are no other shared_ptrs sharing this
     * object. If so, it resets the shared pointer, and removes the entry
     * in the map.
     *
     * @param urn: The fully formed URN that the TransportClient uses to
     * connect to the TransportServer. Also the key to the transport map.
     *
     */


    void TransportClient::release_transport(string urn)
    {
        ThreadLock<decltype(transports)> l(transports);
        client_map_t::iterator cmi;

        l.lock();

        if ((cmi = transports.find(urn)) != transports.end())
        {
            if (cmi->second.unique())
            {
                cmi->second.reset();
            }

            transports.erase(cmi);
        }
    }

    /**
     * Creates a TransportClient and returns it in a shared pointer.
     *
     * @param urn: The fully formed URN to the data sink.
     *
     * @return A std::shared_ptr<TransportClient> pointing to the
     * constructed TransportClient.
     *
     */

    shared_ptr<TransportClient> TransportClient::create(std::string urn)
    {
        ThreadLock<Mutex> l(factories_mutex);
        vector<string> parts;
        boost::split(parts, urn, boost::is_any_of(":"));

        if (!parts.empty())
        {
            factory_map_t::iterator i;

            l.lock();

            if ((i = factories.find(parts[0])) != factories.end())
            {
                factory_sig ff = i->second;
                return shared_ptr<TransportClient>(ff(urn));
            }

            throw TransportClient::CreationError("No known factory for " + parts[0]);
        }

        throw TransportClient::CreationError("Malformed URN " + urn);
    }


    TransportClient::TransportClient(string urn)
        : _urn(urn)
    {
    }

    TransportClient::~TransportClient()
    {
        disconnect();
    }

    bool TransportClient::_connect()
    {
        return false;
    }

    bool TransportClient::_disconnect()
    {
        return false;
    }

    bool TransportClient::_subscribe(string , DataCallbackBase *)
    {
        return false;
    }

    bool TransportClient::_unsubscribe(string )
    {
        return false;
    }
};
