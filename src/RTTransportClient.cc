/*******************************************************************
 *  RTDataInterface.cc - Implementation of a tsemfifo<T> transport.
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

#include "matrix/RTTransportClient.h"
#include "matrix/RTTransportServer.h"
#include "matrix/zmq_util.h"
#include "matrix/Keymaster.h"
#include "matrix/Mutex.h"
#include "matrix/ThreadLock.h"

#include <string>

using namespace std;
using namespace mxutils;

namespace matrix
{
    TransportClient *RTTransportClient::factory(string urn)
    {
        return new RTTransportClient(urn);
    }

    /**
     * RTTranportClient constructor.
     *
     * @param urn: The fully formed URN of the TransportServer. Used to
     * subscribe to its services.
     *
     */

    RTTransportClient::RTTransportClient(string urn)
        : TransportClient(urn)
    {
    }


    RTTransportClient::~RTTransportClient()
    {
        _unsubscribe(_key, _cb);
    }

    bool RTTransportClient::_connect(string)
    {
        bool rval = false;

        if (!_key.empty() && _cb != NULL)
        {
            rval = _subscribe(_key, _cb);
        }

        return rval;
    }

    bool RTTransportClient::_disconnect()
    {
        bool rval = false;

        if (!_key.empty() && _cb != NULL)
        {
            rval = _unsubscribe(_key, _cb);
        }

        return rval;
    }

    bool RTTransportClient::_subscribe(string key, DataCallbackBase *cb)
    {
        bool rval = false;
        std::map<std::string, RTTransportServer *>::iterator server;

        _key = key;
        _cb = cb;

        if ((server = RTTransportServer::_rttransports.find(_urn))
            != RTTransportServer::_rttransports.end())
        {
            // got the RTTransportServer...
            rval = server->second->_subscribe(key, cb);
        }

        return rval;
    }

    bool RTTransportClient::_unsubscribe(string key, DataCallbackBase *cb)
    {
        bool rval = false;
        std::map<std::string, RTTransportServer *>::iterator server;

        _key = key;
        _cb = cb;

        if ((server = RTTransportServer::_rttransports.find(_urn))
            != RTTransportServer::_rttransports.end())
        {
            // got the RTTransportServer...
            rval = server->second->_unsubscribe(key, cb);
        }

        return rval;
    }
}



