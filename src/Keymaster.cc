/*******************************************************************
 *  Keymaster.cc - Implements a YAML-based 0MQ key/value store
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

#include "matrix/Keymaster.h"
#include "matrix/ZMQContext.h"
#include "matrix/ThreadLock.h"
#include "matrix/tsemfifo.h"
#include "matrix/Thread.h"
#include "matrix/ThreadLock.h"
#include "matrix/zmq_util.h"
#include "matrix/netUtils.h"
#include "matrix/matrix_util.h"
#include "matrix/yaml_util.h"
#include "matrix/Time.h"
#include "matrix/ResourceLock.h"

#include <string>
#include <cstring>
#include <sstream>
#include <map>
#include <vector>
#include <list>
#include <iostream>
#include <sstream>
#include <exception>
#include <algorithm>
#include <memory>

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include <boost/circular_buffer.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/shared_ptr.hpp>

using namespace std;
using namespace mxutils;
using namespace matrix;

#define SUBSCRIBE   1
#define UNSUBSCRIBE 2
#define QUIT        3
#define KM_TIMEOUT  5000

struct substring_p
{
    substring_p(string subs)
        : _subs(subs)
    {
    }

    bool operator()(const string s)
    {
        return !(s.find(_subs) == string::npos);
    }

    string _subs;
};

struct same_transport_p
{
    same_transport_p(string url)
    {
        vector<string> parts;
        boost::split(parts, url, boost::is_any_of(":"));

        if (!parts.empty())
        {
            _transport = parts[0];
        }
    }

    bool operator()(const string s)
    {
        substring_p pred(_transport);
        return pred(s);
    }

    string _transport;
};

/**
 * KmImpl is the private implementation of the KeymasterServer class.
 *
 */

struct KeymasterServer::KmImpl
{
    KmImpl(YAML::Node config);
    ~KmImpl();

    struct data_package
    {
        std::string key;
        std::string val;
    };

    void server_task();
    void state_manager_task();
    void heartbeat_task();
    bool load_config_file(string filename);
    bool publish(std::string key, bool block = false);
    void run();
    void terminate();

    void setup_urls();
    bool using_tcp();
    void bind_server(zmq::socket_t &server_sock, vector<string> &urls);

    Thread<KmImpl> _server_thread;
    Thread<KmImpl> _state_manager_thread;
    Thread<KmImpl> _heartbeat_thread;
    TCondition<bool> _server_thread_ready;
    TCondition<bool> _state_manager_thread_ready;

    int _state_port_used;
    int _pub_port_used;
    bool _state_manager_done;
    tsemfifo<data_package> _data_queue;
    Mutex _cache_lock;
    std::string _state_task_url;
    std::string _hostname;
    bool _state_task_quit;
    bool _running;
    int _clone_interval;

    // The service URLs. Each interface (STATE or PUBLISH) may have
    // multiple URLs (tcp, inproc, ipc) for possible future
    // use. Subscribers will need the publisher service urls.
    std::vector<std::string> _state_service_urls;
    std::vector<std::string> _publish_service_urls;

    list<YAML::Node> _root_node;    //<? THE keymaster node
};

/**
 * Constructor of the implementation class of the KeymasterServer.  The
 * implementation class handles all the details; the KeymasterServer class
 * merely provides the external interface, and loads the configuration file.
 *
 * @param config: The YAML configuration file. Specifies the Keymaster
 * server configuration and also the initial state of the Keymaster's
 * data store.
 *
 * KeymasterServer supports a 0MQ REQ/REP service which allows the
 * Keymaster client to query for values, alter values, create values and
 * delete values. KeymasterServer also publishes values that have been
 * changed by any client to all interested subscribers. These URLs are
 * not well known however. The Keymaster client class obtains these via
 * request, so that it can then subscribe.
 *
 */

KeymasterServer::KmImpl::KmImpl(YAML::Node config)
:
    _server_thread(this, &KeymasterServer::KmImpl::server_task),
    _state_manager_thread(this, &KeymasterServer::KmImpl::state_manager_task),
    _heartbeat_thread(this, &KeymasterServer::KmImpl::heartbeat_task),
    _server_thread_ready(false),
    _state_manager_thread_ready(false),
    _data_queue(1000),
    _state_task_url(string("inproc://") + gen_random_string(20)),
    _state_task_quit(true),
    _running(true),
    _clone_interval(0)
{
    _root_node.push_front(YAML::Clone(config));
    setup_urls();

    if (using_tcp() && !getCanonicalHostname(_hostname))
    {
        // fallback to non-canonical host name, and leave a warning

        char hn[256];

        if (gethostname(hn, 255) != 0)
        {
            // total failure.
            ostringstream msg;
            msg << "KeymasterServer: TCP transport requested, "
                << "but unable to obtain hostname:"
                << strerror(errno) << ends;
            throw(runtime_error(msg.str()));
        }

        cerr << "WARNING: unable to obtain canonical hostname. Using '"
             << hn << "' instead" << endl;
        _hostname = hn;
    }
}

/**
 * Destructor. Signals the server thread that we're done, waiting for it to exit
 * on its own.
 *
 */

KeymasterServer::KmImpl::~KmImpl()

{
    vector<string>::const_iterator i;

    terminate();

    i = find_if(_publish_service_urls.begin(),
                _publish_service_urls.end(),
                substring_p("ipc"));

    if (i != _publish_service_urls.end())
    {
        unlink(i->c_str());
    }
}

/**
 * Starts the keymaster threads.
 *
 */

void KeymasterServer::KmImpl::run()
{
    if (!_server_thread.running())
    {
        if (_server_thread.start() != 0)
        {
            throw(runtime_error(
                      string("KeymasterServer: unable to start publishing thread")));
        }
    }

    if (_server_thread_ready.wait(true, 1000000) != true)
    {
        throw(runtime_error(
                  string("KeymasterServer: timed out waiting for publishing thread")));
    }

    // Make sure this is run AFTER the _server_thread (publisher)
    // because it will put publishing information in the _root_node. All
    // access to the _root_node should be via the _state_manager_thread
    // because the _root_node is not thread-safe.
    if (!_state_manager_thread.running())
    {
        if (_state_manager_thread.start() != 0
            || !_state_manager_thread_ready.wait(true, 1000000))
        {
            throw(runtime_error(
                      string("KeymasterServer: unable to start request thread")));
        }
    }

    if (!_heartbeat_thread.running())
    {
        cout << "Starting the heartbeat thread" << endl;

        if (_heartbeat_thread.start() != 0)
        {
            cout << "Heartbeat thread did not start" << endl;

            throw(runtime_error(
                      string("KeymasterServer: unable to start the heartbeat thread")));
        }
    }
    // Now that we're running, publish everything, so that any clients
    // already subscribed may be updated.
    publish("Root", true);
}

/**
 * Terminates the Keymaster server threads cleanly.
 *
 */

void KeymasterServer::KmImpl::terminate()
{
    _running = false;

    if (_state_manager_thread.running())
    {
        zmq::socket_t sock(ZMQContext::Instance()->get_context(), ZMQ_PAIR);
        sock.connect(_state_task_url.c_str());
        z_send(sock, _state_task_quit, 0);
        sock.close();
        _state_manager_thread.stop_without_cancel();
    }

    if (_server_thread.running())
    {
        _data_queue.release();
        _server_thread.stop_without_cancel();
    }

    if (_heartbeat_thread.running())
    {
        _heartbeat_thread.stop_without_cancel();
    }
}

/**
 * Sets up and validates all the urls. The URLs are retrieved from the
 * root node.
 *
 * @return bool, false if there is a problem, true otherwise.
 *
 */

void KeymasterServer::KmImpl::setup_urls()
{
    vector<string>::const_iterator cvi;
    vector<string> urls =
        _root_node.front()["Keymaster"]["URLS"]["Initial"].as<vector<string> >();
    // TODO: make key optional
    _clone_interval = _root_node.front()["Keymaster"]["clone_interval"].as<int>();

    for (cvi = urls.begin(); cvi != urls.end(); ++cvi)
    {
        string lc(cvi->size(), 0);
        transform(cvi->begin(), cvi->end(), lc.begin(), ::tolower);
        _state_service_urls.push_back(lc);

        if (lc.find("tcp") != string::npos)
        {
            // We want the publisher to be the port used for the
            // Keymaster queries, +1.
            vector<string> parts;
            boost::split(parts, lc, boost::is_any_of(":"));
            auto port = convert<int>(parts.back()) + 1;
            _publish_service_urls.push_back(string("tcp://*:" + to_string(port)));
        }
        else if (lc.find("ipc") != string::npos)
        {
            string s = lc + ".publisher";
            _publish_service_urls.push_back(s);
        }
        else if (lc.find("inproc") != string::npos)
        {
            string s = lc + ".publisher";
            _publish_service_urls.push_back(s);
        }
        else
        {
            ostringstream msg;
            msg << "KeymasterServer: Unrecognized URL: " << *cvi << ends;
            throw(runtime_error(msg.str()));
        }
    }

    // If an inproc URL is not specified, generate one just for the
    // state task, so that the heartbeat_task can use it.
    if (!any_of(_state_service_urls.begin(), _state_service_urls.end(),
                [](string i){return i.find("inproc") != string::npos;}))
    {
        _state_service_urls.push_back(string("inproc://") + gen_random_string(20));
    }
}

/**
 * Checks to see if TCP transport is required, by examining the state
 * service URLs (publisher service URLs will mirror these)
 *
 * @return bool, true if TCP is required, false otherwise.
 *
 */

bool KeymasterServer::KmImpl::using_tcp()
{
    bool rval = false;
    vector<string>::const_iterator cvi;

    if (find_if(_state_service_urls.begin(),
                _state_service_urls.end(), substring_p("tcp"))
        != _state_service_urls.end())
    {
        rval = true;
    }

    return rval;
}

/**
 * Binds a given server socket to the provided urls, optionally using
 * transient addresses.
 *
 * @param server_sock: The server. May be a ZMQ_PUB or ZMQ_REP.
 *
 * @param urls: A list of URLs to bind to. These are presumed to be
 * valid. IPC & inproc urls given with the `transient` flag will have a
 * random string appended to them to avoid collisions. These URLs will
 * then all be replaced by the one actually used, except the TCP one (if
 * included) being completely replaced in all cases by the string
 * "tcp://<hostname>:<port>, whether the port is transient or not.
 *
 * @param transient: A flag, if true the TCP and IPC urls (if provided)
 * will be bound to a transient port (TCP) or to a temporary pipe
 * (IPC). If false, the URLs are used as is.
 *
 */

void KeymasterServer::KmImpl::bind_server(zmq::socket_t &server_sock, vector<string> &urls)
{
    vector<string>::iterator cvi;

    // bind to all publisher URLs
    for (cvi = urls.begin(); cvi != urls.end(); ++cvi)
    {
        if (cvi->find("tcp") != string::npos)
        {
            // If it's TCP we bind to tcp://*:<port>, but we also
            // create here the url which may be used by a client to find
            // this service. It will later be posted on the Keymaster.
            vector<string> fields;
            boost::split(fields, *cvi, boost::is_any_of(":"));
            server_sock.bind(cvi->c_str());
            ostringstream tcp_url;
            tcp_url << "tcp://" << _hostname << ":" << fields.back();
            *cvi = tcp_url.str();
            cout << "INFO: Keymaster server at " << *cvi << endl;
        }
        else
        {
            // If it's IPC or inprock, use the given URL. It will already have
            // '.publisher appended if this is the publisher.
            server_sock.bind(cvi->c_str());
            cout << "INFO: Keymaster server at " << *cvi << endl;
        }
    }
}

/**
 * This is the publisher server task.  It sits on the queue waiting
 * for something to be published until it gets a "QUIT" message.  This
 * consists of releasing the state queue.
 *
 */

void KeymasterServer::KmImpl::server_task()

{
    data_package dp;
    zmq::socket_t data_publisher(ZMQContext::Instance()->get_context(), ZMQ_PUB);
    string tcp_url;

    try
    {
        bind_server(data_publisher, _publish_service_urls);
    }
    catch (zmq::error_t &e)
    {
        cerr << Time::isoDateTime(Time::getUTC())
             <<  " -- Error in KeymasterServer publisher thread: " << e.what() << endl
             << "Exiting KeymasterServer publishing thread." << endl;
        return;
    }

    // Allow constructor to move on
    _server_thread_ready.signal(true);
    // TBF: Give clients some time (0.5 sec) to reconnect. As a
    // publisher, we don't care if there are any clients. But if there
    // are, and they need to recover from a restart of the keymaster,
    // this can give them the time to do it. Perhaps a heartbeat will
    // allow more secure recovery.
    Time::thread_delay(2000000000);

    while (_data_queue.get(dp))
    {
        try
        {
            z_send(data_publisher, dp.key, ZMQ_SNDMORE);
            z_send(data_publisher, dp.val, 0);
        }
        catch (zmq::error_t &e)
        {
            cerr << Time::isoDateTime(Time::getUTC())
                 << " -- ZMQ exception in publisher thread: "
                 << e.what() << endl;
        }
    }

    // Done. Clean up.
    int zero = 0;
    data_publisher.setsockopt(ZMQ_LINGER, &zero, sizeof zero);
    data_publisher.close();
}

/**
 * This 0MQ server task implements a REPLY socket (of a REQ/REP pair),
 * whose function is to receive requests for cached data.  This is
 * useful for late joiners, especially for keys that don't change
 * frequently.
 *
 */

void KeymasterServer::KmImpl::state_manager_task()

{
    zmq::context_t &ctx = ZMQContext::Instance()->get_context();
    zmq::socket_t state_sock(ctx, ZMQ_REP);
    zmq::socket_t pipe(ctx, ZMQ_PAIR);  // mostly to tell this task to go away
    unsigned int put_counter(0), clone_interval(1000);

    try
    {
        // control pipe
        pipe.bind(_state_task_url.c_str());
    }
    catch (zmq::error_t &e)
    {
        cerr << Time::isoDateTime(Time::getUTC())
             << " -- Error in state manager thread: " << e.what() << endl
             << "Exiting state thread." << endl
             << "_state_task_url = " << _state_task_url << endl;
        return;
    }

    try
    {
        // bind to all state server URLs
        bind_server(state_sock, _state_service_urls);
        put_yaml_val(
            _root_node.front(), "KeymasterServer.URLS", _state_service_urls, true);
        publish("KeymasterServer.URLS");
    }
    catch (zmq::error_t &e)
    {
        cerr << Time::isoDateTime(Time::getUTC())
             << " -- Error in state manager thread: " << e.what() << endl
             << "Exiting state thread." << endl
             << "_state_service_urls = " << endl;
        output_vector(_state_service_urls, cerr);
        cerr << endl;

        return;
    }


    yaml_result rs = put_yaml_val(
        _root_node.front(),
        "Keymaster.URLS.AsConfigured.State", _state_service_urls, true);
    yaml_result rp = put_yaml_val(
        _root_node.front(),
        "Keymaster.URLS.AsConfigured.Pub", _publish_service_urls, true);
    ostringstream state;
    ostringstream pub;
    mxutils::output_vector(_state_service_urls, state);
    mxutils::output_vector(_publish_service_urls, pub);
    publish("Keymaster.URLS.AsConfigured.State", true);
    publish("Keymaster.URLS.AsConfigured.Pub", true);

    if (! (rs.result && rp.result))
    {
        cerr << Time::isoDateTime(Time::getUTC())
             << " -- Error storing configured URLs into the root node." << endl
             << "Exiting state thread." << endl;
        return;
    }

    zmq::pollitem_t items [] =
        {
#if ZMQ_VERSION_MAJOR > 3
            { (void *)pipe, 0, ZMQ_POLLIN, 0 },
            { (void *)state_sock, 0, ZMQ_POLLIN, 0 }
#else
            { pipe, 0, ZMQ_POLLIN, 0 },
            { state_sock, 0, ZMQ_POLLIN, 0 }
#endif
        };

    _state_manager_thread_ready.signal(true); // allow 'run()' to move
                                              // on.

    if (_clone_interval != 0)
    {
        clone_interval = _clone_interval;
    }
    while (1)
    {
        try
        {
            zmq::poll(&items [0], 2, -1);

            if (items[0].revents & ZMQ_POLLIN)
            {
                bool quit;
                z_recv(pipe, quit);

                if (_state_task_quit == quit)
                {
                    break;
                }
            }

            if (items[1].revents & ZMQ_POLLIN)
            {
                string key;
                vector<string> frame;

                z_recv(state_sock, key);

                // Determine the request.  Currently requests may
                // be either a "ping" (just to see if the service
                // is alive); a "LIST" to get information on all
                // samplers and parameters published.  If none of
                // the above, the request is assumed to be a key
                // to a published item.

                if (key.size() == 4 && key == "ping")
                {
                    // read any remaining parts
                    z_recv_multipart(state_sock, frame);

                    // reply with something
                    z_send(state_sock, "I'm not dead yet!", 0);
                }
                /////////////////// G E T ///////////////////
                else if (key.size() == 3 && key == "GET")
                {
                    z_recv_multipart(state_sock, frame);

                    if (!frame.empty())
                    {
                        string keychain = frame[0];
                        ostringstream rval;

                        if (keychain == "Root")
                        {
                            keychain = "";
                        }

                        yaml_result r = get_yaml_node(_root_node.front(), keychain);
                        rval << r;
                        z_send(state_sock, rval.str(), 0);
                    }
                    else
                    {
                        string msg("ERROR: Keychain expected, but not received!");
                        z_send(state_sock, msg, 0);
                    }
                }
                /////////////////// P U T ///////////////////
                else if (key.size() == 3 && key == "PUT")
                {
                    z_recv_multipart(state_sock, frame);

                    if (frame.size() > 1)
                    {
                        string keychain = frame[0];

                        if (keychain == "Root")
                        {
                            keychain = "";
                        }

                        string yaml_string = frame[1];
                        bool create = false;

                        if (frame.size() > 2 && frame[2] == "create")
                        {
                            create = true;
                        }

                        yaml_result r;
                        ostringstream rval;
                        YAML::Node n = YAML::Load(yaml_string);

                        r = put_yaml_node(_root_node.front(), keychain, n, create);

                        if (r.result)
                        {
                            publish(keychain);
                        }

                        rval << r;
                        z_send(state_sock, rval.str(), 0);

                        // What follows is here to prevent undue
                        // memory usage. yaml-cpp has an unbounded
                        // memory use problem (see
                        // https://github.com/jbeder/yaml-cpp/issues/232
                        // ). Cloning the node and dropping the
                        // original one flushes out the memory. We do
                        // it at every nth put--as specified by
                        // `clone_interval`--where n is large enough
                        // not to impact performance but small enough
                        // to ensure no runaway memory use.

                        if ((++put_counter % clone_interval) == 0)
                        {
                            _root_node.push_front(YAML::Clone(_root_node.front()));
                            _root_node.pop_back();
                        }
                    }
                    else
                    {
                        string msg("ERROR: Keychain and value expected, but not received!");
                        z_send(state_sock, msg, 0);
                    }
                }
                /////////////////// D E L ///////////////////
                else if (key.size() == 3 && key == "DEL")
                {
                    z_recv_multipart(state_sock, frame);

                    if (!frame.empty())
                    {
                        string keychain = frame[0];
                        yaml_result r = delete_yaml_node(_root_node.front(), keychain);
                        ostringstream rval;
                        rval << r;
                        z_send(state_sock, rval.str(), 0);

                        if (r.result)
                        {
                            publish(keychain, true);
                        }
                    }
                    else
                    {
                        string msg("ERROR: Keychain expected, but not received!");
                        z_send(state_sock, msg, 0);
                    }
                }
                else
                {
                    z_recv_multipart(state_sock, frame);
                    ostringstream msg;
                    msg << "Unknown request '" << key;
                    z_send(state_sock, msg.str(), 0);
                }
            }
        }
        catch (zmq::error_t &e)
        {
            cerr << Time::isoDateTime(Time::getUTC())
                 << " -- State manager task, main loop: " << e.what() << endl;
        }
    }

    int zero = 0;
    state_sock.setsockopt(ZMQ_LINGER, &zero, sizeof zero);
    state_sock.close();
}

/**
 * KeymasterServer::KmImpl::heartbeat_task() will increment an uptime
 * counter that will also serve as the Keymaster's heartbeat for any
 * client that subscribes to it. This will give clients the means to
 * detect if the Keymaster server goes away.
 *
 */

void KeymasterServer::KmImpl::heartbeat_task()
{
    zmq::context_t &ctx = ZMQContext::Instance()->get_context();
    zmq::socket_t sock(ctx, ZMQ_REQ);
    string response;
    string cmd("PUT"), key("Keymaster.heartbeat");
    Time::Time_t one_sec(1000000000L);
    Time::Time_t wake_time = Time::getUTC() + one_sec;

    auto url = get_most_local(_state_service_urls);

    if (not url.empty())
    {
        sock.connect(url.c_str());

        while (_running)
        {
            Time::thread_sleep_until(wake_time);
            Time::Time_t t = wake_time;
            wake_time += one_sec;
            string val = to_string(t);
            string flag("create");

            try
            {
                z_send(sock, cmd, ZMQ_SNDMORE, KM_TIMEOUT);
                z_send(sock, key, ZMQ_SNDMORE, KM_TIMEOUT);
                z_send(sock, val, ZMQ_SNDMORE, KM_TIMEOUT);
                z_send(sock, flag, 0, KM_TIMEOUT);
                z_recv(sock, response, KM_TIMEOUT);
            }
            catch (MatrixException &e)
            {
                cerr << Time::isoDateTime(Time::getUTC())
                     << " -- Keymaster Server Heartbeat task: "
                     << "url=" << url << endl
                     << "cmd=" << cmd << " key=" << key << " val=" << val
                     << " flag=" << flag << endl << e.what() << endl;
            }
        }
    }
}

/**
 * Publish data. Whenever a node is modified, we need to of
 * course publish that node. But we also need to publish
 * upstream nodes, because someone may have subscribed to them,
 * and a change to this key means a change to all upstream
 * keys. So if the node is "foo.bar.baz", we publish "foo",
 * "foo.bar", and "foo.bar.baz"
 *
 * @param key: the data key
 *
 * @return true if the data was succesfuly placed in the publication
 * queue, false otherwise.
 *
 */

bool KeymasterServer::KmImpl::publish(std::string key, bool block)
{
    bool rval = true;

    // Publish "Root" if there is no key
    try
    {
        data_package dp = {key, ""};
        YAML::Node node = _root_node.front();

        if (dp.key.empty())
        {
            ostringstream yr;
            yr << node;
            dp.key = "Root";
            dp.val = yr.str();
        }
        else
        {
            vector<string> keys;
            boost::split(keys, dp.key, boost::is_any_of("."));

            // Publish with keys
            for (size_t i = 1; i < keys.size() + 1; ++i)
            {
                string key = boost::algorithm::join(vector<string>(keys.begin(), keys.begin() + i), ".");
                yaml_result r = get_yaml_node(node, key);

                if (r.result == true)
                {
                    ostringstream yr;
                    // we just need the node that goes with the key.
                    yr << r.node;
                    dp.key = key;
                    dp.val = yr.str();

                    if (block)
                    {
                        _data_queue.put(dp);
                    }
                    else
                    {
                        rval = rval and _data_queue.try_put(dp);
                    }
                }
            }
        }
    }
    catch (YAML::Exception &e)
    {
        cerr << Time::isoDateTime(Time::getUTC())
             << " -- YAML exception in publish: " << e.what() << endl;
        return false;
    }

    return rval;
}

/**
 * \class KeymasterServer
 *
 * This is a class that provides a 0MQ publishing facility. KeymasterServer
 * both publishes data as a ZMQ PUB, but also provides a REQ/REP service.
 *
 * The KeymasterServer should be one of the first objects created in the
 * main program entry point:
 *
 *     int main(...)
 *     {
 *         KeymasterServer *kms;
 *         kms = new KeymasterServer("config.yaml");
 *
 *         if (!kms->run())
 *         {
 *             return 1;
 *         }
 *
 *         ...
 *
 *         return 0;
 *     }
 *
 * The Keymaster now is ready to serve clients. The initial state of the
 * KeymasterServer is set by the file 'config.yaml'.
 *
 */

/**
 * KeymasterServer constructor.  Constructs a KeymasterServer from a
 * valid YAML encoded configuration file.
 *
 * @param configfile: A YAML configuration file which sets the
 * Keymaster's data store to an initial state.
 *
 */

KeymasterServer::KeymasterServer(std::string configfile)
{
    YAML::Node config;

    try
    {
        config = YAML::LoadFile(configfile);
    }
    catch (YAML::BadFile &e)
    {
        ostringstream msg;
        msg << "KeymasterServer: Could not open config file "
            << configfile;
        throw(runtime_error(msg.str()));
    }

    _impl.reset(new KeymasterServer::KmImpl(config));
}

/**
 * KeymasterServer constructor.  Constructs a KeymasterServer from a
 * valid YAML::Node.
 *
 * @param n: A YAML::Node containing the initial configuration for the
 * Keymaster server.
 *
 */

KeymasterServer::KeymasterServer(YAML::Node n)
{
    _impl.reset(new KeymasterServer::KmImpl(n));
}

/**
 * Destructor, signals the server thread that we're done, waiting for it
 * to exit on its own.
 *
 */

KeymasterServer::~KeymasterServer()

{
    _impl.reset();
}

/**
 * Starts the KeymasterServer running.
 *
 * @return true if the threads were started, false otherwise.
 */

void KeymasterServer::run()
{
    _impl->run();
}

/**
 * Terminates the KeymasterServer threads. This function will block
 * until the threads are done.
 *
 */

void KeymasterServer::terminate()
{
    _impl->terminate();
}

/****************************************************************//**
 * \class Keymaster
 *
 * The Keymaster class is a client to the keymaster service,
 * encapsulating and hiding the details of the connection.
 *
 * Example usage:
 *
 *      Keymaster km("inproc://matrix.keymaster");
 *      string key = "foo.Transports";
 *      vector<string> transports = km.get_as<vector<string> >(key)
 *
 *******************************************************************/

/**
 * The Keymaster client constructor makes a connection to the specified
 * Keymaster service URL. Will throw a KeymasterException if it is
 * unable to make the connection.
 *
 * @param keymaster_url: The url for the keymaster service
 *
 */

Keymaster::Keymaster(string keymaster_url, bool /* shared */)
    :
    _km_url(keymaster_url),
    _pipe_url(string("inproc://") + gen_random_string(20)),
    _subscriber_thread(this, &Keymaster::_subscriber_task),
    _subscriber_thread_ready(false),
    _put_thread(this, &Keymaster::_put_task),
    _put_thread_ready(false),
    _put_thread_run(false)
{
}

/**
 * The destructor will close the connection to the keymaster service.
 *
 */

Keymaster::~Keymaster()
{
    int zero = 0;

    if (_subscriber_thread.running())
    {
        zmq::socket_t ctrl(ZMQContext::Instance()->get_context(), ZMQ_REQ);
        ctrl.connect(_pipe_url.c_str());
        z_send(ctrl, QUIT, 0);
        int rval;
        z_recv(ctrl, rval);
        _subscriber_thread.stop_without_cancel();
    }

    if (_km_.get())
    {
        ThreadLock<Mutex> lck(_shared_lock);
        _km_->setsockopt(ZMQ_LINGER, &zero, sizeof zero);
        _km_->close();
    }

    if (_put_thread.running())
    {
        _put_thread_run = false;
        _put_thread.stop_without_cancel();
    }
}

/**
 * RPC call to the Keymaster. Makes this call atomic, so that multiple
 * threads may use one Keymaster client without interrupting the
 * REQ/REPL pairs.
 *
 * @param cmd: One of the commands recognized by the Keymaster Server:
 * GET, PUT, DEL.
 *
 * @param key: A key to a YAML node. In form "key1.key2.key3" which
 * represents a hierarchy of YAML nodes on the Keymaster.
 *
 * @param val: A new value for the node pointet to by 'key'.
 *
 * @param flag: A flag regulating the operation of PUT: if 'create'
 * PUT will create a new node at the specified key if one doesn't
 * already exist. If "", it will return an error if the key does not exist.
 *
 */

yaml_result Keymaster::_call_keymaster(string cmd, string key, string val, string flag)
{
    string response;
    yaml_result yr;
    int pre_cancel_state;
    ThreadLock<Mutex> lck(_shared_lock);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &pre_cancel_state);
    ResourceLock canceler([pre_cancel_state]()
                          { pthread_setcancelstate(pre_cancel_state, nullptr); });
    ostringstream msg;

    try
    {
        msg << "Keymaster: Failed to " << cmd << " key '"
            << key << " from Keymaster at " << _km_url << " ";

        lck.lock();
        shared_ptr<zmq::socket_t> km = _keymaster_socket();
        // always send a command
        z_send(*km, cmd, ZMQ_SNDMORE, KM_TIMEOUT);
        // always send a key
        z_send(*km, key, val.empty() ? 0: ZMQ_SNDMORE, KM_TIMEOUT);

        if (!val.empty())
        {
            z_send(*km, val, flag.empty() ? 0 : ZMQ_SNDMORE, KM_TIMEOUT);
        }

        if (!flag.empty())
        {
            z_send(*km, flag, 0, KM_TIMEOUT);
        }

        // use a reasonable time-out, in case Keymaster is gone.
        z_recv(*km, response, KM_TIMEOUT);

        yr.from_yaml_node(YAML::Load(response));
        _r = yr;
        return yr;
    }
    catch (YAML::Exception &e)
    {
        msg << e.what();
        yr.err = msg.str();
        yr.result = false;
        _r = yr;
        return yr;
    }
    catch (MatrixException &e)
    {
        _handle_keymaster_server_exception();
        msg << e.what();
        yr.err = msg.str();
        yr.result = false;
        _r = yr;
        return yr;
    }
    catch (zmq::error_t &e)
    {
        msg << e.what();
        yr.err = msg.str();
        yr.result = false;
        _r = yr;
        return yr;
    }
    catch (std::exception &e)
    {
        msg << e.what();
        yr.err = msg.str();
        yr.result = false;
        _r = yr;
        return yr;
    }

}

/**
 * Closes the socket to deal with problems such as the Keymaster
 * server disappearing. Since the socket is a ZMQ_REQ socket, sending
 * without being able to receive puts the socket into a state in which
 * it cannot resend. The shared pointer is reset, so that the
 * companion function `_keymaster_socket()` knows to create a new one
 * and reconnect.
 *
 */

void Keymaster::_handle_keymaster_server_exception()
{
    int zero = 0;

    _km_->setsockopt(ZMQ_LINGER, &zero, sizeof zero);
    _km_->close();
    _km_.reset();
}

/**
 * Attempts to return a valid zmq::socket_t connected to the keymaster
 * server. If the function is unable to connect the socket it throws a
 * zmq::error_t. This handles the potential case of the keymaster
 * going away before this client does. In that case,
 * `_handle_keymaster_server_exception()` closes the socket and resets
 * the shared pointer prior to `get()`, `put()` and `del()` throwing
 * an exception. Instead of using the `_km_` shared pointer directly,
 * `get()`, `put()` and `del()` all call this function to obtain a
 * share of this pointer to the socket; if it was previously closed
 * and reset, this will construct a new one and attempt to reconnect
 * it.
 *
 * @return std::shared_ptr<zmq::socket_t>, which will point to a
 * socket connected to the Keymaster server.
 *
 */

shared_ptr<zmq::socket_t> Keymaster::_keymaster_socket()
{
    if (_km_.get())
    {
        return _km_;
    }

    _km_.reset(new zmq::socket_t(ZMQContext::Instance()->get_context(), ZMQ_REQ));
    _km_->connect(_km_url.c_str());
    return _km_;
}

/**
 * Returns a YAML::Node corresponding to the keychain 'key'.
 *
 * example:
 *
 *      Keymaster km("inproc://keymaster");
 *      YAML::Node n = km.get("components.nettask.source.URLs");
 *
 * @param key: The keychain. Keychains are a sequence of keys separated
 * by periods (".") which will specify a path to a value in the
 * keystore.
 *
 * @return A YAML::Node corresponding to the keychain.
 *
 */

YAML::Node Keymaster::get(std::string key)
{
    yaml_result yr;

    if (!get(key, yr))
    {
        throw KeymasterException(yr.err);
    }

    return yr.node;
}

bool Keymaster::get(std::string key, yaml_result &yr)
{
    string cmd("GET");

    yr = _call_keymaster(cmd, key);
    return yr.result;
}

/**
 * Puts a YAML::Node representing some value at the node represented by
 * the given keychain. Will optionally create new nodes if some part of
 * the keychain does not exist.
 *
 * example:
 *
 *      Keymaster km("inproc://keymaster");
 *      int packets;
 *      string n;
 *      ...
 *
 *      // update packets processed
 *      n = to_string(packets)
 *      km.put_nb("STATUS.PACKETS", n);
 *
 * The key "STATUS.PACKETS", the string 'n', and a boolean parameter
 * (default is false) will be packed into a std::tuple, and placed
 * into a FIFO. Another thread will process the other end of the FIFO,
 * and communicate the request to the KeymasterServer. The calling
 * thread can continue long before this process is complete.
 *
 * @param key: The keychain. Keychains are a sequence of keys separated
 * by periods (".") which will specify a path to a value in the
 * keystore.
 *
 * @param n: The new value to place at the end of the keychain.
 *
 * @param create: If true, the keymaster will create a new node for 'n',
 * and any new nodes it needs in between the last good key on the chain
 * and the key corresponding to 'n'.
 *
 */

void Keymaster::put_nb(std::string key, std::string n, bool create)
{
    _run_put(); // does nothing if already running

    tuple<string, string, bool> state(key, n, create);
    _put_fifo.put_no_block(state);
}

/**
 * Puts a YAML::Node representing some value at the node represented by
 * the given keychain. Will optionally create new nodes if some part of
 * the keychain does not exist. Does not block during the put, placing
 * all the data into a queue for transmission by another thread. This
 * is useful in situations where execution time becomes critical,
 * and/or the actual results of the put don't matter.
 *
 * example:
 *
 *      Keymaster km("inproc://keymaster");
 *      YAML::Node n = YAML::Load("[IPC, TCP]");
 *      km.put("components.nettask.source.URLs", n);
 *
 * Will throw a KeymasterException if the key is not found and 'create'
 * is false. Will create the new node(s) if 'create' is true.
 *
 * @param key: The keychain. Keychains are a sequence of keys separated
 * by periods (".") which will specify a path to a value in the
 * keystore.
 *
 * @param n: The new yaml node to place at the end of the keychain.
 *
 * @param create: If true, the keymaster will create a new node for 'n',
 * and any new nodes it needs in between the last good key on the chain
 * and the key corresponding to 'n'.
 *
 */

bool Keymaster::put(std::string key, YAML::Node n, bool create)
{
    string cmd("PUT"), create_flag("create");
    yaml_result yr;
    ostringstream val;

    val << n;
    yr = _call_keymaster(cmd, key, val.str(), create ? create_flag : "");
    n.reset();
    return yr.result;
}

/**
 * Deletes the node specified by the keychain 'key' from the keymaster.
 *
 * example:
 *
 *      Keymaster km("inproc://keymaster");
 *      km.delete("components.nettask.source.ID");
 *
 * @param key: The keychain. Keychains are a sequence of keys separated
 * by periods (".") which will specify a path to a value in the
 * keystore.
 *
 */

bool Keymaster::del(std::string key)
{
    string cmd("DEL");
    yaml_result yr;

    yr = _call_keymaster(cmd, key);
    return yr.result;
}

/**
 * Subscribes to a key on the keymaster.
 *
 * Example usage:
 *
 *     template <typename T>
 *     struct MyCallback : public KeymasterCallbackBase
 *     {
 *         MyCallback(T v)
 *           : data(v)
 *         {}
 *
 *         TCondition<T> data;
 *
 *     private:
 *         void _call(string key, YAML::Node val)
 *         {
 *             data.signal(val.as<T>());
 *         }
 *     };
 *
 *     MyCallback<int> cb(0);
 *     km.subscribe("components.nettask.source.ID", &cb);
 *
 * @param key: the subscription key.
 *
 * @param f: A pointer to a KeymasterCallbackBase functor. This functor will
 * be called whenever the value represented by 'key' updates on the
 * keymaster. NOTE: The function does not assume ownership of this
 * object! This should be managed by the thread calling this function.
 *
 * @return: true if all went well. false means that the subscription
 * failed, which could happen if the keymaster is not running, so the
 * subscription thread could not be started (the subscription thread
 * needs to get the subscription urls from the Keymaster).
 *
 */

bool Keymaster::subscribe(string key, KeymasterCallbackBase *f)
{
    // first start the subscriber thread. If it's already running this
    // won't do anything.

    try
    {
        _run();
    }
    catch (KeymasterException &e)
    {
        cerr << Time::isoDateTime(Time::getUTC())
             << " -- " << e.what() << endl
             << "Unable to obtain the Keymaster publishing URLs. "
             << "Ensure a Keymaster is running and try again."
             << endl;
        return false;
    }

    // Next, request the subscription by posting a request to the
    // subscriber thread.
    zmq::socket_t pipe(ZMQContext::Instance()->get_context(), ZMQ_REQ);
    pipe.connect(_pipe_url.c_str());
    z_send(pipe, SUBSCRIBE, ZMQ_SNDMORE);
    z_send(pipe, key, ZMQ_SNDMORE);
    z_send(pipe, f, 0);
    int rval;
    z_recv(pipe, rval);
    return rval ? true : false;
}

/**
 * Unsubscribes to a key on the keymaster. Has no effect if the key is
 * not subscribed to.
 *
 * @param key: The key to unsubscribe to.
 *
 */

bool Keymaster::unsubscribe(string key)
{
    // request that the subscriber thread unsubscribe from 'key'
    zmq::socket_t pipe(ZMQContext::Instance()->get_context(), ZMQ_REQ);
    pipe.connect(_pipe_url.c_str());
    z_send(pipe, UNSUBSCRIBE, ZMQ_SNDMORE);
    z_send(pipe, key, 0, 1000);
    int rval;
    z_recv(pipe, rval, 1000);
    return rval ? true : false;
}

/**
 * Makes an RPC call indirectly ('Linda' model) via the Keymaster, by
 * writing to 'key', which the service is subscribed to. The service
 * responds by writing back to this key (client writes to
 * <key>.request, and server to <key>.reply)
 *
 * @param key: The RPC service key, a period-separated string
 * @param params: A YAML:Node containing the parameters needed by the
 * service
 * @param to: Time out, in milliseconds.
 *
 * @return A yaml_result containing the success flag, key, and
 * YAML::Node with the results.
 *
 */


yaml_result Keymaster::rpc(string key, YAML::Node params, Time::Time_t to_ms)
{
    auto send_key = key + ".request";
    auto reply_key = key + ".reply";
    yaml_result reply(false);
    Time::Time_t to_ns = to_ms * 1000000UL;
    KeymasterRPCCB cb;

    if (subscribe(reply_key, &cb))
    {
        if (put(send_key, params))
        {
            reply = cb.rval(to_ns);
        }

        if (not unsubscribe(reply_key))
        {
            cerr << "Unable to unsubscribe to " << reply_key << endl;
        }
    }

    return reply;
}

/**
 * Returns a copy of the latest yaml_result.
 *
 * @param
 *
 * @return A yaml_result which is a copy of the latest result.
 *
 */

yaml_result Keymaster::get_last_result()
{
    ThreadLock<Mutex> lck(_shared_lock);
    lck.lock();
    return _r;
}

/**
 * Starts the subscriber thread, if it is not already running.
 *
 */

void Keymaster::_run()
{
    ThreadLock<Mutex> lck(_shared_lock);

    // If the subscriber thread is not running we will need to get the
    // keymaster publishing urls for it before we start it. We obtain
    // the publishing urls here in _run() because doing so in the
    // constructor would cause an exception to be generated if the
    // KeymasterServer is not running.
    if (!_subscriber_thread.running())
    {
        for (int i = 0; i < 10; ++i)
        {
            // get the keymaster publishing URLs:
            try
            {
                _km_pub_urls = get_as<vector<string> >("Keymaster.URLS.AsConfigured.Pub");
            }
            catch (KeymasterException &e)
            {
                // in case of race condition, give Keymaster time
                // (100mS) to start up. On the 10th try give up and
                // re-throw the exception.
                Time::thread_delay(100000000);

                if (i == 9)
                {
                    throw(e);
                }
            }
        }
    }

    lck.lock();

    // Check again, but this time after locking. Locking before
    // getting the subscriber urls leads to a deadlock; but if we
    // check to see if it is running, then get the URLs, then lock,
    // another thread could have already started doing this. This is
    // why we check twice. The worst that can happen is that the
    // publishing urls are needlesly retrieved.
    if (!_subscriber_thread.running())
    {
        if ((_subscriber_thread.start() != 0) || (!_subscriber_thread_ready.wait(true, 1000000)))
        {
            throw(runtime_error(string("Keymaster: unable to start subscriber thread")));
        }
    }
}

/**
 * The subscriber thread. This thread handles all the subscription
 * related activities. It receives and acts on requests to subscribe and
 * unsubscribe, and it also receives and handles the data received from
 * the keymaster's publisher. This thread is responsible for running the
 * callback functors paired with the keys, so it should be kept in mind
 * that that code executes asynchronously from client code.
 *
 */

void Keymaster::_subscriber_task()
{
    zmq::socket_t sub_sock(ZMQContext::Instance()->get_context(), ZMQ_SUB);
    zmq::socket_t pipe(ZMQContext::Instance()->get_context(), ZMQ_REP);
    vector<string>::const_iterator cvi;

    // use the URL that has the same transport as the keymaster URL
    cvi = find_if(_km_pub_urls.begin(), _km_pub_urls.end(), same_transport_p(_km_url));

    // TBF: What to do if they don't match? currently just quit.
    if (cvi == _km_pub_urls.end())
    {
        cerr << Time::isoDateTime(Time::getUTC())
             << " -- Publisher URL transport mismatch with the keymaster" << endl;
        return;
    }

    string the_url = *cvi;

    try
    {
        sub_sock.connect(the_url.c_str());
        pipe.bind(_pipe_url.c_str());
    }
    catch (zmq::error_t &e)
    {
        cerr << "Error in Keymaster::_subscriber_task(): " << e.what() << endl;
        cerr << "Subscription URL = " << the_url << endl;
        cerr << "Pipe URL = " << _pipe_url << endl;
        cerr << "There will be no Keymaster events." << endl;
    }

    _subscriber_thread_ready.signal(true);

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
                    KeymasterCallbackBase *f_ptr;
                    z_recv(pipe, key);
                    z_recv(pipe, f_ptr);

                    // Publisher publishes this as 'Root'. A
                    // subscription with an empty key subscribes to all keys.
                    if (key.empty())
                    {
                        key = "Root";
                    }

                    _callbacks[key] = f_ptr;
                    sub_sock.setsockopt(ZMQ_SUBSCRIBE, key.c_str(), key.length());
                    z_send(pipe, 1, 0);
                }
                else if (msg == UNSUBSCRIBE)
                {
                    string key;
                    z_recv(pipe, key);

                    if (key.empty())
                    {
                        key = "Root";
                    }

                    sub_sock.setsockopt(ZMQ_UNSUBSCRIBE, key.c_str(), key.length());

                    if (_callbacks.find(key) != _callbacks.end())
                    {
                        _callbacks.erase(key);
                    }

                    z_send(pipe, 1, 0);
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
                vector<string> val;
                z_recv(sub_sock, key);
                z_recv_multipart(sub_sock, val);

                if (!val.empty())
                {
                    map<string, KeymasterCallbackBase *>::const_iterator mci;
                    mci = _callbacks.find(key);

                    if (mci != _callbacks.end())
                    {
                        YAML::Node n = YAML::Load(val[0]);
                        mci->second->exec(mci->first, n);
                    }
                }
            }
        }
        catch (zmq::error_t &e)
        {
            cerr << Time::isoDateTime(Time::getUTC())
                 << " -- Keymaster subscriber task: " << e.what() << endl;
            break;
        }
        catch (YAML::Exception &e)
        {
            cerr << Time::isoDateTime(Time::getUTC())
                 << " -- Keymaster subscriber task: " << e.what() << endl;
        }
    }

    int zero = 0;
    pipe.setsockopt(ZMQ_LINGER, &zero, sizeof zero);
    pipe.close();
    zero = 0;
    sub_sock.setsockopt(ZMQ_LINGER, &zero, sizeof zero);
    sub_sock.close();
}

/**
 * Starts the deferred put thread, if it is not already running.
 *
 */

void Keymaster::_run_put()
{
    ThreadLock<Mutex> lck(_shared_lock);

    lck.lock();
    _put_thread_run = true;

    if (!_put_thread.running())
    {
        if ((_put_thread.start() != 0) || (!_put_thread_ready.wait(true, 1000000)))
        {
            throw(runtime_error(string("Keymaster: unable to start deferred put thread")));
        }
    }
}

/**
 * Thread entry point for the deferred put thread. Reads a tuple out
 * of the status queue, splitting it into a key, a value, and a create
 * flag.
 *
 */

void Keymaster::_put_task()
{
    tuple<string, string, bool> state;
    map<string, string> memo;
    string key, message;
    bool create = false;
    map<string, string>::iterator mi;

    _put_thread_ready.signal(true);

    while (_put_thread_run)
    {
        if (_put_fifo.timed_get(state, 5000000))
        {
            key = std::get<0>(state);
            message = std::get<1>(state);

            key.shrink_to_fit();
            message.shrink_to_fit();

            if ((mi = memo.find(key)) != memo.end())
            {
                if (mi->second == message)
                {
                    continue; // don't spam the keymaster with
                              // duplicate values.
                }

                create = false;
            }
            else if (std::get<2>(state)) // new key, must set 'create' flag
            {                            // but only if caller requested it.
                create = true;
            }

            memo[key] = message;
            put(key, YAML::Node(message), create);
        }
    }
}
