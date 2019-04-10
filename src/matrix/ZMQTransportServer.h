/*******************************************************************
 *  ZMQTransportServer.h - ZMQ-Based transport server
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

#if !defined(_ZMQTRANSPORT_SERVER_H_)
#define _ZMQTRANSPORT_SERVER_H_

#include "matrix/TransportServer.h"
#include <string>

namespace matrix
{

    class ZMQTransportServer : public matrix::TransportServer
    {
    public:
        ZMQTransportServer(std::string keymaster_url, std::string key);
        virtual ~ZMQTransportServer();

    private:
        bool _publish(std::string key, const void *data, size_t size_of_data);
        bool _publish(std::string key, std::string data);

        struct PubImpl;
        std::shared_ptr<PubImpl> _impl;

        friend class matrix::TransportServer;
        static matrix::TransportServer *factory(std::string, std::string);
    };
}

#endif
