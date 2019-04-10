/*******************************************************************
 *  RTTransportServer.h - Real Time INPROC transport server. Transfers
 *  data from server to client via a tsemfifo. This can be made to
 *  work very fast and satisfy real-time requirements, if necessary.
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

#if !defined(_RTTRANSPORT_SERVER_T_)
#define _RTTRANSPORT_SERVER_T_

#include "matrix/TransportServer.h"
#include <string>
#include <memory>

namespace matrix
{
    class DataCallbackBase;

    /**
     * \class RTTransportServer
     *
     * Provides a publishing service for any type T over a tsemfifo<T>. It
     * keeps a list of subscribers, and publishes the data to their
     * tsemfifo<T>s. This class is generally not called or used directly,
     * instead being set up behind the scenes by a DataSource<T>.
     *
     */

    class RTTransportServer : public matrix::TransportServer
    {
    public:

        RTTransportServer(std::string keymaster_url, std::string key);
        virtual ~RTTransportServer();

    private:

        // The RTTransportClient needs access to these two functions.
        friend class RTTransportClient;

        bool _subscribe(std::string key, DataCallbackBase *cb);
        bool _unsubscribe(std::string key, DataCallbackBase *cb);

        bool _publish(std::string key, const void *data, size_t size_of_data);
        bool _publish(std::string key, std::string data);

        struct Impl;
        std::shared_ptr<Impl> _impl;
        friend class matrix::TransportServer;
        static matrix::TransportServer *factory(std::string, std::string);
        static std::map<std::string, RTTransportServer *> _rttransports;
    };

}

#endif
