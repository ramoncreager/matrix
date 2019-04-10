/*******************************************************************
 *  ZMQTransportServer.cc - Implementation of ZMQ-based transport server
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

#include "matrix/ZMQTransportServer.h"
#include "matrix/RTTransportServer.h"
#include "matrix/ZMQContext.h"
#include "matrix/zmq_util.h"
#include "matrix/matrix_util.h"
#include "matrix/netUtils.h"
#include "matrix/Time.h"
#include "matrix/Keymaster.h"
#include <boost/regex.hpp>

using namespace std;
using namespace std::placeholders;
using namespace mxutils;


namespace matrix
{

/**
 * Creates a ZMQTransportServer, returning a TransportServer pointer to it. This
 * is a static function that can be used by TransportServer::create() to
 * create this type of object based solely on the transports provided to
 * create(). The caller of TransportServer::create() thus needs no specific
 * knowledge of ZMQTransportServer.
 *
 * @param km_urn: the URN to the keymaster.
 *
 * @param key: The key to query the keymaster. This key should point to
 * a YAML node that contains information about the data source. One of
 * the sub-keys of this node must be a key 'Specified', which returns a
 * vector of transports required for this data source.
 *
 * @return A TransportServer * pointing to the created ZMQTransportServer.
 *
 */

    TransportServer *ZMQTransportServer::factory(string km_url, string key)
    {
        TransportServer *ds = new ZMQTransportServer(km_url, key);
        return ds;
    }

// Matrix will support ZMQTransportServer out of the box, so we can go ahead
// and initialize the static TransportServer::factories here, pre-loading it
// with the ZMQTransportServer supported transports. Library users who
// subclass their own TransportServer will need to call
// TransportServer::add_factory() somewhere in their code, prior to creating
// their custom TransportServer objects, to add support for custom
// transports.
    map<string, TransportServer::factory_sig> TransportServer::factories =

    {
        {"tcp",      &ZMQTransportServer::factory},
        {"ipc",      &ZMQTransportServer::factory},
        {"inproc",   &ZMQTransportServer::factory},
        {"rtinproc", &RTTransportServer::factory}
    };

/**
 * \class PubImpl is the private implementation of the ZMQTransportServer class.
 *
 */

    struct ZMQTransportServer::PubImpl
    {
        PubImpl(vector<string> urls);
        ~PubImpl();

        bool publish(string key, string data);
        bool publish(string key, void const *data, size_t sze);
        vector<string> get_urls();

        string _hostname;
        vector<string> _publish_service_urls;

        zmq::context_t &_ctx;
        zmq::socket_t _pub_skt;
    };

/**
 * Constructor of the implementation class of ZMQTransportServer.  The
 * implementation class handles all the details; the ZMQTransportServer class
 * merely provides the external interface.
 *
 * @param urns: The desired URNs, as a vector of strings. If
 * only the transport is given, ephemeral URLs will be generated.
 *
 */

    ZMQTransportServer::PubImpl::PubImpl(vector<string> urns)
        :
        _ctx(ZMQContext::Instance()->get_context()),
        _pub_skt(_ctx, ZMQ_PUB)

    {

        // process the urns.
        _publish_service_urls.clear();
        _publish_service_urls.resize(urns.size());
        transform(urns.begin(), urns.end(), _publish_service_urls.begin(), &process_zmq_urn);
        auto str_not_empty = std::bind(not_equal_to<string>(), _1, string());

        if (!all_of(_publish_service_urls.begin(), _publish_service_urls.end(), str_not_empty))
        {
            throw CreationError("Cannot use one or more of the following transports", urns);
        }

        string tcp_url;

        try
        {
            vector<string>::iterator urn;

            for (urn = _publish_service_urls.begin(); urn != _publish_service_urls.end(); ++urn)
            {
                boost::regex p_tcp("^tcp"), p_inproc("^inproc"), p_ipc("^ipc"), p_xs("X+$");
                boost::smatch result;

                // bind using tcp. If port is not given (port == 0), then use ephemeral port.
                if (boost::regex_search(*urn, result, p_tcp))
                {
                    int port_used;

                    if (!getCanonicalHostname(_hostname))
                    {

                        cerr << Time::isoDateTime(Time::getUTC())
                             << " -- ZMQTransportServer: Unable to obtain canonical hostname: "
                             << strerror(errno) << endl;
                        return;
                    }

                    // ephem port requested? ("tcp://*:XXXXX")
                    if (boost::regex_search(*urn, result, p_xs))
                    {
                        port_used = zmq_ephemeral_bind(_pub_skt, "tcp://*:*", 1000);
                    }
                    else
                    {
                        _pub_skt.bind(urn->c_str());
                        vector<string> parts;
                        boost::split(parts, *urn, boost::is_any_of(":"));
                        port_used = convert<int>(parts[2]);
                    }

                    // transmogrify the tcp urn to the actual urn needed for
                    // a client to access the service:
                    // 'tcp://<canonical_hostname>:<port>
                    ostringstream tcp_url;
                    tcp_url << "tcp://" << _hostname << ":" << port_used;
                    *urn = tcp_url.str();
                }

                // bind using IPC or INPROC:
                if (boost::regex_search(*urn, result, p_ipc)
                    || boost::regex_search(*urn, result, p_inproc))
                {
                    // these are already in a form the client can use.
                    _pub_skt.bind(urn->c_str());
                }
            }
        }
        catch (zmq::error_t &e)
        {
            cerr << Time::isoDateTime(Time::getUTC())
                 << " -- Error in publisher thread: " << e.what() << endl
                 << "Exiting publishing thread." << endl;
            return;
        }

    }

/**
 * Signals the server thread that we're done, waiting for it to exit
 * on its own.
 *
 */

    ZMQTransportServer::PubImpl::~PubImpl()

    {
        int zero = 0;
        _pub_skt.setsockopt(ZMQ_LINGER, &zero, sizeof zero);
        _pub_skt.close();
    }

/**
 * Returns the URLs bound to the services.
 *
 * @return A vector of two vectors of strings. The first element is the
 * state service URL set, the second is the publisher service URL set.
 *
 */

    vector<string> ZMQTransportServer::PubImpl::get_urls()
    {
        return _publish_service_urls;
    }

/**
 * Publishes the data, as represented by a string.
 *
 * @param key: The published key to the data.
 *
 * @param data: The data, contained in a std::string buffer
 *
 */

    bool ZMQTransportServer::PubImpl::publish(string key, string data)
    {
        return publish(key, data.data(), data.size());
    }

/**
 * Publishes the data, provided as a void * with a size parameter.
 *
 * @param key: The published key to the data.
 *
 * @param data: A void pointer to the buffer containing the data
 *
 * @param sze: The size of the data buffer
 *
 */

    bool ZMQTransportServer::PubImpl::publish(string key, void const *data, size_t sze)
    {
        bool rval = true;

        try
        {
            z_send(_pub_skt, key, ZMQ_SNDMORE, 0);
            z_send(_pub_skt, (const char *)data, sze, 0, 0);
        }
        catch (zmq::error_t &e)
        {
            cerr << Time::isoDateTime(Time::getUTC())
                 << " -- ZMQ exception in publisher: "
                 << e.what() << endl;
            rval = false;
        }

        return rval;
    }


    ZMQTransportServer::ZMQTransportServer(string keymaster_url, string key)
        : TransportServer(keymaster_url, key)
    {
        try
        {
            Keymaster km(_km_url);
            vector<string> urns;
            urns = km.get_as<vector<string> >(_transport_key + ".Specified");

            // will throw CreationError if it fails.
            _impl.reset(new PubImpl(urns));

            // register the AsConfigured urns:
            urns = _impl->get_urls();
            km.put(_transport_key + ".AsConfigured", urns, true);
        }
        catch (KeymasterException &e)
        {
            throw CreationError(e.what());
        }
    }

    ZMQTransportServer::~ZMQTransportServer()
    {
        // close pub socket.
        _impl.reset();

        try
        {
            Keymaster km(_km_url);
            km.del(_transport_key + ".AsConfigured");
        }
        catch (KeymasterException &e)
        {
            // Just making sure no exception is thrown from destructor. The
            // Keymaster client could throw if the KeymasterServer goes away
            // prior to this call.
        }
    }

    bool ZMQTransportServer::_publish(string key, const void *data, size_t size_of_data)
    {
        return _impl->publish(key, data, size_of_data);
    }

    bool ZMQTransportServer::_publish(string key, string data)
    {
        return _impl->publish(key, data);
    }
}
