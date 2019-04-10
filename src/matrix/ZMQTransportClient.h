/*******************************************************************
 *  ZMQTransportClient.h - ZMQ-Based transport client
 *
 *  Copyright (C) 2015,2019 Associated Universities, Inc. Washington
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

#if !defined(_ZMQTRANSPORT_CLIENT_H_)
#define _ZMQTRANSPORT_CLIENT_H_

#include "matrix/TransportClient.h"
#include <string>

namespace matrix
{
    class ZMQTransportClient : public matrix::TransportClient
    {
    public:
        ZMQTransportClient(std::string urn);
        virtual ~ZMQTransportClient();

    private:
        bool _connect();
        bool _disconnect();
        bool _subscribe(std::string key, matrix::DataCallbackBase *cb);
        bool _unsubscribe(std::string key);

        struct Impl;
        std::shared_ptr<Impl> _impl;

        friend class matrix::TransportClient;
        static matrix::TransportClient *factory(std::string);
    };
}

#endif
